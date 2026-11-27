#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import argparse
import email.utils
import hashlib
import http.client
import json
import os
from pathlib import Path
import re
import shutil
import sys
import tarfile
import time
from urllib.error import HTTPError, URLError
from urllib.parse import quote
from urllib.request import Request, urlopen


ATTEMPTS = 7
SYSTEM_ARCHIVE = 'system_dlkm_staging_archive.tar.gz'
SYSTEM_DIR = 'system_dlkm'
MEMBER_LIMIT = 64 * 1024 ** 2
EXTRA_ARTIFACTS = frozenset({
    'manifest.xml',
    'abi_symbollist',
    'gki_aarch64_protected_modules',
})


def backoff(attempt, headers=None):
    delay = min(60, 2 ** (attempt + 1))
    retry_after = headers.get('Retry-After') if headers else None
    if retry_after:
        try:
            delay = max(delay, float(retry_after))
        except ValueError:
            try:
                delay = max(delay, email.utils.parsedate_to_datetime(retry_after).timestamp() - time.time())
            except (ValueError, TypeError, OverflowError):
                pass
    time.sleep(min(120, max(0, delay)))


def open_request(url, offset=0):
    headers = {'User-Agent': 'mindone-gki-fetch/1', 'Accept-Encoding': 'identity'}
    if offset:
        headers['Range'] = f'bytes={offset}-'
    return urlopen(Request(url, headers=headers), timeout=60)


def measure(path):
    digest = hashlib.sha256()
    size = 0
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            size += len(chunk)
            digest.update(chunk)
    return size, digest.hexdigest()


def download(url, path, mandatory):
    partial = path.with_name(path.name + '.part')
    path.parent.mkdir(parents=True, exist_ok=True)
    for attempt in range(ATTEMPTS):
        offset = partial.stat().st_size if partial.exists() else 0
        try:
            with open_request(url, offset) as response:
                length = response.headers.get('Content-Length')
                total = int(length) if length is not None else None
                if response.status == 206:
                    match = re.fullmatch(r'bytes ([0-9]+)-([0-9]+)/([0-9]+)',
                                         response.headers.get('Content-Range', ''))
                    if not match or int(match[1]) != offset or int(match[2]) != int(match[3]) - 1:
                        raise ValueError(f'Invalid Content-Range for {url}')
                    if total is not None and total != int(match[3]) - offset:
                        raise ValueError(f'Inconsistent Content-Length for {url}')
                    total = int(match[3])
                    mode = 'ab'
                elif response.status == 200:
                    mode = 'wb'
                else:
                    raise ValueError(f'Unexpected HTTP status {response.status} for {url}')
                with partial.open(mode) as stream:
                    while True:
                        chunk = response.read(1024 * 1024)
                        if not chunk:
                            break
                        stream.write(chunk)
                    stream.flush()
                    os.fsync(stream.fileno())
                if total is not None and partial.stat().st_size != total:
                    raise OSError(f'Incomplete download: {url}')
            partial.replace(path)
            return True
        except HTTPError as error:
            if error.code == 404 and not mandatory:
                return False
            if error.code == 416:
                partial.unlink(missing_ok=True)
            elif error.code not in (408, 429, 500, 502, 503, 504):
                raise
            if attempt == ATTEMPTS - 1:
                raise
            backoff(attempt, error.headers)
        except (OSError, URLError, http.client.HTTPException):
            if attempt == ATTEMPTS - 1:
                raise
            backoff(attempt)
    raise RuntimeError(f'Download attempts exhausted: {url}')


def mapped_artifacts(mapping_path, build_id, include_optional):
    configs = json.loads(mapping_path.read_text(encoding='utf-8'))['download_configs']
    result = []
    for filename, config in sorted(configs.items()):
        if filename == 'ci_target_mapping.json':
            continue
        mandatory = bool(config['mandatory'])
        if not include_optional and not mandatory and filename not in EXTRA_ARTIFACTS:
            continue
        parts = Path(filename).parts
        if Path(filename).is_absolute() or '..' in parts or not parts:
            raise ValueError(f'Unsafe artifact path in CI mapping: {filename}')
        remote = config['remote_filename_fmt'].replace('{build_number}', build_id)
        result.append((filename, remote, mandatory))
    if not result:
        raise ValueError('CI target mapping lists no artifacts')
    return result


def unpack_system_dlkm(destination, expected_size, expected_sha256):
    archive_path = destination / SYSTEM_ARCHIVE
    if measure(archive_path) != (expected_size, expected_sha256):
        raise ValueError(f'{SYSTEM_ARCHIVE} digest mismatch')
    target = destination / SYSTEM_DIR
    temporary = destination / (SYSTEM_DIR + '.part')
    if temporary.exists():
        shutil.rmtree(temporary)
    if target.exists():
        shutil.rmtree(target)
    temporary.mkdir()
    seen = set()
    files = 0
    with tarfile.open(archive_path, mode='r:gz') as archive:
        for member in archive:
            name = member.name.removeprefix('./').rstrip('/')
            parts = name.split('/')
            if (name in seen or name.startswith('/') or '..' in parts or '' in parts[1:]
                    or not (member.isdir() or member.isfile())):
                raise ValueError(f'Unsafe or duplicate member in {SYSTEM_ARCHIVE}: {member.name}')
            seen.add(name)
            path = temporary / name if name else temporary
            if member.isdir():
                path.mkdir(parents=True, exist_ok=True)
                continue
            if not 0 <= member.size <= MEMBER_LIMIT:
                raise ValueError(f'Oversized member in {SYSTEM_ARCHIVE}: {member.name}')
            path.parent.mkdir(parents=True, exist_ok=True)
            with archive.extractfile(member) as source, path.open('xb') as stream:
                shutil.copyfileobj(source, stream)
            files += 1
    if not (temporary / 'flatten/lib/modules').is_dir():
        raise ValueError(f'{SYSTEM_ARCHIVE} lacks flatten/lib/modules')
    temporary.replace(target)
    return files


def fetch(build_id, target, destination, include_optional, mapping_only):
    destination.mkdir(parents=True, exist_ok=True)
    artifacts = [('ci_target_mapping.json', 'ci_target_mapping.json', True)]
    rows = []
    missing = []
    index = 0
    while index < len(artifacts):
        filename, remote, mandatory = artifacts[index]
        index += 1
        url = f'https://ci.android.com/builds/submitted/{build_id}/{target}/latest/raw/{quote(remote)}'
        path = destination / filename
        if not download(url, path, mandatory):
            missing.append(filename)
            continue
        size, sha256 = measure(path)
        rows.append({'file': filename, 'remote': remote, 'size': size, 'sha256': sha256, 'mandatory': mandatory})
        if filename == 'ci_target_mapping.json':
            if mapping_only:
                break
            artifacts += mapped_artifacts(path, build_id, include_optional)
    unpacked_files = 0
    if not mapping_only:
        required = {name for name, _, mandatory in artifacts if mandatory}
        if not required.issubset(row['file'] for row in rows):
            raise RuntimeError('Mandatory GKI artifacts are missing')
        sys_rows = [row for row in rows if row['file'] == SYSTEM_ARCHIVE]
        if sys_rows:
            unpacked_files = unpack_system_dlkm(destination, sys_rows[0]['size'], sys_rows[0]['sha256'])
    manifest_path = destination / 'artifacts-manifest.json'
    manifest_path.write_text(json.dumps({
        'build': build_id,
        'target': target,
        'rows': rows,
        'missing_optional': missing,
        'unpacked_system_dlkm_files': unpacked_files,
    }, indent=2) + '\n', encoding='utf-8')
    return {
        'ok': True,
        'build': build_id,
        'target': target,
        'directory': str(destination),
        'artifacts': len(rows),
        'missing_optional': missing,
        'unpacked_system_dlkm_files': unpacked_files,
    }


def main():
    parser = argparse.ArgumentParser(
        description='Fetch pinned Google GKI prebuilt artifacts from ci.android.com using ci_target_mapping.json.'
    )
    parser.add_argument('--gki-base', type=Path, help='Path to mindone/gki-base.json')
    parser.add_argument('--build-id', help='Android CI build ID (digits only)')
    parser.add_argument('--target', default='kernel_aarch64')
    parser.add_argument('--dest', type=Path, required=True)
    parser.add_argument('--include-optional', action='store_true')
    parser.add_argument('--mapping-only', action='store_true')
    args = parser.parse_args()
    build_id = args.build_id
    target = args.target
    if args.gki_base:
        pin = json.loads(args.gki_base.read_text(encoding='utf-8'))
        build_id = build_id or str(pin['gki_build_id'])
        target = pin.get('gki_target', target)
    if not build_id or not re.fullmatch(r'[0-9]+', build_id):
        raise SystemExit('fetch-gki-prebuilt.py: provide --build-id or --gki-base with a numeric gki_build_id')
    result = fetch(build_id, target, args.dest.resolve(), args.include_optional, args.mapping_only)
    print(json.dumps(result, separators=(',', ':')))
    return 0


if __name__ == '__main__':
    sys.exit(main())

import os, subprocess, json, glob
SET = os.path.expanduser('~/opt/k6set-2009g')
kos = sorted(glob.glob(os.path.join(SET, '*.ko')))
NM = '/opt/toolchains/llvm-19.1.4-aarch64/bin/llvm-nm'
if not os.path.exists(NM):
    NM = 'nm'
prov, need = {}, {}
for ko in kos:
    name = os.path.basename(ko)[:-3]
    out = subprocess.run([NM, '--defined-only', ko], capture_output=True, text=True).stdout
    for l in out.splitlines():
        p = l.split()
        if len(p) >= 3 and p[1] in ('T', 't', 'D', 'd', 'B', 'b', 'R', 'r'):
            prov.setdefault(p[2], name)
    out = subprocess.run([NM, '-u', ko], capture_output=True, text=True).stdout
    need[name] = {l.split()[-1] for l in out.splitlines() if l.strip()}
deps = {}
for m, syms in need.items():
    d = sorted({prov[s] for s in syms if s in prov and prov[s] != m})
    if d:
        deps[m] = d
json.dump(deps, open('/tmp/mindone-deps.json', 'w'), indent=1)
print('modules with inter-module deps:', len(deps))
for m in sorted(deps)[:8]:
    print('  %-28s <- %s' % (m, ', '.join(deps[m])[:70]))

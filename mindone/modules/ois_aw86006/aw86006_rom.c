// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2026 The LineageOS Project */
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/minmax.h>
#include <linux/string.h>
#include <linux/unaligned.h>
#include "aw86006.h"

#define AW_ROM_VERSION		0x01
#define AW_ROM_REQUEST		0x84
#define AW_ROM_REPLY		0x48
#define AW_ROM_ACK_FLAG		0x01
#define AW_ROM_HANK		0x01
#define AW_ROM_FLASH		0x03
#define AW_HANK_CONNECT		0x01
#define AW_HANK_VERSION		0x21
#define AW_HANK_ID		0x31
#define AW_FLASH_WRITE		0x01
#define AW_FLASH_READ		0x11
#define AW_FLASH_ERASE		0x21
#define AW_READ_PREFIX		5
#define AW_ERASE_BLOCK_MS	8
#define AW_ROM_TRIES		2
#define AW_CONNECT_TRIES	5
#define AW_UPDATE_MS		60000
#define AW_HEADER_SUM_FROM	0x0c
#define AW_HEADER_SUM_WORDS	13
#define AW_RECORD_SUM_FROM	12

static const u8 aw86006_ok[1];

u8 aw86006_sum8(const u8 *p, size_t len)
{
	u8 sum = 0;

	while (len--)
		sum += *p++;
	return sum;
}

u32 aw86006_sum32(const u8 *p, size_t words)
{
	u32 sum = 0;
	size_t i;

	for (i = 0; i < words; i++)
		sum += get_unaligned_le32(p + 4 * i);
	return sum;
}

static size_t aw86006_frame(u8 *buf, u8 dir, u8 module, u8 event,
			    const u8 *data, u8 len)
{
	buf[1] = AW_ROM_VERSION;
	buf[2] = dir;
	buf[3] = module;
	buf[4] = event;
	buf[5] = len;
	buf[6] = 0;
	buf[7] = AW_ROM_ACK_FLAG;
	if (len)
		memcpy(buf + AW_ROM_HEAD, data, len);
	buf[8] = aw86006_sum8(buf + AW_ROM_HEAD, len);
	buf[0] = aw86006_sum8(buf + 1, AW_ROM_HEAD - 1);
	return AW_ROM_HEAD + len;
}

static int aw86006_expect(const u8 *rx, u8 module, u8 event,
			  const u8 *data, u8 len)
{
	u8 want[AW_ROM_ACK];

	if (len > AW_ROM_ACK - AW_ROM_HEAD)
		return -EINVAL;
	aw86006_frame(want, AW_ROM_REPLY, module, event, data, len);
	return memcmp(rx, want, AW_ROM_HEAD + len) ? -EPROTO : 0;
}

static int aw86006_reply(const u8 *rx, u8 module, u8 event, size_t data_len)
{
	if (rx[0] != aw86006_sum8(rx + 1, AW_ROM_HEAD - 1) ||
	    rx[1] != AW_ROM_VERSION || rx[2] != AW_ROM_REPLY ||
	    rx[3] != module || rx[4] != event || rx[7] != AW_ROM_ACK_FLAG ||
	    rx[8] != aw86006_sum8(rx + AW_ROM_HEAD, data_len))
		return -EBADMSG;
	return rx[AW_ROM_HEAD] ? -EREMOTEIO : 0;
}

enum aw86006_area aw86006_area_state(int ret)
{
	switch (ret) {
	case 0:
		return AW_AREA_VALID;
	case -ENODATA:
		return AW_AREA_BLANK;
	case -EBADMSG:
		return AW_AREA_INVALID;
	}
	return AW_AREA_FAILED;
}

int aw86006_header_parse(const u8 *hdr, struct aw86006_info *info)
{
	static const u8 magic[8] = "AWINIC";

	if (!memchr_inv(hdr, 0xff, AW_HEADER_LEN))
		return -ENODATA;
	if (memcmp(hdr, magic, sizeof(magic)))
		return -EBADMSG;
	info->checksum = get_unaligned_le32(hdr + 0x08);
	info->app_checksum = get_unaligned_le32(hdr + 0x0c);
	info->app_length = get_unaligned_le32(hdr + 0x10);
	info->app_version = get_unaligned_le32(hdr + 0x14);
	info->app_id = get_unaligned_le32(hdr + 0x18);
	info->move_checksum = get_unaligned_le32(hdr + 0x1c);
	info->move_version = get_unaligned_le32(hdr + 0x20);
	info->move_length = get_unaligned_le32(hdr + 0x24);
	if (aw86006_sum32(hdr + AW_HEADER_SUM_FROM, AW_HEADER_SUM_WORDS) !=
	    info->checksum)
		return -EBADMSG;
	return 0;
}

int aw86006_image_check(const u8 *img, size_t size, struct aw86006_info *info)
{
	int ret;

	if (size < AW_MOVE_LEN + AW_HEADER_LEN)
		return -EINVAL;
	if (size > AW_FLASH_LIMIT - AW_FLASH_BASE)
		return -EFBIG;
	ret = aw86006_header_parse(img + AW_MOVE_LEN, info);
	if (ret)
		return ret;
	if (info->move_length != AW_MOVE_LEN || info->app_id != AW_APP_ID ||
	    info->app_length % 4 ||
	    info->app_length != size - AW_MOVE_LEN - AW_HEADER_LEN)
		return -EINVAL;
	if (aw86006_sum32(img, AW_MOVE_LEN / 4) != info->move_checksum ||
	    aw86006_sum32(img + AW_MOVE_LEN + AW_HEADER_LEN,
			  info->app_length / 4) != info->app_checksum)
		return -EBADMSG;
	return 0;
}

int aw86006_record_check(const u8 *rec, u32 *words, u32 *sum)
{
	if (!memchr_inv(rec, 0xff, AW_RECORD_LEN))
		return -ENODATA;
	*words = get_unaligned_le32(rec + 4);
	if (get_unaligned_le32(rec) != AW_RECORD_MAGIC ||
	    *words > (AW_RECORD_LEN - AW_RECORD_SUM_FROM) / 4)
		return -EBADMSG;
	*sum = aw86006_sum32(rec + AW_RECORD_SUM_FROM, *words);
	return *sum == get_unaligned_le32(rec + 8) ? 0 : -EBADMSG;
}

bool aw86006_flash_writable(u32 addr, u32 len)
{
	return addr >= AW_FLASH_BASE && len &&
	       len <= AW_FLASH_LIMIT - AW_FLASH_BASE &&
	       addr <= AW_FLASH_LIMIT - len;
}

int aw86006_rom_connect(struct aw86006 *aw)
{
	static const u8 ack[] = { 0x00, 0x01, 0x00, 0x00, 0x00 };
	u8 req[AW_ROM_HEAD];
	size_t len;
	int ret;

	len = aw86006_frame(req, AW_ROM_REQUEST, AW_ROM_HANK, AW_HANK_CONNECT,
			    NULL, 0);
	ret = aw86006_rom_xfer(aw, req, len, AW_ROM_GAP_US, aw->rx, AW_ROM_ACK);
	if (!ret)
		ret = aw86006_expect(aw->rx, AW_ROM_HANK, AW_HANK_CONNECT + 1,
				     ack, sizeof(ack));
	memcpy(aw->connect_raw, aw->rx, AW_ROM_ACK);
	aw->connect_ret = ret;
	return ret;
}

int aw86006_rom_query(struct aw86006 *aw, unsigned int which)
{
	static const u8 events[] = { AW_HANK_VERSION, AW_HANK_ID };
	u8 req[AW_ROM_HEAD];
	size_t len;
	int ret;

	if (which >= ARRAY_SIZE(events))
		return -EINVAL;
	len = aw86006_frame(req, AW_ROM_REQUEST, AW_ROM_HANK, events[which],
			    NULL, 0);
	ret = aw86006_rom_xfer(aw, req, len, AW_ROM_GAP_US, aw->rx, AW_ROM_ACK);
	if (!ret)
		ret = aw86006_reply(aw->rx, AW_ROM_HANK, events[which] + 1,
				    AW_ROM_ACK - AW_ROM_HEAD);
	memcpy(aw->query_raw[which], aw->rx, AW_ROM_ACK);
	aw->query_ret[which] = ret;
	return ret;
}

int aw86006_rom_read(struct aw86006 *aw, u32 addr, u8 *out, u8 len)
{
	u8 req[6];
	size_t n;
	int ret = -EINVAL;
	int i;

	if (!len || len > AW_ROM_CHUNK)
		return -EINVAL;
	req[0] = len;
	req[1] = 0;
	put_unaligned_le32(addr, req + 2);
	n = aw86006_frame(aw->tx, AW_ROM_REQUEST, AW_ROM_FLASH, AW_FLASH_READ,
			  req, sizeof(req));
	for (i = 0; i < AW_ROM_TRIES; i++) {
		ret = aw86006_rom_xfer(aw, aw->tx, n, AW_ROM_GAP_US, aw->rx,
				       AW_ROM_ACK + len);
		if (!ret)
			ret = aw86006_reply(aw->rx, AW_ROM_FLASH,
					    AW_FLASH_READ + 1,
					    AW_READ_PREFIX + len);
		if (!ret) {
			memcpy(out, aw->rx + AW_ROM_ACK, len);
			return 0;
		}
	}
	return ret;
}

int aw86006_rom_erase(struct aw86006 *aw, u32 addr, unsigned int blocks)
{
	u8 req[6];
	size_t n;
	int ret = -EIO;
	int i;

	if (!blocks || blocks > U8_MAX || addr % AW_ROM_BLOCK ||
	    !aw86006_flash_writable(addr, blocks * AW_ROM_BLOCK))
		return -EPERM;
	req[0] = blocks;
	req[1] = 0;
	put_unaligned_le32(addr, req + 2);
	n = aw86006_frame(aw->tx, AW_ROM_REQUEST, AW_ROM_FLASH, AW_FLASH_ERASE,
			  req, sizeof(req));
	for (i = 0; i < AW_ROM_TRIES; i++) {
		ret = aw86006_rom_xfer(aw, aw->tx, n,
				       blocks * AW_ERASE_BLOCK_MS * USEC_PER_MSEC,
				       aw->rx, AW_ROM_SHORT_ACK);
		if (!ret)
			ret = aw86006_expect(aw->rx, AW_ROM_FLASH,
					     AW_FLASH_ERASE + 1, aw86006_ok,
					     sizeof(aw86006_ok));
		if (!ret)
			return 0;
	}
	return ret;
}

int aw86006_rom_write(struct aw86006 *aw, u32 addr, const u8 *data, u8 len)
{
	u8 req[4 + AW_ROM_CHUNK];
	size_t n;
	int ret = -EIO;
	int i;

	if (!len || len > AW_ROM_CHUNK || !aw86006_flash_writable(addr, len))
		return -EPERM;
	put_unaligned_le32(addr, req);
	memcpy(req + 4, data, len);
	for (i = 0; i < AW_CONNECT_TRIES; i++) {
		ret = aw86006_rom_connect(aw);
		if (!ret)
			break;
	}
	if (ret)
		return ret;
	n = aw86006_frame(aw->tx, AW_ROM_REQUEST, AW_ROM_FLASH, AW_FLASH_WRITE,
			  req, 4 + len);
	for (i = 0; i < AW_ROM_TRIES; i++) {
		ret = aw86006_rom_xfer(aw, aw->tx, n, AW_ROM_WRITE_US, aw->rx,
				       AW_ROM_SHORT_ACK);
		if (!ret)
			ret = aw86006_expect(aw->rx, AW_ROM_FLASH,
					     AW_FLASH_WRITE + 1, aw86006_ok,
					     sizeof(aw86006_ok));
		if (!ret)
			return 0;
	}
	return ret;
}

int aw86006_rom_read_area(struct aw86006 *aw, u32 addr, u8 *out, size_t len)
{
	size_t off;
	int ret;

	for (off = 0; off < len; off += AW_ROM_CHUNK) {
		if (aw86006_stopping(aw))
			return -EINTR;
		ret = aw86006_rom_read(aw, addr + off, out + off,
				       min_t(size_t, AW_ROM_CHUNK, len - off));
		if (ret)
			return ret;
	}
	return 0;
}

static void aw86006_rom_dump(struct aw86006 *aw)
{
	u32 off;
	int ret;

	for (off = 0; off < AW_FLASH_SIZE; off += AW_ROM_CHUNK) {
		if (aw86006_stopping(aw)) {
			aw->flash_ret = -EINTR;
			return;
		}
		ret = aw86006_rom_read(aw, AW_FLASH_BASE + off, aw->flash + off,
				       AW_ROM_CHUNK);
		if (!ret)
			continue;
		memset(aw->flash + off, 0, AW_ROM_CHUNK);
		if (!aw->flash_bad++) {
			aw->flash_first_bad = AW_FLASH_BASE + off;
			aw->flash_ret = ret;
		}
	}
	aw->flash_complete = true;
}

void aw86006_rom_identify(struct aw86006 *aw, unsigned int what)
{
	int ret = -EIO;
	int i;

	for (i = 0; i < AW_CONNECT_TRIES && !aw86006_stopping(aw); i++) {
		ret = aw86006_rom_connect(aw);
		if (!ret)
			break;
	}
	if (ret) {
		aw->mode = AW_MODE_SILENT;
		return;
	}
	aw->mode = AW_MODE_ROM;
	if (what & AW_IDENTIFY_QUERY) {
		aw86006_rom_query(aw, 0);
		aw86006_rom_query(aw, 1);
	}
	if (what & AW_IDENTIFY_HEADER) {
		aw->header_ret = aw86006_rom_read_area(aw, AW_FLASH_APP,
						       aw->header,
						       AW_HEADER_LEN);
		aw->header_state = aw->header_ret ? AW_AREA_FAILED :
			aw86006_area_state(aw86006_header_parse(aw->header,
								&aw->header_info));
	}
	if (what & AW_IDENTIFY_RECORD) {
		aw->record_ret = aw86006_rom_read_area(aw, AW_FLASH_RECORD,
						       aw->record,
						       AW_RECORD_LEN);
		aw->record_state = aw->record_ret ? AW_AREA_FAILED :
			aw86006_area_state(aw86006_record_check(aw->record,
								&aw->record_words,
								&aw->record_sum));
	}
	if ((what & AW_IDENTIFY_FLASH) && aw->flash)
		aw86006_rom_dump(aw);
}

static int aw86006_rom_pass(struct aw86006 *aw, const u8 *img, size_t size,
			    bool verify, ktime_t end)
{
	u8 buf[AW_ROM_CHUNK];
	size_t off;
	u8 len;
	int ret;

	for (off = 0; off < size; off += AW_ROM_CHUNK) {
		if (off == AW_MOVE_LEN)
			continue;
		if (aw86006_stopping(aw))
			return -EINTR;
		if (ktime_after(ktime_get(), end))
			return -ETIMEDOUT;
		aw->update_addr = AW_FLASH_BASE + off;
		len = min_t(size_t, AW_ROM_CHUNK, size - off);
		if (!verify) {
			ret = aw86006_rom_write(aw, aw->update_addr, img + off,
						len);
		} else {
			ret = aw86006_rom_read(aw, aw->update_addr, buf, len);
			if (!ret && memcmp(buf, img + off, len))
				ret = -EILSEQ;
		}
		if (ret)
			return ret;
	}
	return 0;
}

int aw86006_rom_update(struct aw86006 *aw, const u8 *img, size_t size)
{
	ktime_t end = ktime_add_ms(ktime_get(), AW_UPDATE_MS);
	u8 buf[AW_HEADER_LEN];
	int ret;

	aw->update_addr = AW_FLASH_APP;
	ret = aw86006_rom_erase(aw, AW_FLASH_APP,
				DIV_ROUND_UP(size - AW_MOVE_LEN, AW_ROM_BLOCK));
	if (ret)
		return ret;
	aw->update_addr = AW_FLASH_BASE;
	ret = aw86006_rom_erase(aw, AW_FLASH_BASE, AW_MOVE_LEN / AW_ROM_BLOCK);
	if (ret)
		return ret;
	ret = aw86006_rom_pass(aw, img, size, false, end);
	if (ret)
		return ret;
	ret = aw86006_rom_pass(aw, img, size, true, end);
	if (ret)
		return ret;
	aw->update_addr = AW_FLASH_APP;
	ret = aw86006_rom_write(aw, AW_FLASH_APP, img + AW_MOVE_LEN,
				AW_HEADER_LEN);
	if (ret)
		return ret;
	ret = aw86006_rom_read(aw, AW_FLASH_APP, buf, AW_HEADER_LEN);
	if (!ret && memcmp(buf, img + AW_MOVE_LEN, AW_HEADER_LEN))
		ret = -EILSEQ;
	return ret;
}

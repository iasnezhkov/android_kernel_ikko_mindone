/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2026 The LineageOS Project */
#ifndef AW86006_H
#define AW86006_H

#include <linux/i2c.h>
#include <linux/ktime.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#define AW_ROM_HEAD		9
#define AW_ROM_ACK		14
#define AW_ROM_SHORT_ACK	10
#define AW_ROM_CHUNK		64
#define AW_ROM_BLOCK		512
#define AW_ROM_GAP_US		200
#define AW_ROM_WRITE_US		1000
#define AW_FLASH_BASE		0x01000000
#define AW_FLASH_APP		0x01002000
#define AW_FLASH_LIMIT		0x0100b000
#define AW_FLASH_RECORD		0x0100f000
#define AW_FLASH_SIZE		0x10000
#define AW_MOVE_LEN		0x2000
#define AW_HEADER_LEN		64
#define AW_RECORD_LEN		512
#define AW_APP_ID		0x88
#define AW_RECORD_MAGIC		0x00000100
#define AW_HALL_LEN		8
#define AW_PINS_LEN		12
#define AW_PIN_NAME_LEN		16
#define AW_TRACE_LEN		48

#define AW_IDENTIFY_HEADER	BIT(0)
#define AW_IDENTIFY_RECORD	BIT(1)
#define AW_IDENTIFY_QUERY	BIT(2)
#define AW_IDENTIFY_FLASH	BIT(3)

enum aw86006_mode {
	AW_MODE_UNKNOWN,
	AW_MODE_SILENT,
	AW_MODE_ROM,
	AW_MODE_APP,
	AW_MODE_ABSENT,
};

enum aw86006_area {
	AW_AREA_UNREAD,
	AW_AREA_FAILED,
	AW_AREA_BLANK,
	AW_AREA_INVALID,
	AW_AREA_VALID,
};

enum aw86006_hold {
	AW_HOLD_NONE,
	AW_HOLD_ACKED,
	AW_HOLD_MISSED,
	AW_HOLD_FAILED,
};

enum aw86006_update {
	AW_UPDATE_NONE,
	AW_UPDATE_NO_FILE,
	AW_UPDATE_BAD_IMAGE,
	AW_UPDATE_NEEDS_ROM,
	AW_UPDATE_CURRENT,
	AW_UPDATE_FAILED,
	AW_UPDATE_DONE,
};

enum aw86006_run {
	AW_RUN_IDLE,
	AW_RUN_NO_PROGRAM,
	AW_RUN_ACTIVE,
	AW_RUN_DONE,
	AW_RUN_STOPPED,
	AW_RUN_POWER_OFF,
	AW_RUN_FAILED,
};

struct aw86006_info {
	u32 checksum;
	u32 app_checksum;
	u32 app_length;
	u32 app_version;
	u32 app_id;
	u32 move_checksum;
	u32 move_version;
	u32 move_length;
};

struct aw86006_pin {
	char name[AW_PIN_NAME_LEN];
	ktime_t at;
};

struct aw86006_xfer {
	ktime_t at;
	u16 addr;
	u16 wlen;
	u16 rlen;
	int ret;
};

struct aw86006 {
	struct i2c_client *client;
	struct mutex lock;
	struct notifier_block nb;
	struct delayed_work sample_work;
	bool slow_set;
	int slow_ret;
	bool powered;
	bool abort;
	bool pin_seen;
	bool fresh;
	ktime_t pin_on;
	ktime_t on_at;
	ktime_t off_at;
	s64 off_gap_us;
	unsigned int npins;
	struct aw86006_pin pins[AW_PINS_LEN];
	unsigned int ntrace;
	unsigned int trace_more;
	struct aw86006_xfer trace[AW_TRACE_LEN];
	s64 answer_us;
	unsigned int polls;
	int sys_ret;
	unsigned int sys_polls;
	enum aw86006_hold hold;
	s64 hold_first_us;
	unsigned int hold_writes;
	unsigned int hold_acks;
	enum aw86006_mode mode;
	u8 app_id;
	u32 app_version;
	int connect_ret;
	u8 connect_raw[AW_ROM_ACK];
	int query_ret[2];
	u8 query_raw[2][AW_ROM_ACK];
	enum aw86006_area header_state;
	int header_ret;
	struct aw86006_info header_info;
	u8 header[AW_HEADER_LEN];
	enum aw86006_area record_state;
	int record_ret;
	u32 record_words;
	u32 record_sum;
	u8 record[AW_RECORD_LEN];
	enum aw86006_area record_after;
	u8 *flash;
	bool flash_complete;
	unsigned int flash_bad;
	u32 flash_first_bad;
	int flash_ret;
	enum aw86006_update update;
	int update_ret;
	u32 update_addr;
	u32 update_version;
	enum aw86006_run run;
	unsigned int run_mode;
	int run_ret;
	bool running;
	ktime_t run_end;
	unsigned int samples;
	u8 hall_first[AW_HALL_LEN];
	u8 hall_last[AW_HALL_LEN];
	u8 tx[AW_ROM_HEAD + 4 + AW_ROM_CHUNK];
	u8 rx[AW_ROM_ACK + AW_ROM_CHUNK];
};

int aw86006_rom_xfer(struct aw86006 *aw, u8 *tx, u16 tx_len,
		     unsigned int wait_us, u8 *rx, u16 rx_len);
bool aw86006_stopping(struct aw86006 *aw);

enum aw86006_area aw86006_area_state(int ret);
u8 aw86006_sum8(const u8 *p, size_t len);
u32 aw86006_sum32(const u8 *p, size_t words);
int aw86006_header_parse(const u8 *hdr, struct aw86006_info *info);
int aw86006_image_check(const u8 *img, size_t size,
			struct aw86006_info *info);
int aw86006_record_check(const u8 *rec, u32 *words, u32 *sum);
bool aw86006_flash_writable(u32 addr, u32 len);
int aw86006_rom_connect(struct aw86006 *aw);
int aw86006_rom_query(struct aw86006 *aw, unsigned int which);
int aw86006_rom_read(struct aw86006 *aw, u32 addr, u8 *out, u8 len);
int aw86006_rom_erase(struct aw86006 *aw, u32 addr, unsigned int blocks);
int aw86006_rom_write(struct aw86006 *aw, u32 addr, const u8 *data, u8 len);
int aw86006_rom_read_area(struct aw86006 *aw, u32 addr, u8 *out, size_t len);
void aw86006_rom_identify(struct aw86006 *aw, unsigned int what);
int aw86006_rom_update(struct aw86006 *aw, const u8 *img, size_t size);

#endif

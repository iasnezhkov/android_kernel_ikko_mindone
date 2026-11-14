/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#define M_BASE 0x01000000u
#define M_SIZE 0x10000u
#define M_PROTECT 0x0100b000u
#define M_RECORD 0x0100f000u
#define M_ADDR 0x69
#define M_SYS_ADDR 0x61
#define M_WINDOW_US 6000
#define M_ROM_MAX_HZ 100000
#define M_BUS_HZ 400000
#define M_LOG 8192

enum m_state { M_OFF, M_WINDOW, M_ROM, M_APP };

struct m_packet {
	s64 t_us;
	u8 module;
	u8 event;
	u32 addr;
	u32 count;
};

struct m_xfer {
	s64 t_us;
	int num;
	u16 wlen;
	u8 first;
	u16 rlen;
	u16 addr;
	enum m_state state;
};

struct m_app_write {
	s64 t_us;
	u16 reg;
	u8 val;
};

static struct {
	enum m_state state;
	ktime_t on;
	s64 por_us;
	bool held;
	u8 flash[M_SIZE];
	u8 regs[0x10000];
	u8 reply[256];
	int reply_len;
	ktime_t reply_ready;
	struct m_packet packets[M_LOG];
	int npackets;
	struct m_xfer xfers[M_LOG];
	int nxfers;
	struct m_app_write app_writes[M_LOG];
	int napp_writes;
	int app_reads;
	int app_rom_frames;
	int other_addr;
	int bad_packets;
	int fast_rom;
	int overreads;
	int early_reads;
	int chip_erases;
	int protected_hits;
	int unerased_writes;
	int unknown_packets;
	int hold_writes;
	int hold_bad_bytes;
	s64 hold_first_us;
	s64 hold_last_us;
	long stuck;
	bool rom_silent;
	bool rom_refuses_record;
	bool refuse_sys_unlock;
	u32 corrupt_addr;
	int corrupt_times;
	int drop_writes_from;
	int write_packets;
	bool ois_on;
	ktime_t ois_on_at;
	ktime_t ois_off_at;
	bool ois_on_at_power_off;
	int power_ups;
	s64 quiet_us;
	s64 sys_up_us;
	u8 chip_id;
	int pings;
	int ping_nacks;
	int window_pings;
	int data_nacks;
	int sys_acks;
	int sys_nacks;
	int sys_data;
	s64 first_data_us;
} m;

static ktime_t now_ns;
static bool bus_locked;
static int lock_count;

static struct {
	int nr;
	u16 addr;
	unsigned int hz;
	int sets;
	int fail;
	ktime_t first_set;
} slow;

static struct delayed_work *dworks[8];
static int ndworks;
static void *devm[16];
static int ndevm;
static struct notifier_block *power_nb;
static const struct kernel_param *params[32];
static int nparams;
static const u8 *fw_image;
static size_t fw_size;
static int fw_requests;
static int fw_outstanding;
static char log_text[1 << 16];
static size_t log_len;

static s64 now_us(void)
{
	return now_ns / 1000;
}

ktime_t ktime_get(void)
{
	return now_ns;
}

void usleep_range(unsigned long min, unsigned long max)
{
	assert(min <= max);
	now_ns += (ktime_t)min * 1000;
}

void msleep(unsigned int ms)
{
	now_ns += (ktime_t)ms * 1000000;
}

int harness_log(const char *level, const struct device *dev, const char *fmt, ...)
{
	size_t start;
	va_list ap;
	int n;

	if (log_len + 512 >= sizeof(log_text))
		log_len = 0;
	start = log_len;
	n = snprintf(log_text + log_len, sizeof(log_text) - log_len, "[%lld] %s: ", (long long)now_us(), level);
	log_len += n;
	va_start(ap, fmt);
	n = vsnprintf(log_text + log_len, sizeof(log_text) - log_len, fmt, ap);
	va_end(ap);
	log_len += n;
	if (getenv("OIS_VERBOSE"))
		fputs(log_text + start, stderr);
	return 0;
}

int scnprintf(char *buf, size_t size, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(buf, size, fmt, ap);
	va_end(ap);
	return n < (int)size ? n : (int)size - 1;
}

int sysfs_emit_at(char *buf, int at, const char *fmt, ...)
{
	va_list ap;
	int n;

	assert(at >= 0 && at < PAGE_SIZE);
	va_start(ap, fmt);
	n = vsnprintf(buf + at, PAGE_SIZE - at, fmt, ap);
	va_end(ap);
	assert(n < PAGE_SIZE - at);
	return n;
}

ssize_t memory_read_from_buffer(void *to, size_t count, loff_t *ppos, const void *from, size_t available)
{
	loff_t pos = *ppos;

	if (pos < 0)
		return -EINVAL;
	if ((size_t)pos >= available || !count)
		return 0;
	if (count > available - pos)
		count = available - pos;
	memcpy(to, (const u8 *)from + pos, count);
	*ppos = pos + count;
	return count;
}

static int parse_long(const char *s, unsigned int base, long long *out)
{
	char *end;
	size_t len = strlen(s);
	char tmp[64];

	if (!len || len >= sizeof(tmp) || s[0] == ' ' || s[0] == '+')
		return -EINVAL;
	memcpy(tmp, s, len + 1);
	if (tmp[len - 1] == '\n')
		tmp[--len] = 0;
	if (!len)
		return -EINVAL;
	errno = 0;
	*out = strtoll(tmp, &end, base);
	if (errno || *end)
		return -EINVAL;
	return 0;
}

int kstrtoint(const char *s, unsigned int base, int *res)
{
	long long v;

	if (parse_long(s, base, &v) || v < INT32_MIN || v > INT32_MAX)
		return -EINVAL;
	*res = v;
	return 0;
}

int kstrtouint(const char *s, unsigned int base, unsigned int *res)
{
	long long v;

	if (s[0] == '-' || parse_long(s, base, &v) || v < 0 || v > UINT32_MAX)
		return -EINVAL;
	*res = v;
	return 0;
}

int kstrtobool(const char *s, bool *res)
{
	switch (s[0]) {
	case 'y': case 'Y': case '1':
		*res = true;
		return 0;
	case 'n': case 'N': case '0':
		*res = false;
		return 0;
	}
	return -EINVAL;
}

static int bool_set(const char *val, const struct kernel_param *kp)
{
	return kstrtobool(val, kp->arg);
}

static int bool_get(char *buffer, const struct kernel_param *kp)
{
	return scnprintf(buffer, PAGE_SIZE, "%c\n", *(bool *)kp->arg ? 'Y' : 'N');
}

const struct kernel_param_ops param_ops_bool = { .set = bool_set, .get = bool_get };

static int string_set(const char *val, const struct kernel_param *kp)
{
	const struct kparam_string *kps = kp->arg;

	if (strlen(val) + 1 > kps->maxlen)
		return -ENOSPC;
	strcpy(kps->string, val);
	return 0;
}

static int string_get(char *buffer, const struct kernel_param *kp)
{
	const struct kparam_string *kps = kp->arg;

	return scnprintf(buffer, PAGE_SIZE, "%s\n", kps->string);
}

const struct kernel_param_ops param_ops_string = { .set = string_set, .get = string_get };

int param_set_uint_minmax(const char *val, const struct kernel_param *kp, unsigned int min, unsigned int max)
{
	unsigned int v;

	if (kstrtouint(val, 0, &v) || v < min || v > max)
		return -EINVAL;
	*(unsigned int *)kp->arg = v;
	return 0;
}

int param_get_uint(char *buffer, const struct kernel_param *kp)
{
	return scnprintf(buffer, PAGE_SIZE, "%u\n", *(unsigned int *)kp->arg);
}

int param_get_int(char *buffer, const struct kernel_param *kp)
{
	return scnprintf(buffer, PAGE_SIZE, "%d\n", *(int *)kp->arg);
}

void harness_param_register(const struct kernel_param *kp)
{
	assert(nparams < (int)ARRAY_SIZE(params));
	params[nparams++] = kp;
}

static const struct kernel_param *find_param(const char *name)
{
	int i;

	for (i = 0; i < nparams; i++)
		if (!strcmp(params[i]->name, name))
			return params[i];
	fprintf(stderr, "no parameter %s\n", name);
	abort();
}

static int set_param(const char *name, const char *value)
{
	return find_param(name)->ops->set(value, find_param(name));
}

static const char *get_param(const char *name)
{
	static char buf[PAGE_SIZE];

	find_param(name)->ops->get(buf, find_param(name));
	return buf;
}

void harness_init_dwork(struct delayed_work *dw, void (*fn)(struct work_struct *))
{
	memset(dw, 0, sizeof(*dw));
	dw->work.func = fn;
	assert(ndworks < (int)ARRAY_SIZE(dworks));
	dworks[ndworks++] = dw;
}

bool schedule_delayed_work(struct delayed_work *dw, unsigned long delay)
{
	if (dw->pending)
		return false;
	dw->pending = true;
	dw->due = now_ns + (ktime_t)delay * (1000000000 / HZ);
	return true;
}

bool cancel_delayed_work_sync(struct delayed_work *dw)
{
	bool was = dw->pending;

	assert(!dw->running);
	dw->pending = false;
	return was;
}

static void pump_until(ktime_t until)
{
	for (;;) {
		struct delayed_work *next = NULL;
		int i;

		for (i = 0; i < ndworks; i++)
			if (dworks[i]->pending && dworks[i]->due <= until && (!next || dworks[i]->due < next->due))
				next = dworks[i];
		if (!next)
			break;
		if (next->due > now_ns)
			now_ns = next->due;
		next->pending = false;
		next->running = true;
		next->work.func(&next->work);
		next->running = false;
	}
	if (until > now_ns)
		now_ns = until;
}

static void pump_ms(unsigned int ms)
{
	pump_until(now_ns + (ktime_t)ms * 1000000);
}

void *harness_devm_alloc(size_t size)
{
	void *p = calloc(1, size);

	assert(p && ndevm < (int)ARRAY_SIZE(devm));
	devm[ndevm++] = p;
	return p;
}

static void devm_release(void)
{
	while (ndevm)
		free(devm[--ndevm]);
	ndworks = 0;
}

int imgsensor_power_register_notifier(struct notifier_block *nb)
{
	assert(!power_nb);
	power_nb = nb;
	return 0;
}

int imgsensor_power_unregister_notifier(struct notifier_block *nb)
{
	assert(power_nb == nb);
	power_nb = NULL;
	return 0;
}

int mtk_i2c_set_slow_target(int nr, u16 addr, unsigned int hz)
{
	if (slow.fail)
		return slow.fail;
	if (nr < 0 || addr < 0x08 || addr > 0x77 || (hz && (hz < 10000 || hz > 1000000)))
		return -EINVAL;
	if (slow.hz && (slow.nr != nr || slow.addr != addr)) {
		if (hz)
			return -EBUSY;
		return 0;
	}
	if (hz && !slow.sets++)
		slow.first_set = now_ns;
	slow.nr = hz ? nr : 0;
	slow.addr = hz ? addr : 0;
	slow.hz = hz;
	return 0;
}

int request_firmware(const struct firmware **fw, const char *name, struct device *dev)
{
	struct firmware *f;
	u8 *data;

	fw_requests++;
	*fw = NULL;
	if (!fw_image || strcmp(name, "aw86006-mindone.prog"))
		return -ENOENT;
	f = malloc(sizeof(*f));
	data = malloc(fw_size ? fw_size : 1);
	assert(f && data);
	memcpy(data, fw_image, fw_size);
	f->data = data;
	f->size = fw_size;
	*fw = f;
	fw_outstanding++;
	return 0;
}

void release_firmware(const struct firmware *fw)
{
	if (!fw)
		return;
	free((void *)fw->data);
	free((void *)fw);
	fw_outstanding--;
}

void i2c_lock_bus(struct i2c_adapter *adap, unsigned int flags)
{
	assert(flags == I2C_LOCK_SEGMENT && !bus_locked);
	bus_locked = true;
	lock_count++;
}

void i2c_unlock_bus(struct i2c_adapter *adap, unsigned int flags)
{
	assert(flags == I2C_LOCK_SEGMENT && bus_locked);
	bus_locked = false;
}

static u8 m_sum8(const u8 *p, size_t n)
{
	u8 s = 0;

	while (n--)
		s += *p++;
	return s;
}

static u32 m_sum32(const u8 *p, size_t words)
{
	u32 s = 0;
	size_t i;

	for (i = 0; i < words; i++)
		s += p[4 * i] | p[4 * i + 1] << 8 | p[4 * i + 2] << 16 | (u32)p[4 * i + 3] << 24;
	return s;
}

static u32 m_le32(const u8 *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24;
}

static void m_put32(u8 *p, u32 v)
{
	p[0] = v;
	p[1] = v >> 8;
	p[2] = v >> 16;
	p[3] = v >> 24;
}

static bool m_header_valid(const u8 *h)
{
	static const u8 magic[8] = { 'A', 'W', 'I', 'N', 'I', 'C', 0, 0 };

	return !memcmp(h, magic, 8) && m_sum32(h + 12, 13) == m_le32(h + 8);
}

static u32 m_app_version(void)
{
	return m_le32(m.flash + 0x2000 + 0x14);
}

static void m_reply(u8 module, u8 event, const u8 *data, int len, ktime_t ready)
{
	m.reply[1] = 0x01;
	m.reply[2] = 0x48;
	m.reply[3] = module;
	m.reply[4] = event;
	m.reply[5] = len;
	m.reply[6] = 0;
	m.reply[7] = 0x01;
	memcpy(m.reply + 9, data, len);
	m.reply[8] = m_sum8(m.reply + 9, len);
	m.reply[0] = m_sum8(m.reply + 1, 8);
	m.reply_len = 9 + len;
	m.reply_ready = ready;
}

static void m_ack(u8 module, u8 event, u8 status, ktime_t ready)
{
	m_reply(module, event, &status, 1, ready);
}

static void m_rom_packet(const u8 *b, int len, ktime_t end)
{
	struct m_packet *p;
	u8 data[5 + 64];
	u32 addr, i;
	int n;

	m.reply_len = 0;
	if (len < 9 || b[1] != 0x01 || b[2] != 0x84 || b[6] || b[7] != 0x01 || b[5] != len - 9 ||
	    b[0] != m_sum8(b + 1, 8) || b[8] != m_sum8(b + 9, len - 9)) {
		m.bad_packets++;
		return;
	}
	assert(m.npackets < M_LOG);
	p = &m.packets[m.npackets++];
	memset(p, 0, sizeof(*p));
	p->t_us = end / 1000;
	p->module = b[3];
	p->event = b[4];
	switch (b[3] << 8 | b[4]) {
	case 0x0101:
		if (m.rom_silent)
			return;
		memcpy(data, (const u8[]){ 0x00, 0x01, 0x00, 0x00, 0x00 }, 5);
		m_reply(0x01, 0x02, data, 5, end + 100000);
		return;
	case 0x0121:
	case 0x0131:
		data[0] = 0;
		m_put32(data + 1, b[4] == 0x21 ? 0x00010002 : 0x00008600);
		m_reply(0x01, b[4] + 1, data, 5, end + 100000);
		return;
	case 0x0311:
		n = b[9];
		addr = m_le32(b + 11);
		p->addr = addr;
		p->count = n;
		memset(data, 0, sizeof(data));
		data[0] = 0;
		if (len != 15 || !n || n > 64 || addr < M_BASE || addr + n > M_BASE + M_SIZE)
			data[0] = 1;
		else if (m.rom_refuses_record && addr >= M_RECORD)
			data[0] = 2;
		m_put32(data + 1, addr);
		if (!data[0])
			memcpy(data + 5, m.flash + (addr - M_BASE), n);
		m_reply(0x03, 0x12, data, 5 + (n <= 64 ? n : 0), end + 100000);
		if (!data[0] && m.corrupt_times && addr == m.corrupt_addr) {
			m.corrupt_times--;
			m.reply[14] ^= 0x01;
		}
		return;
	case 0x0301:
		addr = m_le32(b + 9);
		n = len - 13;
		p->addr = addr;
		p->count = n;
		m.write_packets++;
		if (m.drop_writes_from && m.write_packets >= m.drop_writes_from)
			return;
		if (n < 1 || n > 64 || addr < M_BASE || addr + n > M_BASE + M_SIZE) {
			m_ack(0x03, 0x02, 1, end + 500000);
			return;
		}
		if (addr + n > M_PROTECT)
			m.protected_hits++;
		for (i = 0; i < (u32)n; i++) {
			u8 *cell = &m.flash[addr - M_BASE + i];

			if ((*cell & b[13 + i]) != b[13 + i])
				m.unerased_writes++;
			*cell &= b[13 + i];
			if ((long)(addr - M_BASE + i) == m.stuck)
				*cell = 0xff;
		}
		m_ack(0x03, 0x02, 0, end + 500000);
		return;
	case 0x0321:
		n = b[9];
		addr = m_le32(b + 11);
		p->addr = addr;
		p->count = n;
		if (len != 15 || !n || addr % 512 || addr < M_BASE || addr + n * 512 > M_BASE + M_SIZE) {
			m_ack(0x03, 0x22, 1, end + 100000);
			return;
		}
		if (addr + n * 512 > M_PROTECT)
			m.protected_hits++;
		memset(m.flash + (addr - M_BASE), 0xff, n * 512);
		m_ack(0x03, 0x22, 0, end + (ktime_t)n * 8000000);
		return;
	case 0x0323:
		m.chip_erases++;
		memset(m.flash, 0xff, sizeof(m.flash));
		m_ack(0x03, 0x24, 0, end + 100000);
		return;
	}
	m.unknown_packets++;
}

static int m_rom_xfer(struct i2c_msg *msgs, int num, ktime_t start, ktime_t end, bool fast)
{
	struct i2c_msg *rd;
	int i;

	if (num == 2 && !(msgs[0].flags & I2C_M_RD) && msgs[0].len == 1 && !msgs[0].buf[0] &&
	    (msgs[1].flags & I2C_M_RD)) {
		rd = &msgs[1];
		memset(rd->buf, 0, rd->len);
		if (fast) {
			m.fast_rom++;
			return num;
		}
		if (start < m.reply_ready) {
			if (m.reply_len)
				m.early_reads++;
			return num;
		}
		if (m.reply_len && rd->len > m.reply_len)
			m.overreads++;
		memcpy(rd->buf, m.reply, rd->len < m.reply_len ? rd->len : m.reply_len);
		m.reply_len = 0;
		return num;
	}
	if (num == 1 && !(msgs[0].flags & I2C_M_RD)) {
		m_rom_packet(msgs[0].buf, msgs[0].len, end);
		return num;
	}
	if (num == 2 && !(msgs[0].flags & I2C_M_RD) && (msgs[1].flags & I2C_M_RD)) {
		m_rom_packet(msgs[0].buf, msgs[0].len, end);
		for (i = 0; i < msgs[1].len; i++)
			msgs[1].buf[i] = i < msgs[0].len ? msgs[0].buf[i] : (i & 1 ? 0x30 : 0x5a);
		return num;
	}
	m.bad_packets++;
	return num;
}

static u8 m_app_reg(u16 reg)
{
	static const u8 hall[8] = { 0x00, 0x00, 0x80, 0x3c, 0x00, 0x00, 0x00, 0xbd };

	if (reg == 0x0000)
		return m.chip_id;
	if (reg >= 0x0010 && reg < 0x0014)
		return m_app_version() >> (8 * (reg - 0x0010));
	if (reg >= 0xf44c && reg < 0xf454)
		return hall[reg - 0xf44c];
	return m.regs[reg];
}

static void m_app_write(u16 reg, u8 val, ktime_t t)
{
	assert(m.napp_writes < M_LOG);
	m.app_writes[m.napp_writes++] = (struct m_app_write){ t / 1000, reg, val };
	switch (reg) {
	case 0xff1f:
		m.regs[reg] = val == 0xae && !m.refuse_sys_unlock;
		return;
	case 0xff11:
		m.regs[reg] = val;
		m.regs[0xff10] = val != 0x02;
		return;
	case 0xf31f:
		m.regs[reg] = val == 0xa6;
		return;
	case 0xf300:
		if (m.regs[0xf31f] == 1)
			m.regs[reg] = val;
		return;
	case 0x0001:
		if (val && !m.ois_on)
			m.ois_on_at = t;
		if (!val && m.ois_on)
			m.ois_off_at = t;
		m.ois_on = val;
		m.regs[reg] = val;
		return;
	}
	m.regs[reg] = val;
}

static int m_app_xfer(struct i2c_msg *msgs, int num, ktime_t end)
{
	u16 reg;
	int i;

	if (num == 2 && !(msgs[0].flags & I2C_M_RD) && msgs[0].len == 2 && (msgs[1].flags & I2C_M_RD)) {
		reg = msgs[0].buf[0] << 8 | msgs[0].buf[1];
		m.app_reads++;
		for (i = 0; i < msgs[1].len; i++)
			msgs[1].buf[i] = m_app_reg(reg + i);
		return num;
	}
	if (num == 1 && !(msgs[0].flags & I2C_M_RD) && msgs[0].len == 3) {
		reg = msgs[0].buf[0] << 8 | msgs[0].buf[1];
		m_app_write(reg, msgs[0].buf[2], end);
		return num;
	}
	m.app_rom_frames++;
	for (i = 0; i < num; i++)
		if (msgs[i].flags & I2C_M_RD)
			memset(msgs[i].buf, 0, msgs[i].len);
	return num;
}

static void m_settle(ktime_t t)
{
	if (m.state == M_WINDOW && t > m.on + (ktime_t)M_WINDOW_US * 1000)
		m.state = m.held || !m_header_valid(m.flash + 0x2000) ? M_ROM : M_APP;
}

static int m_sys_transfer(struct i2c_msg *msgs, int num, ktime_t start)
{
	now_ns = start + 11 * 1000000000LL / M_BUS_HZ + 50000;
	if (num != 1 || (msgs[0].flags & I2C_M_RD) || msgs[0].len) {
		m.sys_data++;
		return -ENXIO;
	}
	m_settle(start);
	if (m.state == M_APP && start >= m.on + (ktime_t)(M_WINDOW_US + m.sys_up_us) * 1000) {
		m.sys_acks++;
		return num;
	}
	m.sys_nacks++;
	return -ENXIO;
}

static int m_transfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num, bool locked)
{
	struct m_xfer *x;
	unsigned int hz = M_BUS_HZ;
	ktime_t start = now_ns, end;
	long long bits = 2;
	bool ping;
	int i;

	assert(locked == bus_locked);
	assert(num >= 1 && num <= 2);
	if (msgs[0].addr == M_SYS_ADDR)
		return m_sys_transfer(msgs, num, start);
	ping = num == 1 && !(msgs[0].flags & I2C_M_RD) && !msgs[0].len;
	for (i = 0; i < num; i++) {
		if (msgs[i].addr != M_ADDR) {
			m.other_addr++;
			return -ENXIO;
		}
		bits += (msgs[i].len + 1) * 9;
	}
	if (slow.hz && slow.nr == adap->nr && slow.addr == M_ADDR && slow.hz < hz)
		hz = slow.hz;
	end = start + bits * 1000000000LL / hz + 50000;
	now_ns = end;
	assert(m.nxfers < M_LOG);
	x = &m.xfers[m.nxfers++];
	x->t_us = start / 1000;
	x->num = num;
	x->wlen = msgs[0].flags & I2C_M_RD ? 0 : msgs[0].len;
	x->first = x->wlen ? msgs[0].buf[0] : 0;
	x->rlen = msgs[num - 1].flags & I2C_M_RD ? msgs[num - 1].len : 0;
	x->addr = msgs[0].addr;
	if (m.state == M_OFF || start < m.on + (ktime_t)m.por_us * 1000 ||
	    start < m.on + (ktime_t)m.quiet_us * 1000) {
		x->state = M_OFF;
		if (ping)
			m.ping_nacks++;
		else
			m.data_nacks++;
		return -ENXIO;
	}
	m_settle(start);
	x->state = m.state;
	if (ping) {
		if (m.state == M_WINDOW) {
			m.window_pings++;
			m.held = true;
		}
		m.pings++;
		return num;
	}
	if (m.first_data_us < 0)
		m.first_data_us = (start - m.on) / 1000;
	if (m.state == M_WINDOW) {
		m.held = true;
		m.hold_writes++;
		if (m.hold_first_us < 0)
			m.hold_first_us = (start - m.on) / 1000;
		m.hold_last_us = (start - m.on) / 1000;
		for (i = 0; i < num; i++)
			if (msgs[i].flags & I2C_M_RD || memchr_inv(msgs[i].buf, 0xac, msgs[i].len))
				m.hold_bad_bytes++;
		return num;
	}
	if (m.state == M_ROM)
		return m_rom_xfer(msgs, num, start, end, hz > M_ROM_MAX_HZ);
	return m_app_xfer(msgs, num, end);
}

int i2c_transfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	return m_transfer(adap, msgs, num, false);
}

int __i2c_transfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	return m_transfer(adap, msgs, num, true);
}

static void model_reset(void)
{
	memset(&m, 0, sizeof(m));
	memset(m.flash, 0xff, sizeof(m.flash));
	m.por_us = 1000;
	m.chip_id = 0x88;
	m.first_data_us = -1;
	m.stuck = -1;
	m.hold_first_us = -1;
	m.hold_last_us = -1;
	memset(&slow, 0, sizeof(slow));
	fw_image = NULL;
	fw_size = 0;
	fw_requests = 0;
	log_len = 0;
	log_text[0] = 0;
}

static void model_power_on(void)
{
	m.state = M_WINDOW;
	m.on = now_ns;
	m.held = false;
	m.reply_len = 0;
	m.ois_on = false;
	m.first_data_us = -1;
	memset(m.regs, 0, sizeof(m.regs));
	m.regs[0xff10] = 1;
	m.power_ups++;
}

static void model_power_off(void)
{
	if (m.ois_on)
		m.ois_on_at_power_off = true;
	m.ois_on = false;
	m.state = M_OFF;
}

static int count_packets(u8 module, u8 event)
{
	int i, n = 0;

	for (i = 0; i < m.npackets; i++)
		if (m.packets[i].module == module && m.packets[i].event == event)
			n++;
	return n;
}

static bool only_read_packets(void)
{
	int i;

	for (i = 0; i < m.npackets; i++) {
		u16 k = m.packets[i].module << 8 | m.packets[i].event;

		if (k != 0x0101 && k != 0x0121 && k != 0x0131 && k != 0x0311)
			return false;
	}
	return true;
}

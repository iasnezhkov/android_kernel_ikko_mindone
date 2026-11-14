/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef PAGE_SIZE
#define PAGE_SIZE 4096
#define HZ 100
#define GFP_KERNEL 0
#define __iomem
#define BIT(n) (1UL << (n))
#define GENMASK(h, l) (((~0UL) << (l)) & (~0UL >> (63 - (h))))
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define div_u64(a, b) ((uint64_t)(a) / (uint64_t)(b))
#define udelay(us) ((void)(us))
#define mb() ((void)0)

#define I2C_MAX_STANDARD_MODE_FREQ 100000
#define I2C_MAX_FAST_MODE_FREQ 400000
#define I2C_MAX_FAST_MODE_PLUS_FREQ 1000000
#define I2C_MAX_TURBO_MODE_FREQ 1400000
#define I2C_MAX_HIGH_SPEED_MODE_FREQ 3400000
#define I2C_MAX_ULTRA_FAST_MODE_FREQ 5000000
#define I2C_M_RD 0x0001
#define I2C_M_TEN 0x0010
#define I2C_AQ_COMB_WRITE_THEN_READ 0x1
#define I2C_AQ_NO_ZERO_LEN 0x2

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

struct device { int unused; };
struct clk { int unused; };
struct completion { int done; };
struct kernel_param { int unused; };
struct i2c_adapter { int nr; unsigned long timeout; void *algo_data; };
struct i2c_msg { u16 addr; u16 flags; u16 len; u8 *buf; };
struct i2c_adapter_quirks {
	unsigned long flags;
	int max_num_msgs;
	u16 max_write_len;
	u16 max_read_len;
	u16 max_comb_1st_msg_len;
	u16 max_comb_2nd_msg_len;
};

#define i2c_get_adapdata(adap) ((adap)->algo_data)
#define kcalloc(n, size, flags) calloc((n), (size))
#define kfree(p) free(p)

static int dev_info_count, dev_warn_count, verbose;
#define dev_dbg(dev, ...) ((void)(dev))
#define dev_info(dev, ...) do { (void)(dev); dev_info_count++; if (verbose) printf("    dev_info: " __VA_ARGS__); } while (0)
#define dev_warn(dev, ...) do { (void)(dev); dev_warn_count++; if (verbose) printf("    dev_warn: " __VA_ARGS__); } while (0)

#define DEFINE_SPINLOCK(name) int name
#define spin_lock(lock) do { assert(!*(lock)); *(lock) = 1; } while (0)
#define spin_unlock(lock) do { assert(*(lock)); *(lock) = 0; } while (0)

static int scnprintf(char *buf, size_t size, const char *fmt, ...)
{
	va_list args;
	int len;

	va_start(args, fmt);
	len = vsnprintf(buf, size, fmt, args);
	va_end(args);
	if (len < 0)
		return 0;
	return (size_t)len >= size ? (int)size - 1 : len;
}

static const char *kstrtox_fixup_radix(const char *s, unsigned int *base)
{
	if (*base == 0) {
		if (s[0] == '0') {
			if ((s[1] | 0x20) == 'x' && isxdigit((unsigned char)s[2]))
				*base = 16;
			else
				*base = 8;
		} else {
			*base = 10;
		}
	}
	if (*base == 16 && s[0] == '0' && (s[1] | 0x20) == 'x')
		s += 2;
	return s;
}

static int kstrtouint(const char *s, unsigned int base, unsigned int *res)
{
	unsigned long long value = 0;
	unsigned int count = 0;
	bool overflow = false;

	if (s[0] == '+')
		s++;
	s = kstrtox_fixup_radix(s, &base);
	for (;; s++, count++) {
		unsigned int c = (unsigned char)*s, digit;

		if (c >= '0' && c <= '9')
			digit = c - '0';
		else if ((c | 0x20) >= 'a' && (c | 0x20) <= 'f')
			digit = (c | 0x20) - 'a' + 10;
		else
			break;
		if (digit >= base)
			break;
		if (value > (ULLONG_MAX - digit) / base)
			overflow = true;
		value = value * base + digit;
	}
	if (overflow)
		return -ERANGE;
	if (!count)
		return -EINVAL;
	if (*s == '\n')
		s++;
	if (*s)
		return -EINVAL;
	if (value != (unsigned int)value)
		return -ERANGE;
	*res = (unsigned int)value;
	return 0;
}

#define MODEL_LOG 4096

struct reg_write {
	int space;
	unsigned int offset;
	unsigned int value;
	int width;
};

struct model {
	u8 regs[0x1000];
	u8 dma[0x100];
	int clock_on;
	int writes;
	struct reg_write log[MODEL_LOG];
};

#define MODELS 4
static struct model *models[MODELS];

static struct model *model_at(const volatile void *addr, int *space, unsigned int *offset)
{
	const u8 *p = (const u8 *)addr;
	int i;

	for (i = 0; i < MODELS; i++) {
		struct model *m = models[i];

		if (!m)
			continue;
		if (p >= m->regs && p < m->regs + sizeof(m->regs)) {
			*space = 0;
			*offset = (unsigned int)(p - m->regs);
			return m;
		}
		if (p >= m->dma && p < m->dma + sizeof(m->dma)) {
			*space = 1;
			*offset = (unsigned int)(p - m->dma);
			return m;
		}
	}
	fprintf(stderr, "register access outside every model\n");
	abort();
}

static void model_write(const volatile void *addr, unsigned int value, int width)
{
	int space;
	unsigned int offset;
	struct model *m = model_at(addr, &space, &offset);
	u8 *base = space ? m->dma : m->regs;

	assert(m->clock_on > 0);
	assert(m->writes < MODEL_LOG);
	m->log[m->writes].space = space;
	m->log[m->writes].offset = offset;
	m->log[m->writes].value = value;
	m->log[m->writes].width = width;
	m->writes++;
	memcpy(base + offset, &value, (size_t)width);
}

static unsigned int model_read(const volatile void *addr, int width)
{
	int space;
	unsigned int offset, value = 0;
	struct model *m = model_at(addr, &space, &offset);
	u8 *base = space ? m->dma : m->regs;

	assert(m->clock_on > 0);
	memcpy(&value, base + offset, (size_t)width);
	return value;
}

#define writeb(v, a) model_write((a), (u8)(v), 1)
#define writew(v, a) model_write((a), (u16)(v), 2)
#define writel(v, a) model_write((a), (u32)(v), 4)
#define readb(a) ((u8)model_read((a), 1))
#define readw(a) ((u16)model_read((a), 2))
#define readl(a) ((u32)model_read((a), 4))

/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
static void mtk_i2c_init_hw(struct mtk_i2c *i2c);

struct timing_regs {
	u16 timing;
	u16 ltiming;
	u16 hs;
	u16 ext;
	u16 clock_div;
	u16 sda;
};

struct xfer {
	u16 addr;
	u16 flags;
	u16 len;
	int num;
	int left_num;
	int op;
	unsigned int speed_hz;
	struct timing_regs regs;
};

#define XFERS 64

struct dev {
	struct mtk_i2c i2c;
	struct model m;
	int clock_fail;
	int fail_at;
	int fail_ret;
	int xfers;
	struct xfer xfer[XFERS];
};

static struct dev *dev_of(struct mtk_i2c *i2c)
{
	return (struct dev *)((char *)i2c - offsetof(struct dev, i2c));
}

static u16 reg16(struct mtk_i2c *i2c, enum I2C_REGS_OFFSET reg)
{
	u16 value;

	memcpy(&value, dev_of(i2c)->m.regs + i2c->ch_offset_i2c + i2c->dev_comp->regs[reg], sizeof(value));
	return value;
}

static struct timing_regs timing_regs(struct mtk_i2c *i2c)
{
	struct timing_regs regs = {
		.timing = reg16(i2c, OFFSET_TIMING),
		.ltiming = reg16(i2c, OFFSET_LTIMING),
		.hs = reg16(i2c, OFFSET_HS),
		.ext = reg16(i2c, OFFSET_EXT_CONF),
		.clock_div = reg16(i2c, OFFSET_CLOCK_DIV),
		.sda = reg16(i2c, OFFSET_SDA_TIMING),
	};

	return regs;
}

static int mtk_i2c_clock_enable(struct mtk_i2c *i2c)
{
	struct dev *d = dev_of(i2c);

	if (d->clock_fail)
		return -EIO;
	d->m.clock_on++;
	return 0;
}

static void mtk_i2c_clock_disable(struct mtk_i2c *i2c)
{
	struct dev *d = dev_of(i2c);

	assert(d->m.clock_on > 0);
	d->m.clock_on--;
}

static int mtk_i2c_do_transfer(struct mtk_i2c *i2c, struct i2c_msg *msgs, int num, int left_num)
{
	struct dev *d = dev_of(i2c);
	struct xfer *x;

	assert(d->m.clock_on > 0);
	assert(d->xfers < XFERS);
	x = &d->xfer[d->xfers++];
	x->addr = msgs->addr;
	x->flags = msgs->flags;
	x->len = msgs->len;
	x->num = num;
	x->left_num = left_num;
	x->op = i2c->op;
	x->speed_hz = i2c->speed_hz;
	x->regs = timing_regs(i2c);
	if (i2c->op == I2C_MASTER_CONTINUOUS_WR)
		kfree(msgs->buf);
	if (d->fail_at && d->xfers == d->fail_at) {
		mtk_i2c_init_hw(i2c);
		return d->fail_ret;
	}
	return 0;
}

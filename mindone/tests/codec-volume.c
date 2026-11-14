// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct device;
struct regmap;
struct iio_channel;
struct nvmem_device;
struct dentry;
struct regulator;
struct snd_soc_component;

#include CODEC_HEADER

#define dev_info(dev, ...) ((void)(dev), (void)index)
#define BIT(n) (1u << (n))
#define fls(x) model_fls(x)

struct snd_ctl_elem_value {
    struct {
        struct {
            long value[2];
        } integer;
    } value;
};

struct soc_mixer_control {
    unsigned int reg, rreg, shift, rshift;
    int max, min, invert, sign_bit, platform_max;
};

struct snd_kcontrol;
typedef int (*kget_t)(struct snd_kcontrol *, struct snd_ctl_elem_value *);

struct snd_kcontrol_new {
    const char *name;
    struct soc_mixer_control mc;
    kget_t get, put;
    const int *tlv;
};

struct snd_soc_component {
    struct mt6358_priv *priv;
};

struct snd_kcontrol {
    unsigned long private_value;
    struct {
        const char *name;
    } id;
};

static struct snd_soc_component component;
static uint32_t regs[0x4000];
static unsigned int writes;

static int model_fls(unsigned int x)
{
    int r = 0;
    while (x) {
        r++;
        x >>= 1;
    }
    return r;
}

static struct snd_soc_component *snd_soc_kcontrol_component(struct snd_kcontrol *k)
{
    (void)k;
    return &component;
}
#define snd_kcontrol_chip(k) snd_soc_kcontrol_component(k)

static void *snd_soc_component_get_drvdata(struct snd_soc_component *c)
{
    return c->priv;
}

static int snd_soc_volsw_is_stereo(struct soc_mixer_control *mc)
{
    return mc->reg != mc->rreg || mc->rshift;
}

static int snd_soc_component_update_bits(struct snd_soc_component *c, unsigned int reg,
                                         unsigned int mask, unsigned int val)
{
    (void)c;
    uint32_t old = regs[reg];
    regs[reg] = (old & ~mask) | (val & mask);
    writes++;
    return old != regs[reg];
}

static int regmap_read(struct regmap *map, unsigned int reg, unsigned int *val)
{
    (void)map;
    *val = regs[reg];
    return 0;
}

static int reg_to_ctl(struct soc_mixer_control *mc, unsigned int reg, unsigned int shift)
{
    int mask = (1 << model_fls(mc->max)) - 1;
    int val = (reg >> shift) & mask;
    if (val < mc->min)
        val = mc->min;
    if (val > mc->max)
        val = mc->max;
    val -= mc->min;
    if (mc->invert)
        val = mc->max - val;
    return val;
}

static int snd_soc_get_volsw(struct snd_kcontrol *k, struct snd_ctl_elem_value *v)
{
    struct soc_mixer_control *mc = (struct soc_mixer_control *)k->private_value;
    v->value.integer.value[0] = reg_to_ctl(mc, regs[mc->reg], mc->shift);
    if (snd_soc_volsw_is_stereo(mc))
        v->value.integer.value[1] = reg_to_ctl(mc, regs[mc->rreg],
                                         mc->reg == mc->rreg ? mc->rshift : mc->shift);
    return 0;
}

static int mt6358_get_vow(struct snd_kcontrol *k, struct snd_ctl_elem_value *v)
{
    (void)k;
    (void)v;
    return 0;
}

static int mt6358_put_vow(struct snd_kcontrol *k, struct snd_ctl_elem_value *v)
{
    (void)k;
    (void)v;
    return 0;
}

#define DECLARE_TLV_DB_SCALE(name, min, step, mute) int name[3] = {min, step, mute}
#define SOC_DOUBLE_EXT_TLV(xname, xreg, sl, sr, xmax, xinv, g, p, t) \
    {.name = xname, .mc = {.reg = xreg, .rreg = xreg, .shift = sl, .rshift = sr, .max = xmax, \
     .invert = xinv}, .get = g, .put = p, .tlv = t}
#define SOC_SINGLE_EXT_TLV(xname, xreg, s, xmax, xinv, g, p, t) \
    {.name = xname, .mc = {.reg = xreg, .rreg = xreg, .shift = s, .rshift = s, .max = xmax, \
     .invert = xinv}, .get = g, .put = p, .tlv = t}
#define SOC_DOUBLE_R_EXT_TLV(xname, l, r, s, xmax, xinv, g, p, t) \
    {.name = xname, .mc = {.reg = l, .rreg = r, .shift = s, .rshift = s, .max = xmax, \
     .invert = xinv}, .get = g, .put = p, .tlv = t}
#define SOC_SINGLE_BOOL_EXT(xname, xdata, g, p) {.name = xname, .get = g, .put = p}

PRODUCTION_CODE

static int failures;

static void check(const char *name, bool pass)
{
    printf("%s %s\n", pass ? "PASS" : "FAIL", name);
    failures += !pass;
}

static const struct snd_kcontrol_new *control(const char *name)
{
    for (size_t i = 0; i < sizeof(mt6358_snd_controls) / sizeof(mt6358_snd_controls[0]); i++)
        if (!strcmp(mt6358_snd_controls[i].name, name))
            return &mt6358_snd_controls[i];
    return NULL;
}

static int put(const struct snd_kcontrol_new *c, long l, long r)
{
    struct snd_kcontrol k = {.private_value = (unsigned long)&c->mc, .id = {c->name}};
    struct snd_ctl_elem_value v = {.value = {.integer = {{l, r}}}};
    return c->put(&k, &v);
}

static void get(const struct snd_kcontrol_new *c, long out[2])
{
    struct snd_kcontrol k = {.private_value = (unsigned long)&c->mc, .id = {c->name}};
    struct snd_ctl_elem_value v = {.value = {.integer = {{-1, -1}}}};
    c->get(&k, &v);
    out[0] = v.value.integer.value[0];
    out[1] = v.value.integer.value[1];
}

static int db_of(const struct snd_kcontrol_new *c, long value)
{
    return c->tlv[0] + c->tlv[1] * (int)value;
}

static int db_of_reg(unsigned int field)
{
    return 800 - 100 * (int)field;
}

static void dl_control(const char *name, unsigned int reg, bool stereo, int gain_l, int gain_r)
{
    char label[160];
    const struct snd_kcontrol_new *c = control(name);
    struct mt6358_priv *priv = component.priv;
    bool ok = true, gains = true, back = true, tlv = true;

    snprintf(label, sizeof(label), "%s exists with a %s register field", name,
             stereo ? "stereo" : "mono");
    check(label, c && c->mc.reg == reg && c->mc.max == DL_GAIN_N_10DB);
    if (!c)
        return;
    for (long v = 0; v <= c->mc.max; v++) {
        long out[2];
        regs[reg] = 0x1f | (0x1f << 7);
        put(c, v, v);
        unsigned int l = regs[reg] & 0x1f, r = (regs[reg] >> 7) & 0x1f;
        if (l != (unsigned int)(DL_GAIN_N_10DB - v) || (stereo && r != l))
            ok = false;
        if (db_of(c, v) != db_of_reg(l))
            tlv = false;
        if (priv->ana_gain[gain_l] != (int)l || (gain_r >= 0 && priv->ana_gain[gain_r] != (int)(stereo ? r : l)))
            gains = false;
        get(c, out);
        if (out[0] != v || (stereo && out[1] != v))
            back = false;
    }
    snprintf(label, sizeof(label), "%s: value v writes register field %d - v", name, DL_GAIN_N_10DB);
    check(label, ok);
    snprintf(label, sizeof(label), "%s: TLV dB of every value equals the register gain (field 0 = +8 dB)", name);
    check(label, tlv);
    snprintf(label, sizeof(label), "%s: ana_gain follows the written register field", name);
    check(label, gains);
    snprintf(label, sizeof(label), "%s: readback returns the written value", name);
    check(label, back);
    regs[reg] = DL_GAIN_N_40DB | (DL_GAIN_N_40DB << 7);
    long out[2];
    get(c, out);
    snprintf(label, sizeof(label), "%s: powerdown field -40 dB reads as the minimum value 0", name);
    check(label, out[0] == 0 && (!stereo || out[1] == 0));
    long top[2];
    put(c, c->mc.max, c->mc.max);
    get(c, top);
    snprintf(label, sizeof(label), "%s: maximum value is the loudest gain, +8 dB", name);
    check(label, (regs[reg] & 0x1f) == DL_GAIN_8DB && top[0] == c->mc.max);
}

int main(void)
{
    static struct mt6358_priv priv;
    component.priv = &priv;

    dl_control("Headphone Volume", MT6358_ZCD_CON2, true,
               AUDIO_ANALOG_VOLUME_HPOUTL, AUDIO_ANALOG_VOLUME_HPOUTR);
    dl_control("Lineout Volume", MT6358_ZCD_CON1, true,
               AUDIO_ANALOG_VOLUME_LINEOUTL, AUDIO_ANALOG_VOLUME_LINEOUTR);
    dl_control("Handset Volume", MT6358_ZCD_CON3, false, AUDIO_ANALOG_VOLUME_HSOUTL, -1);

    const struct snd_kcontrol_new *pga = control("PGA Volume");
    check("PGA Volume is one stereo control over AUDENC_ANA_CON0/CON1",
          pga && pga->mc.reg == MT6358_AUDENC_ANA_CON0 && pga->mc.rreg == MT6358_AUDENC_ANA_CON1 &&
          pga->mc.shift == RG_AUDPREAMPLGAIN_SFT && RG_AUDPREAMPLGAIN_SFT == RG_AUDPREAMPRGAIN_SFT &&
          pga->mc.max == 4 && !pga->mc.invert);
    if (pga) {
        bool ok = true;
        for (long l = 0; l <= 4; l++)
            for (long r = 0; r <= 4; r++) {
                long out[2];
                regs[MT6358_AUDENC_ANA_CON0] = 0xf8ff;
                regs[MT6358_AUDENC_ANA_CON1] = 0xf8ff;
                put(pga, l, r);
                get(pga, out);
                if (((regs[MT6358_AUDENC_ANA_CON0] >> RG_AUDPREAMPLGAIN_SFT) & RG_AUDPREAMPLGAIN_MASK) != l ||
                    ((regs[MT6358_AUDENC_ANA_CON1] >> RG_AUDPREAMPRGAIN_SFT) & RG_AUDPREAMPRGAIN_MASK) != r ||
                    (regs[MT6358_AUDENC_ANA_CON0] & ~(RG_AUDPREAMPLGAIN_MASK << RG_AUDPREAMPLGAIN_SFT)) != 0xf8ff ||
                    (regs[MT6358_AUDENC_ANA_CON1] & ~(RG_AUDPREAMPRGAIN_MASK << RG_AUDPREAMPRGAIN_SFT)) != 0xf8ff ||
                    priv.ana_gain[AUDIO_ANALOG_VOLUME_MICAMP1] != l ||
                    priv.ana_gain[AUDIO_ANALOG_VOLUME_MICAMP2] != r || out[0] != l || out[1] != r)
                    ok = false;
            }
        check("PGA Volume writes left to CON0 and right to CON1 only, ana_gain and readback follow", ok);
        check("PGA Volume TLV is 0 dB + 6 dB per step", pga->tlv[0] == 0 && pga->tlv[1] == 600);
    }
    check("no fork-only volume names remain",
          !control("Headset Volume") && !control("PGA1 Volume") && !control("PGA2 Volume") &&
          !control("VOW Phase2 Switch"));
    check("Wake-on-Voice Phase2 Switch exists", control("Wake-on-Voice Phase2 Switch") != NULL);
    return failures ? 1 : 0;
}

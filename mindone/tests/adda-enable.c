#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MT6789_DAI_ADDA 0
#define MT6789_DAI_AP_DMIC 1
#define SNDRV_PCM_STREAM_PLAYBACK 0
#define dev_info(dev, ...) ((void)(dev))
#define dev_warn(dev, ...) ((void)(dev))
#define AUDIO_AEE(...) assert(0)

struct regmap {
    uint32_t values[AFE_MAX_REGISTER / 4 + 1];
};

struct mtk_afe_adda_priv {
    int dl_rate;
    int ul_rate;
};

struct mt6789_afe_private {
    struct mtk_afe_adda_priv *dai_priv[2];
};

struct mtk_base_afe {
    void *dev;
    struct regmap *regmap;
    struct mt6789_afe_private *platform_priv;
};

struct snd_pcm_substream {
    int stream;
};

struct snd_pcm_hw_params {
    unsigned int rate;
};

struct snd_soc_dai {
    struct mtk_base_afe *afe;
    int id;
};

static struct mtk_base_afe *snd_soc_dai_get_drvdata(struct snd_soc_dai *dai)
{
    return dai->afe;
}

static unsigned int params_rate(struct snd_pcm_hw_params *params)
{
    return params->rate;
}

static int regmap_write(struct regmap *map, unsigned int reg, unsigned int value)
{
    assert(reg <= AFE_MAX_REGISTER && reg % 4 == 0);
    map->values[reg / 4] = value;
    return 0;
}

static int regmap_update_bits(struct regmap *map, unsigned int reg,
                              unsigned int mask, unsigned int value)
{
    assert(reg <= AFE_MAX_REGISTER && reg % 4 == 0);
    map->values[reg / 4] = (map->values[reg / 4] & ~mask) | (value & mask);
    return 0;
}

static void mtk_adda_ul_src_dmic(struct mtk_base_afe *afe, int id)
{
    (void)afe;
    (void)id;
    assert(0);
}

PRODUCTION_FUNCTIONS

static struct regmap map;
static struct mtk_afe_adda_priv adda;
static struct mt6789_afe_private platform = {.dai_priv = {&adda, &adda}};
static struct mtk_base_afe afe = {.regmap = &map, .platform_priv = &platform};
static struct snd_soc_dai dai = {.afe = &afe, .id = MT6789_DAI_ADDA};
static struct snd_pcm_substream playback = {.stream = SNDRV_PCM_STREAM_PLAYBACK};

static void configure(unsigned int rate)
{
    struct snd_pcm_hw_params params = {.rate = rate};
    assert(mtk_dai_adda_hw_params(&playback, &params, &dai) == 0);
    assert(adda.dl_rate == (int)rate);
    assert(map.values[AFE_ADDA_DL_SRC2_CON1 / 4] == 0xf74f0000u);
}

static void power(bool enabled)
{
    assert(DAPM_REG == 0x108 && DAPM_SHIFT == 0);
    regmap_update_bits(&map, DAPM_REG, 1u << DAPM_SHIFT,
                      (unsigned int)enabled << DAPM_SHIFT);
}

static void initial(void)
{
    memset(&map, 0, sizeof(map));
    configure(48000);
    assert(map.values[0x108 / 4] == 0x83001802u);
    power(true);
    assert(map.values[0x108 / 4] == 0x83001803u);
    power(false);
    assert(map.values[0x108 / 4] == 0x83001802u);
    puts("initial: hw_params before DAPM enable preserves idle and starts playback");
}

static void reroute(void)
{
    initial();
    power(true);
    configure(48000);
    assert(map.values[0x108 / 4] == 0x83001803u);
    power(false);
    power(true);
    configure(48000);
    assert(map.values[0x108 / 4] == 0x83001803u);
    power(false);
    assert(map.values[0x108 / 4] == 0x83001802u);
    puts("reroute: late hw_params preserves the running DAPM supply");
}

static void rates(void)
{
    static const struct {
        unsigned int rate;
        uint32_t expected;
    } cases[] = {
        {8000, 0x03001822}, {11025, 0x13001802}, {12000, 0x23001802},
        {16000, 0x33001822}, {22050, 0x43001802}, {24000, 0x53001802},
        {32000, 0x63001802}, {44100, 0x73001802}, {48000, 0x83001802},
        {96000, 0x93001802}, {192000, 0xa3001802}, {12345, 0x83001802}
    };
    for (unsigned int enabled = 0; enabled <= 1; enabled++) {
        for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            map.values[0x108 / 4] = 0xfffffffeu | enabled;
            configure(cases[i].rate);
            assert(map.values[0x108 / 4] == (cases[i].expected | enabled));
            configure(cases[(i + 1) % (sizeof(cases) / sizeof(cases[0]))].rate);
            assert((map.values[0x108 / 4] & 1) == enabled);
        }
    }
    puts("rates: all supported rates and fallback retain both initial enable states");
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    if (!strcmp(argv[1], "initial"))
        initial();
    else if (!strcmp(argv[1], "reroute"))
        reroute();
    else {
        assert(!strcmp(argv[1], "all"));
        initial();
        reroute();
        rates();
    }
    return 0;
}

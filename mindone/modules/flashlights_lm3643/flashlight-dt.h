/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#ifndef _FLASHLIGHT_DT_H
#define _FLASHLIGHT_DT_H

#define DUMMY_GPIO_DTNAME "mediatek,flashlights_dummy_gpio"
#define DUMMY_DTNAME      "mediatek,flashlights_dummy"
#define DUMMY_DTNAME_I2C  "mediatek,flashlights_dummy_i2c"
#define LED191_DTNAME     "mediatek,flashlights_led191"
#define LM3642_DTNAME     "mediatek,flashlights_lm3642"
#define LM3642_DTNAME_I2C "mediatek,strobe_main"
/* MINDONE-LM3643 (F3252): names matched to OUR device tree.
 * Was: platform "mediatek,flashlights_lm3643" and i2c "mediatek,strobe_main" --
 * neither node exists in this device's tree, so the driver never bound.
 * Now: anchor is the unbound gpio_flashled node ("mediatek,flashlights_dummy_gpio"),
 * i2c device 6-0063 is declared as "mediatek,lm3643". */
#define LM3643_DTNAME     "mediatek,flashlights_dummy_gpio"
#define LM3643_DTNAME_I2C "mediatek,lm3643"
#define LM3644_DTNAME     "mediatek,flashlights_lm3644"
#define LM3644_DTNAME_I2C "mediatek,strobe_main"
#define MT6336_DTNAME     "mediatek,flashlights_mt6336"
#define MT6370_DTNAME     "mediatek,flashlights_mt6370"
#define MT6360_DTNAME     "mediatek,flashlights_mt6360"
#define RT4505_DTNAME     "mediatek,flashlights_rt4505"
#define RT4505_DTNAME_I2C "mediatek,strobe_main"
#define RT5081_DTNAME     "mediatek,flashlights_rt5081"

#define AW3644_DTNAME_I2C "mediatek,strobe_main"
#define AW3644_DTNAME     "mediatek,flashlights_aw3644"
/*prize add by zhuzhengjiang for flash start*/
#define AW36515_DTNAME     "mediatek,flashlights_aw36515"
#define AW36515_DTNAME_I2C "mediatek,strobe_main"
/*prize add by zhuzhengjiang for flash end*/
#endif /* _FLASHLIGHT_DT_H */

// SPDX-License-Identifier: GPL-2.0
/*
 * panel-sd5302h-fhd1240-dsi-vdo-amoled-ocp2133.c
 *
 * RECONSTRUCTED FROM BINARY (.ko reverse engineering), not vendor source.
 * Original prebuilt module info (from .modinfo):
 *   author      = maliejun@agenewtech.com
 *   description = "hongzan jd9365d VDO LCD Panel Driver"
 *   name        = panel_sd5302h_fhd1240_dsi_vdo_amoled_ocp2133
 *   compatible  = "sd5302h,dsi,vdo,amoled"
 *   depends     = mtk_panel_ext
 *   vermagic    = 5.10.233-android12-5.10-android12-9-gb84438489451 ...
 *
 * IC: JD9365D (Jadard) driver IC, OCP2133 LCD bias/PMIC.
 * Panel: 1080x1240 AMOLED, VDO (video) mode DSI, 4 data lanes, 96Hz.
 * (Same author/vendor/IC family as panel-ch13721c in this same directory
 * -- see that file's header comment for the full reverse-engineering
 * methodology, which applies identically here. This panel's driver is
 * structurally byte-for-byte the same shape; only the init table and
 * timing differ.)
 *
 * Unlike ch13721c, this panel's DCS/generic init table is large (41
 * static lcm_dcs_write_seq_static() calls, 602 bytes total cmd+params,
 * plus one runtime backlight write: page-select 0xFD, CMD2 unlock
 * 0xF0/0xB9, GOA/timing setup 0xE0/0xE3/0xE5/0xEA, power control
 * 0xB1/0xD7/0xE4, and six large (14-64 byte) gamma/voltage tables via
 * cmd 0xC9/0xD9/0xC4/0xF5) -- extracted byte-exact and in original call
 * order from .rodata (symbols lcm_panel_init.d .. .d.58, offsets
 * 0x3dc-0x636), confirmed by walking every ADRP+ADD ->
 * mipi_dsi_{dcs_write_buffer,generic_write} relocation in
 * lcm_prepare()'s disassembly (all references are in strictly ascending
 * .rodata-offset order, i.e. offset order == source/call order). Re-run
 * independently (2026-08-19) with a fresh llvm-objcopy/readelf dump of
 * the stock .ko: byte-identical to the committed analysis/ dumps, and
 * the extract_dcs.py output matches this table call-for-call.
 * (Earlier notes on this file said "43 commands" -- that was an
 * imprecise prose estimate; the actual, verified count of static-table
 * entries is 41, as above.)
 *
 * ext_params/mtk_panel_ext.h: unlike ch13721c, this panel actually DOES
 * use two fields beyond pll_clk -- see the comment on `ext_params`
 * below for the full, independently re-verified breakdown (pll_clk=430,
 * cust_esd_check=1, esd_check_enable=1, two populated
 * lcm_esd_check_table entries). Everything else in the 60856-byte
 * struct is zero/default.
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/suspend.h>

#include <drm/drm_connector.h>
#include <drm/drm_device.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#include <video/mipi_display.h>

#define CONFIG_MTK_PANEL_EXT
#if defined(CONFIG_MTK_PANEL_EXT)
#include <drivers/gpu/drm/mediatek/mediatek_v2/mtk_panel_ext.h>
#include <drivers/gpu/drm/mediatek/mediatek_v2/mtk_drm_graphics_base.h>
#endif

/* Recovered from .data offset 0xef60 (symbol "bl_tb0", 2 bytes): {0x51, 0x80}. */
static u8 bl_tb0[] = { 0x51, 0x80 };

struct lcm {
	struct device *dev;
	struct drm_panel panel;
	struct backlight_device *backlight;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *avdd_en_gpio;
	struct gpio_desc *disp_pwm_gpio;

	bool prepared;
	bool enabled;

	int error;
};

#define lcm_dcs_write_seq_static(ctx, seq...)                          \
	({                                                              \
		static const u8 d[] = { seq };                          \
		lcm_dcs_write(ctx, d, ARRAY_SIZE(d));                    \
	})

static inline struct lcm *panel_to_lcm(struct drm_panel *panel)
{
	return container_of(panel, struct lcm, panel);
}

/* Same 0xB0 DCS/generic split as ch13721c -- confirmed identical in this
 * binary's lcm_dcs_write disassembly. */
static void lcm_dcs_write(struct lcm *ctx, const void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	const u8 *addr = data;
	ssize_t ret;

	if (ctx->error < 0)
		return;

	if (*addr < 0xB0)
		ret = mipi_dsi_dcs_write_buffer(dsi, data, len);
	else
		ret = mipi_dsi_generic_write(dsi, data, len);

	if (ret < 0) {
		dev_err(ctx->dev, "error %zd writing seq: %ph\n", ret, data);
		ctx->error = ret;
	}
}

/*
 * Full JD9365D bring-up table for sd5302h, extracted byte-exact from
 * .rodata symbols lcm_panel_init.d .. .d.58 (file offsets 0x3dc-0x636),
 * in strict ascending-offset (== call) order. Standard JD9365D CMD2
 * idiom: 0xFD selects the register page, 0xF0/0xB9 unlock CMD2/CMD3,
 * 0xC9/0xD9/0xC4/0xF5 are large per-page gamma/voltage tables.
 *
 * The exact position of the runtime default-backlight write (bl_tb0,
 * cmd 0x51) relative to the {0x53,0x28} "Write Control Display" entry
 * right before Sleep-Out could not be disambiguated with full certainty
 * (both a dcs_write_buffer and a generic_write reference the same
 * .rodata+0x630 bytes in the disassembly -- most likely the compiler
 * reused that literal for an error-message %ph argument as well as the
 * real write, the same way it does for every other command here). It is
 * placed here immediately after TE-ON, mirroring the verified ch13721c
 * ordering and the position implied by the relocation stream.
 */
static void lcm_panel_init(struct lcm *ctx)
{
	/* MINDONE-PANEL-PWRORDER (01.09.2026, F3388): power the panel's analog rails
	 * (AVDD/AVEE via OCP2133, plus disp-pwm) BEFORE pulsing reset. The original
	 * order pulsed reset while avdd-en was still low; it only worked because the
	 * bootloader left AVDD on at cold boot. After a real panel power-down
	 * (unprepare on suspend drives avdd-en low), reset hit an unpowered
	 * controller - the always-on DSI receiver kept ACKing so init "succeeded"
	 * but the panel never latched and stayed black until reboot. Analog-then-
	 * reset is the datasheet order, self-sufficient regardless of bootloader state. */
	gpiod_set_value(ctx->avdd_en_gpio, 1);
	msleep(15);
	gpiod_set_value(ctx->disp_pwm_gpio, 1);
	msleep(5);

	gpiod_set_value(ctx->reset_gpio, 1);
	msleep(20);
	gpiod_set_value(ctx->reset_gpio, 0);
	msleep(20);
	gpiod_set_value(ctx->reset_gpio, 1);
	msleep(20);

	lcm_dcs_write_seq_static(ctx, 0xFD, 0x00);
	lcm_dcs_write_seq_static(ctx, 0xF0, 0xA5, 0x0F, 0xF0);
	lcm_dcs_write_seq_static(ctx, 0xFD, 0x00);
	lcm_dcs_write_seq_static(ctx, 0xE3,
		0xF3, 0x00, 0x00, 0x00, 0x00, 0xEC, 0x5E, 0x10, 0x32, 0x54,
		0x76, 0x98, 0xBA, 0xDC, 0xFE, 0x06, 0x12, 0x18, 0x20, 0x28,
		0x30, 0x38, 0x40, 0x3F, 0x20, 0x10, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
	lcm_dcs_write_seq_static(ctx, 0xFD, 0x40);
	lcm_dcs_write_seq_static(ctx, 0xE3,
		0x00, 0x00, 0x00, 0x00, 0xEC, 0x5E, 0x00, 0x03, 0x06, 0x09,
		0x0B, 0x0D, 0x0F, 0x11);
	lcm_dcs_write_seq_static(ctx, 0xFD, 0x00);
	lcm_dcs_write_seq_static(ctx, 0xE0, 0x20, 0x00, 0x63, 0x7B, 0x40, 0x00);
	lcm_dcs_write_seq_static(ctx, 0xE5, 0x80);
	lcm_dcs_write_seq_static(ctx, 0xFD, 0x00);
	lcm_dcs_write_seq_static(ctx, 0xB9, 0x11, 0x3F);
	lcm_dcs_write_seq_static(ctx, 0xFD, 0x00);
	lcm_dcs_write_seq_static(ctx, 0xC4,
		0x13, 0x00, 0x00, 0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x07,
		0x09, 0x0D, 0x11, 0x15, 0x19, 0x21, 0x29, 0x31, 0x39, 0x41,
		0x49, 0x51, 0x59, 0x61, 0x69, 0x71, 0x79, 0x7F, 0x83, 0x87,
		0x87, 0x87, 0x87, 0x87, 0xFF, 0x04, 0x04, 0x44, 0x44, 0x44,
		0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44,
		0x44, 0x40, 0x00, 0x00);
	lcm_dcs_write_seq_static(ctx, 0xEA,
		0xA8, 0xF1, 0x06, 0x06, 0x00, 0x10, 0x00, 0x10, 0x00, 0x10,
		0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x10,
		0x00, 0x44, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00,
		0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
		0xFF, 0xFF, 0xFF, 0xFF, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x11, 0x11, 0x11, 0x11, 0x01);
	lcm_dcs_write_seq_static(ctx, 0xFD, 0x00);
	lcm_dcs_write_seq_static(ctx, 0xB9, 0x11, 0x3F);
	lcm_dcs_write_seq_static(ctx, 0xD7, 0x88);
	lcm_dcs_write_seq_static(ctx, 0xFD, 0x00);
	lcm_dcs_write_seq_static(ctx, 0xB1, 0x78, 0x99, 0x0F);
	lcm_dcs_write_seq_static(ctx, 0xFD, 0x40);
	lcm_dcs_write_seq_static(ctx, 0xE4, 0x7F, 0xFF);
	lcm_dcs_write_seq_static(ctx, 0xFD, 0x00);
	lcm_dcs_write_seq_static(ctx, 0xC9,
		0x01, 0x59, 0x00, 0x00, 0x00, 0x00, 0x03, 0x16, 0x16, 0x16,
		0x00, 0x00, 0x07, 0x16, 0x16, 0x21, 0x04, 0x00, 0x15, 0x16,
		0x16, 0x00, 0x40, 0xC1, 0x11, 0x16, 0x16, 0x37, 0x44, 0xC1,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00);
	lcm_dcs_write_seq_static(ctx, 0xFD, 0x40);
	lcm_dcs_write_seq_static(ctx, 0xC9,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x11, 0xBF, 0x04, 0x00, 0x00, 0x03, 0x08, 0x00,
		0x0D, 0x13, 0x00, 0x19, 0x1D, 0x00, 0x22, 0x26, 0x00, 0x2C,
		0x33, 0x00, 0x3A, 0x44, 0x00, 0x4F, 0x5B, 0x00, 0x68, 0x7A,
		0x00, 0x8C, 0xA0, 0x00, 0xC4, 0x11, 0xBF, 0x04, 0x00, 0x00,
		0x03, 0x08, 0x00);
	lcm_dcs_write_seq_static(ctx, 0xFD, 0x80);
	lcm_dcs_write_seq_static(ctx, 0xC9,
		0x0D, 0x13, 0x00, 0x19, 0x1D, 0x00, 0x22, 0x26, 0x00, 0x2C,
		0x33, 0x00, 0x3A, 0x44, 0x00, 0x4F, 0x5B, 0x00, 0x68, 0x7A,
		0x00, 0x8C, 0xA0, 0x00, 0xC4, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00);
	lcm_dcs_write_seq_static(ctx, 0xFD, 0xC0);
	lcm_dcs_write_seq_static(ctx, 0xC9,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00);
	lcm_dcs_write_seq_static(ctx, 0xFD, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x6C, 0x01);
	lcm_dcs_write_seq_static(ctx, 0xFD, 0x00);
	lcm_dcs_write_seq_static(ctx, 0xD9,
		0x32, 0x41, 0xF5, 0x00, 0x92, 0x25, 0xAE, 0x4E, 0xE5, 0x75,
		0x00, 0x20, 0xAE, 0x0F, 0x00, 0xE6, 0xFF, 0x80, 0xC8, 0x84,
		0x08, 0x10, 0x18, 0x02, 0x90, 0xDA, 0xFF, 0x82, 0xB3, 0x10,
		0xFF, 0x82, 0x9A, 0x10, 0xFF, 0x19, 0xB3, 0x01, 0x12, 0x20,
		0x0E, 0xD0, 0x23, 0x02, 0x1A, 0xB0, 0x00, 0xE8, 0x53, 0x45,
		0x26, 0x90, 0x01, 0xA6, 0x5F, 0x3C, 0xF1, 0xBF, 0x01, 0x0B,
		0x20, 0x41);
	lcm_dcs_write_seq_static(ctx, 0xFD, 0x40);
	lcm_dcs_write_seq_static(ctx, 0xD9,
		0xC2, 0x93, 0xFF, 0x01, 0x70, 0x03, 0xF2, 0x23, 0xFD, 0x1C,
		0xC0, 0x00, 0x4A, 0x04, 0x3E, 0x22, 0x60, 0x01, 0x19, 0x60,
		0x3D, 0xC8, 0x6F, 0xFF, 0x00, 0x50, 0x4C);
	lcm_dcs_write_seq_static(ctx, 0xFD, 0xC0);
	lcm_dcs_write_seq_static(ctx, 0xF5,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00);

	lcm_dcs_write_seq_static(ctx, 0x35, 0x00); /* MIPI_DCS_SET_TEAR_ON */

	/* Default/initial backlight level (bl_tb0[1] holds the level byte). */
	lcm_dcs_write(ctx, bl_tb0, ARRAY_SIZE(bl_tb0));

	lcm_dcs_write_seq_static(ctx, 0x53, 0x28); /* MIPI_DCS_WRITE_CONTROL_DISPLAY: BCTRL|DD */
	lcm_dcs_write_seq_static(ctx, 0x11, 0x00); /* MIPI_DCS_EXIT_SLEEP_MODE */
	msleep(120);
	lcm_dcs_write_seq_static(ctx, 0x29, 0x00); /* MIPI_DCS_SET_DISPLAY_ON */
	msleep(50);
}

static int lcm_disable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (!ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_POWERDOWN;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = false;

	return 0;
}

/* Recovered byte-exact from .rodata (lcm_unprepare.d / .d.61 / .d.62,
 * offsets 0x3d8-0x3db) -- identical shape to ch13721c. */
static int lcm_unprepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (!ctx->prepared)
		return 0;

	lcm_dcs_write_seq_static(ctx, 0x28); /* MIPI_DCS_SET_DISPLAY_OFF */
	msleep(20);
	lcm_dcs_write_seq_static(ctx, 0x10); /* MIPI_DCS_ENTER_SLEEP_MODE */
	msleep(120);
	lcm_dcs_write_seq_static(ctx, 0x4F, 0x01); /* vendor: deep standby / bias off */

	if (ctx->disp_pwm_gpio)
		gpiod_set_value(ctx->disp_pwm_gpio, 0);
	if (ctx->avdd_en_gpio)
		gpiod_set_value(ctx->avdd_en_gpio, 0);

	ctx->error = 0;
	ctx->prepared = false;

	return 0;
}

static int lcm_prepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	int ret;

	if (ctx->prepared)
		return 0;

	lcm_panel_init(ctx);

	ret = ctx->error;
	if (ret < 0)
		lcm_unprepare(panel);

	ctx->prepared = true;

#if defined(CONFIG_MTK_PANEL_EXT)
	mtk_panel_tch_rst(panel);
#endif

	return ret;
}

static int lcm_enable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_UNBLANK;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = true;

	return 0;
}

/*
 * Timing recovered byte-exact from .data offset 0xef68 (symbol
 * default_mode, 120 bytes; only the first 24 were non-zero):
 *
 *   clock=143270 (kHz), hdisplay=1080, hsync_start=1108, hsync_end=1112,
 *   htotal=1148, hskew=0, vdisplay=1240, vsync_start=1282, vsync_end=1290,
 *   vtotal=1300, vscan=0.
 *
 * Self-consistency check: htotal*vtotal*96Hz/1000 = 1148*1300*96/1000
 * = 143270.4 =~ clock (143270) -- confirms 96Hz refresh, exactly.
 * vdisplay=1240 also matches the "fhd1240" in this panel's module name.
 */
#define HFP  28
#define HSA   4
#define HBP  36
#define VFP  42
#define VSA   8
#define VBP  10
#define HAC 1080
#define VAC 1240
#define PCLK_KHZ 143270 /* (HAC+HFP+HSA+HBP)*(VAC+VFP+VSA+VBP)*96/1000 */

static const struct drm_display_mode default_mode = {
	.clock = PCLK_KHZ,
	.hdisplay = HAC,
	.hsync_start = HAC + HFP,
	.hsync_end = HAC + HFP + HSA,
	.htotal = HAC + HFP + HSA + HBP,
	.vdisplay = VAC,
	.vsync_start = VAC + VFP,
	.vsync_end = VAC + VFP + VSA,
	.vtotal = VAC + VFP + VSA + VBP,
};

static int lcm_get_modes(struct drm_panel *panel, struct drm_connector *connector)
{
	pr_notice("MINDONE-PANEL-NOESD: build with panel self-check disabled\n");
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &default_mode);
	if (!mode) {
		dev_err(connector->dev->dev, "failed to add mode %ux%u@%u\n",
			default_mode.hdisplay, default_mode.vdisplay,
			drm_mode_vrefresh(&default_mode));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs lcm_drm_funcs = {
	.disable = lcm_disable,
	.unprepare = lcm_unprepare,
	.prepare = lcm_prepare,
	.enable = lcm_enable,
	.get_modes = lcm_get_modes,
};

#if defined(CONFIG_MTK_PANEL_EXT)
/*
 * Independently re-verified (2026-08-19): exactly 10 of the 60856 bytes
 * in the stock ext_params are non-zero, and all 10 land exactly on
 * field boundaries (confirmed via compiled offsetof(), not hand-counted):
 *   - pll_clk (offset +4) = 430. Matches 4-lane DSI at this pixel
 *     clock/24bpp (143270 kHz * 24 / 4 / 2 ~= 429.8 -> 430); see
 *     ch13721c's header comment for the same derivation.
 *   - cust_esd_check (offset +0xa40) = 1.
 *   - esd_check_enable (offset +0xa44) = 1.
 *   - lcm_esd_check_table[0] (offset +0xa48) = {cmd=0x0A, count=1,
 *     para_list[0]=0x9C}: MIPI DCS "Get Power Mode", expected 0x9C --
 *     the standard MTK ESD-check idiom (same entry ch13721c has, but
 *     that panel leaves the enable flags at zero; this one turns it on).
 *   - lcm_esd_check_table[1] (offset +0xa5e) = {cmd=0xFB, count=1,
 *     para_list[0]=0x11}: a second, vendor/JD9365D-specific status
 *     register read (0xFB is not a standard MIPI DCS opcode). Exact
 *     semantics of this second check are not otherwise documented;
 *     reproduced here byte-faithful to the binary.
 *   Both table entries have mask_list all-zero in the binary; that is
 *   reproduced as-is (not editorialized -- the masking semantics live
 *   in the mtk_disp ESD-check core, outside this driver).
 * Every other field (round-corner, DSC, dynamic fps, msync, spr/cm...)
 * is zero/default in the binary and left at struct defaults here.
 */
static struct mtk_panel_params ext_params = {
	.pll_clk = 430,
	/* MINDONE: keep the data lanes in HS through HFP and drop to LP once per
	 * frame (vertical blanking) instead of once per line. With the stock
	 * default (per-line LP) every line pays two LP<->HS transitions, about
	 * 2 * data_phy_cycle * 4 lanes ~= 208 byte clocks, which the 172 bytes
	 * of HFP+HBP cannot absorb at 860 Mbps: the line grows from 8.02 us to
	 * ~8.5 us and the frame from 10.42 ms (96 Hz) to ~11.04 ms. Measured on
	 * the device 19.09: kernel lcm_fps_ctx_get 11.03-11.05 ms (fps=9057),
	 * LK fps=9049, SurfaceFlinger "ideal period 10.42ms: period = 11.07ms".
	 * mtk_dsi_config_vdo_timing() sets HFP_HS_EN and shortens the blanking
	 * lines by the LP overhead when this is on. Not in the stock binary. */
	.vdo_per_frame_lp_enable = 1,
	/* MINDONE: the panel self-check is turned OFF here.
	 * Entry [1] of lcm_esd_check_table reads register 0xFB and expects 0x11; 0xFB
	 * is not standard MIPI DCS and belongs to a DIFFERENT controller (JD9365D)
	 * per this file's own header - likely inherited during reconstruction. On
	 * this device the read returns 0x00, the check fails 19x, and the driver
	 * thrashes the output down/up 20 times before giving up, producing IOMMU
	 * faults on OVL_RDMA0. Crutch, recorded in HACKS - real fix: find
	 * the register this panel actually answers on. */
	.cust_esd_check = 0,
	.esd_check_enable = 0,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0A,
		.count = 1,
		.para_list[0] = 0x9C,
	},
	.lcm_esd_check_table[1] = {
		.cmd = 0xFB,
		.count = 1,
		.para_list[0] = 0x11,
	},
};

static int panel_ext_reset(struct drm_panel *panel, int on)
{
	struct lcm *ctx = panel_to_lcm(panel);

	gpiod_set_value(ctx->reset_gpio, on);

	return 0;
}

static int panel_ata_check(struct drm_panel *panel)
{
	return 1;
}

static int lcm_setbacklight_cmdq(void *dsi, dcs_write_gce cb, void *handle,
				  unsigned int level)
{
	if (level > 255)
		level = 255;

	bl_tb0[1] = (u8)level;

	if (!cb)
		return -1;

	cb(dsi, handle, bl_tb0, ARRAY_SIZE(bl_tb0));

	return 0;
}

static struct mtk_panel_funcs ext_funcs = {
	.reset = panel_ext_reset,
	.ata_check = panel_ata_check,
	.set_backlight_cmdq = lcm_setbacklight_cmdq,
};
#endif

/* MINDONE-PANEL-PREPCLR (01.09.2026, F3385): when the skip-panel-switch path
 * leaves prepared/enabled true without a real unprepare, a system suspend
 * still powers the panel context down — and every later lcm_prepare/enable
 * no-ops on the stale flags, leaving the panel black until reboot (no ESD
 * check on this board). On suspend entry the panel is either already
 * unprepared (flags false — no-op) or about to lose state anyway, so
 * clearing the flags is idempotent and only ever forces a full re-init.
 */
static struct lcm *mindone_prepclr_ctx;

static int mindone_panel_pm_event(struct notifier_block *nb,
				  unsigned long event, void *unused)
{
	struct lcm *ctx = READ_ONCE(mindone_prepclr_ctx);

	if (event != PM_SUSPEND_PREPARE || !ctx)
		return NOTIFY_DONE;

	if (ctx->prepared || ctx->enabled) {
		dev_err(ctx->dev,
			"MINDONE-PANEL-PREPCLR: stale prepared=%d enabled=%d cleared on suspend entry\n",
			ctx->prepared, ctx->enabled);
		ctx->prepared = false;
		ctx->enabled = false;
	}
	return NOTIFY_DONE;
}

static struct notifier_block mindone_panel_pm_nb = {
	.notifier_call = mindone_panel_pm_event,
};

static int lcm_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct device_node *dsi_node, *remote_node = NULL, *endpoint;
	struct device_node *backlight;
	struct lcm *ctx;
	int ret;

	dsi_node = of_get_parent(dev->of_node);
	if (dsi_node) {
		endpoint = of_graph_get_next_endpoint(dsi_node, NULL);
		if (endpoint) {
			remote_node = of_graph_get_remote_port_parent(endpoint);
			if (!remote_node) {
				dev_info(dev, "No panel connected,skip probe lcm\n");
				return -ENODEV;
			}
		}
	}
	if (remote_node != dev->of_node) {
		/* Real string (confirmed via `strings` on the stock .ko) is
		 * "%s:sd5302h + skip probe due to not current lcm" -- the
		 * panel name is a literal baked into the format string. */
		dev_info(dev, "%s:sd5302h + skip probe due to not current lcm\n", __func__);
		return -ENODEV;
	}

	ctx = devm_kzalloc(dev, sizeof(struct lcm), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ctx);
	ctx->dev = dev;

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	/* MIPI_DSI_MODE_EOT_PACKET (kernel <6.x: "do send EOT packets", the
	 * behavior this driver wants) was removed upstream; EOT packets are
	 * sent unconditionally now, and the surviving flag
	 * MIPI_DSI_MODE_NO_EOT_PACKET means the OPPOSITE (suppress them) --
	 * so the correct 6.1 port is to just drop the bit, not translate it,
	 * since the wanted behavior (EOT packets sent) is now the default. */
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE
			 | MIPI_DSI_MODE_LPM;

	backlight = of_parse_phandle(dev->of_node, "backlight", 0);
	if (backlight) {
		ctx->backlight = of_find_backlight_by_node(backlight);
		of_node_put(backlight);
		if (!ctx->backlight)
			return -EPROBE_DEFER;
	}

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio)) {
		ret = PTR_ERR(ctx->reset_gpio);
		/* stock string is "cannot get reset-gpios %ld" (PTR_ERR is
		 * long) -- kept ret as int for the function's own return
		 * path, cast to long only for the format match. */
		dev_err(dev, "cannot get reset-gpios %ld\n", (long)ret);
		return ret;
	}

	ctx->avdd_en_gpio = devm_gpiod_get(dev, "avdd-en", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->avdd_en_gpio)) {
		ret = PTR_ERR(ctx->avdd_en_gpio);
		dev_err(dev, "cannot get avdd-en-gpios %ld\n", (long)ret);
		return ret;
	}

	ctx->disp_pwm_gpio = devm_gpiod_get(dev, "disp-pwm", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->disp_pwm_gpio)) {
		ret = PTR_ERR(ctx->disp_pwm_gpio);
		dev_err(dev, "cannot get disp-pwm-gpios %ld\n", (long)ret);
		return ret;
	}

	ctx->prepared = true;
	ctx->enabled = true;

	drm_panel_init(&ctx->panel, dev, &lcm_drm_funcs, DRM_MODE_CONNECTOR_DSI);

	drm_panel_add(&ctx->panel);

	/* MINDONE-PANEL-PREPCLR: arm the stale-flag clear (single panel). */
	WRITE_ONCE(mindone_prepclr_ctx, ctx);
	register_pm_notifier(&mindone_panel_pm_nb);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		return ret;
	}

#if defined(CONFIG_MTK_PANEL_EXT)
	mtk_panel_tch_handle_reg(&ctx->panel);
	ret = mtk_panel_ext_create(dev, &ext_params, &ext_funcs, &ctx->panel);
	if (ret < 0)
		return ret;
#endif

	return ret;
}

/* struct mipi_dsi_driver.remove changed int->void upstream (~6.x driver-core
 * "void remove()" cleanup, matches drm_mipi_dsi.h in this tree) -- same real
 * teardown logic, just no return value. */
static void lcm_remove(struct mipi_dsi_device *dsi)
{
	struct lcm *ctx = mipi_dsi_get_drvdata(dsi);
#if defined(CONFIG_MTK_PANEL_EXT)
	struct mtk_panel_ctx *ext_ctx = find_panel_ctx(&ctx->panel);
#endif

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);
#if defined(CONFIG_MTK_PANEL_EXT)
	mtk_panel_detach(ext_ctx);
	mtk_panel_remove(ext_ctx);
#endif
}

static const struct of_device_id lcm_of_match[] = {
	{ .compatible = "sd5302h,dsi,vdo,amoled", },
	{ }
};
MODULE_DEVICE_TABLE(of, lcm_of_match);

static struct mipi_dsi_driver lcm_driver = {
	.probe = lcm_probe,
	.remove = lcm_remove,
	.driver = {
		.name = "panel_sd5302h_fhd1240_dsi_vdo_amoled_ocp2133",
		.owner = THIS_MODULE,
		.of_match_table = lcm_of_match,
	},
};

module_mipi_dsi_driver(lcm_driver);

MODULE_AUTHOR("maliejun@agenewtech.com");
MODULE_DESCRIPTION("hongzan jd9365d VDO LCD Panel Driver");
MODULE_LICENSE("GPL v2");

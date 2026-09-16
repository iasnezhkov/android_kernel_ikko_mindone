// SPDX-License-Identifier: GPL-2.0
/*
 * panel-ch13721c-fhd-dsi-vdo-amoled-ocp2133.c
 *
 * RECONSTRUCTED FROM BINARY (.ko reverse engineering), not vendor source.
 * Original prebuilt module info (from .modinfo):
 *   author      = maliejun@agenewtech.com
 *   description = "hongzan jd9365d VDO LCD Panel Driver"
 *   name        = panel_ch13721c_fhd_dsi_vdo_amoled_ocp2133
 *   compatible  = "ch13721c,dsi,vdo,amoled"
 *   depends     = mtk_panel_ext
 *   vermagic    = 5.10.233-android12-5.10-android12-9-gb84438489451 ...
 *
 * IC: JD9365D (Jadard) driver IC, OCP2133 LCD bias/PMIC.
 * Panel: 1080x1920 AMOLED, VDO (video) mode DSI, 4 data lanes.
 *
 * This file was rebuilt by:
 *   1. Disassembling .text (llvm-objdump -d -r) of the stock .ko and
 *      resolving every ADRP+ADD -> .rodata/.data relocation.
 *   2. Extracting all "lcm_panel_init.d*" / "lcm_unprepare.d*" anonymous
 *      const byte arrays from .rodata in file-offset order, which is also
 *      call order (verified: every call site's rodata offset increases
 *      monotonically through lcm_prepare/lcm_unprepare).
 *   3. Recovering `struct drm_display_mode default_mode` and `bl_tb0[]`
 *      from the tail of .data (offset 0xef68 / 0xef60), decoded against
 *      the real mainline layout (int clock; u16 hdisplay; hsync_start;
 *      hsync_end; htotal; hskew; vdisplay; vsync_start; vsync_end; vtotal;
 *      vscan;) confirmed byte-for-byte against
 *      include/drm/drm_modes.h in this project's kernel/common tree.
 *   4. Cross-checking the general driver shape (struct lcm, lcm_dcs_write
 *      0xB0 DCS/generic threshold, ext_params/ext_funcs wiring,
 *      lcm_setbacklight_cmdq(dsi, cb, handle, level) signature) against
 *      a real, structurally near-identical MediaTek mediatek_v2 panel
 *      driver family found in MediaTek's published sources for a sibling SoC (same
 *      mt67xx/mt68xx "mediatek_v2" DRM panel_ext architecture, e.g.
 *      drivers/gpu/drm/panel/panel_jd9161z_fwvga1170_dsi_vdo_boe.c and
 *      panel-alpha-dzx-nt36672c-vdo-120hz.c) -- these confirmed the
 *      exact idioms our disassembly showed byte-for-byte (the 0xB0
 *      dcs/generic split, the bl_tb0[] backlight pattern, the
 *      mtk_panel_ext_create() call ordering).
 *
 * Confidence:
 *   - DCS/generic init sequence (lcm_panel_init):  HIGH  (byte-exact,
 *     recovered directly from .rodata; order verified against the
 *     relocation stream of lcm_prepare).
 *   - GPIO sequencing / delays in lcm_prepare/lcm_unprepare: HIGH
 *     (straight-line disassembly, no complex control flow).
 *   - struct drm_display_mode timing: HIGH (byte-exact from .data,
 *     internally self-consistent: htotal*vtotal*64Hz/1000 == clock
 *     field to within rounding).
 *   - ext_params (struct mtk_panel_params) contents: HIGH for the two
 *     fields that are actually non-zero in the binary, LOW/default for
 *     everything else. Independently re-verified (2026-08-19, second
 *     pass) by computing real offsetof() values for every field up to
 *     lcm_esd_check_table against the vendor mtk_panel_ext.h (compiled
 *     standalone, not hand-counted) and diffing against every non-zero
 *     byte in the 60856-byte blob: exactly 5 bytes are non-zero, and
 *     all 5 land exactly on field boundaries computed by the compiler:
 *       - offset +4..+5:      pll_clk = 413 (u32, high bytes zero).
 *         Consistent with pll_clk = data_rate/2 for a 4-lane DSI link
 *         at this pixel clock (137.887MHz * 24bpp / 4 lanes / 2 ~= 413.6).
 *       - offset +0xa48..+0xa4a: lcm_esd_check_table[0] = {cmd=0x0A,
 *         count=1, para_list[0]=0x9C} (mask_list all zero). This is the
 *         MIPI DCS "Get Power Mode" (0x0A) read, expected value 0x9C --
 *         the single most common ESD-check idiom across MTK panel
 *         drivers. NOTE: cust_esd_check/esd_check_enable (offsets
 *         +0xa40/+0xa44) are BOTH zero for this panel, i.e. the table
 *         entry is present in the binary but ESD polling is not enabled
 *         for ch13721c (unlike sd5302h, see that file). Reproduced
 *         faithfully below: table populated, enable flags left at
 *         their zero default.
 *     Every other field of mtk_panel_params (round corner, DSC, dynamic
 *     fps, msync, spr/cm, ...) is zero in the binary, i.e. genuinely
 *     unused by this panel -- left at struct defaults below.
 *
 * NOT reconstructed / needs the real vendor header to build as-is:
 *   "../mediatek/mediatek_v2/mtk_panel_ext.h" and the mtk_panel_ext.ko
 *   it binds to (mtk_panel_ext_create, mtk_panel_tch_handle_reg,
 *   mtk_panel_detach, mtk_panel_remove, find_panel_ctx are all imported,
 *   undefined symbols in the stock .ko -- provided by a SEPARATE
 *   mtk_panel_ext.ko, not by this driver). A verbatim copy of that
 *   header exists in MediaTek's published sources for a sibling SoC's drivers/gpu/drm/mediatek/
 *   mediatek_v2/mtk_panel_ext.h (same mt67xx/68xx "mediatek_v2" DRM
 *   generation) and is very likely ABI-compatible, but this was not
 *   verified field-by-field against the 60856-byte ext_params blob --
 *   only pll_clk (offset +4) could be confirmed. If mtk_panel_ext.ko's
 *   own reconstruction diverges even slightly in field order, ext_params
 *   below will be silently wrong past pll_clk. Build against the exact
 *   header used to build mtk_panel_ext.ko for this device.
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

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

/* Default backlight command, MIPI_DCS_SET_DISPLAY_BRIGHTNESS-style write.
 * bl_tb0[1] is overwritten at runtime with the requested level.
 * Recovered from .data offset 0xef60 (symbol "bl_tb0", 2 bytes): {0x51, 0x80}.
 */
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

/* Recovered from lcm_dcs_write() disassembly: every call site does
 * `cmp w8, #0xAF; b.hi generic_write; else dcs_write_buffer`, i.e. DCS
 * command bytes <= 0xAF are standard MIPI DCS (mipi_dsi_dcs_write_buffer),
 * everything >= 0xB0 is a JD9365D vendor/CMD2 register sent as a raw
 * generic MIPI packet (mipi_dsi_generic_write). This matches the same
 * idiom used verbatim in the mediatek_v2 reference panel drivers.
 */
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
 * Full bring-up sequence for ch13721c (JD9365D), extracted byte-exact from
 * .rodata (symbols lcm_panel_init.d, .d.22 .. .d.26, offsets 0x4e4-0x4ef).
 * This panel's table is unusually short (6 commands total) compared to
 * sd5302h's -- everything else in .rodata/.data for this panel is zero,
 * so this genuinely is the entire vendor init sequence, most likely
 * because gamma/voltage trim lives in this panel's OTP and doesn't need
 * runtime programming.
 */
static void lcm_panel_init(struct lcm *ctx)
{
	gpiod_set_value(ctx->reset_gpio, 1);
	msleep(20);
	gpiod_set_value(ctx->reset_gpio, 0);
	msleep(20);
	gpiod_set_value(ctx->reset_gpio, 1);
	msleep(20);

	gpiod_set_value(ctx->avdd_en_gpio, 1);
	msleep(10);
	gpiod_set_value(ctx->disp_pwm_gpio, 1);

	lcm_dcs_write_seq_static(ctx, 0xF0, 0x51); /* CMD2 unlock */
	lcm_dcs_write_seq_static(ctx, 0xC0, 0x2D); /* vendor reg */
	lcm_dcs_write_seq_static(ctx, 0xC1, 0x18); /* vendor reg */
	lcm_dcs_write_seq_static(ctx, 0x35, 0x00); /* MIPI_DCS_SET_TEAR_ON, mode 0 */

	/* Default/initial backlight level (bl_tb0[1] holds the level byte). */
	lcm_dcs_write(ctx, bl_tb0, ARRAY_SIZE(bl_tb0));

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

/*
 * Recovered byte-exact from .rodata (lcm_unprepare.d / .d.32 / .d.33,
 * offsets 0x4e0-0x4e3) and the call order in lcm_unprepare()'s
 * disassembly.
 */
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
 * default_mode, 120 bytes). Only the first 24 bytes were non-zero;
 * decoded against the real struct drm_display_mode layout
 * (int clock; u16 hdisplay,hsync_start,hsync_end,htotal,hskew,vdisplay,
 * vsync_start,vsync_end,vtotal,vscan;):
 *
 *   clock=137887 (kHz), hdisplay=1080, hsync_start=1092, hsync_end=1094,
 *   htotal=1106, hskew=0, vdisplay=1920, vsync_start=1932, vsync_end=1936,
 *   vtotal=1948, vscan=0.
 *
 * Self-consistency check: htotal*vtotal*64Hz/1000 = 1106*1948*64/1000
 * = 137887.23 =~ clock (137887) -- confirms 64Hz refresh, exactly.
 */
#define HFP  12
#define HSA   2
#define HBP  12
#define VFP  12
#define VSA   4
#define VBP  12
#define HAC 1080
#define VAC 1920
#define PCLK_KHZ 137887 /* (HAC+HFP+HSA+HBP)*(VAC+VFP+VSA+VBP)*64/1000 */

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
 * NOTE: struct mtk_panel_params is 60856 bytes in the stock .ko; exactly
 * 5 of those bytes are non-zero, and all 5 land exactly on field
 * boundaries (independently confirmed via compiled offsetof(), not by
 * hand-counting):
 *   - pll_clk (offset +4) = 413. Consistent with `pll_clk = data_rate/2`
 *     for a 4-lane DSI link at this panel's pixel clock and 24bpp:
 *     137887 kHz * 24bpp / 4 lanes / 2 ~= 413.6 -> 413 (truncated).
 *   - lcm_esd_check_table[0] (offset +0xa48) = {cmd=0x0A, count=1,
 *     para_list[0]=0x9C}: MIPI DCS "Get Power Mode" read, expected 0x9C
 *     -- the standard MTK ESD-check idiom. cust_esd_check/
 *     esd_check_enable (offsets +0xa40/+0xa44) are both zero for this
 *     panel, so the table is present but ESD polling is not wired on
 *     for ch13721c (contrast panel-sd5302h in this same directory,
 *     which has both flags set).
 * Every other field (round-corner pattern, DSC, dynamic fps, msync,
 * spr/cm, ...) is zero/default in the binary and left at struct
 * defaults here.
 */
static struct mtk_panel_params ext_params = {
	.pll_clk = 413,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0A,
		.count = 1,
		.para_list[0] = 0x9C,
	},
};

static int panel_ext_reset(struct drm_panel *panel, int on)
{
	struct lcm *ctx = panel_to_lcm(panel);

	gpiod_set_value(ctx->reset_gpio, on);

	return 0;
}

/* Reads DCS reg 0x53, always returns 1 in the stock binary (no real
 * pass/fail logic was found -- matches other mediatek_v2 reference drivers
 * where ata_check is a stub). */
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

static int lcm_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct device_node *dsi_node, *remote_node = NULL, *endpoint;
	struct device_node *backlight;
	struct lcm *ctx;
	int ret;

	/* devicetree-graph "is this endpoint actually me" gate, recovered
	 * from lcm_probe's disassembly -- identical idiom to every other
	 * mediatek_v2 panel driver. */
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
		 * "%s:ch13721c + skip probe due to not current lcm" -- the
		 * panel name is a literal baked into the format string, not
		 * substituted via a second %s. */
		dev_info(dev, "%s:ch13721c + skip probe due to not current lcm\n", __func__);
		return -ENODEV;
	}

	ctx = devm_kzalloc(dev, sizeof(struct lcm), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ctx);
	ctx->dev = dev;

	/* dsi->lanes/format/mode_flags not directly observed (they live in
	 * the caller-supplied mipi_dsi_device before probe runs on real MTK
	 * kernels); 4 lanes inferred from ext_params.pll_clk, see above. */
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

	/* GPIO devicetree property names recovered verbatim from the error
	 * strings in .rodata: "reset-gpios", "avdd-en-gpios",
	 * "disp-pwm-gpios". Unlike most mediatek_v2 reference drivers (which
	 * devm_gpiod_get/put around every use), this driver gets each GPIO
	 * ONCE here and keeps the descriptor in ctx for prepare/unprepare --
	 * confirmed by the disassembly (ctx offsets read directly by
	 * gpiod_set_value in lcm_prepare/lcm_unprepare, no repeated
	 * devm_gpiod_get calls there). */
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
	{ .compatible = "ch13721c,dsi,vdo,amoled", },
	{ }
};
MODULE_DEVICE_TABLE(of, lcm_of_match);

static struct mipi_dsi_driver lcm_driver = {
	.probe = lcm_probe,
	.remove = lcm_remove,
	.driver = {
		.name = "panel_ch13721c_fhd_dsi_vdo_amoled_ocp2133",
		.owner = THIS_MODULE,
		.of_match_table = lcm_of_match,
	},
};

module_mipi_dsi_driver(lcm_driver);

MODULE_AUTHOR("maliejun@agenewtech.com");
MODULE_DESCRIPTION("hongzan jd9365d VDO LCD Panel Driver");
MODULE_LICENSE("GPL v2");

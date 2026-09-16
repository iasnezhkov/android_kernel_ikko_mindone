// SPDX-License-Identifier: GPL-2.0
/*
 * sileadfp.c — Gigadevice/Silead Fingerprint driver for GSL6xxx/GSL7xxx/GSL8xxx.
 * Reconstructed from disassembly of the prebuilt sileadfp.ko (vermagic
 * 5.10.233-android12-5.10-android12-9-gb84438489451; shipped-binary author
 * Bill Yu <billyu@silead.com>) via llvm-readelf/-nm/-objdump/-objcopy with
 * relocations resolved. Full RE method, fidelity notes, and the MINDONE
 * insert rationale: MINDONE-MODULES-NOTES-0901, re510-modules.
 */

#include <linux/cdev.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeup.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include <linux/suspend.h>
#include "mtk_disp_notify.h"
#include "tee_fp.h"

#define SILFP_DEV_NAME		"sil"
#define SILFP_CLASS_NAME	"silead_fp"
#define SILFP_INPUT_NAME	"fp-keys"
#define SILFP_WQ_NAME		"silfp_wq"
#define SILFP_DRV_VERSION	"v0.3.8"

#define SILFP_CHIP_ID_MASK	0x6193U		/* (id >> 16), confirmed literal */

/* ioctl magic confirmed exact ('s' == 0x73) from `and w9,w1,#0xff00;
 * cmp w9,#0x7300` in silfp_ioctl(). Individual command numbers below are
 * the confirmed *operations* (see file header); exact nr assignment is
 * best-effort, not traced bit-for-bit for every slot.
 */
#define SILFP_IOC_MAGIC		's'
#define SILFP_IOC_INIT		_IO(SILFP_IOC_MAGIC, 0)

/* MINDONE-FP-IOC1621 (F3276): HAL expects two ioctls we lacked at init,
 * causing "Could not init silead device (-1008)". Added GET_VERSION
 * (nr=21, returns "v0.3.8") and GET_CONFIG (nr=16, returns the 48-byte
 * SPI config blob) per the vendor .ko's real handlers (offsets/bytes in
 * MINDONE-MODULES-NOTES-0901). nr=17 intentionally left unhandled:
 * the vendor's own jump table has no case for it either. */
#define SILFP_IOC_GET_VERSION	_IOR(SILFP_IOC_MAGIC, 21, char[10])
#define SILFP_IOC_GET_CONFIG	_IOR(SILFP_IOC_MAGIC, 16, char[48])

/* MINDONE-FP-IOC1026 (F3292): two more vendor ioctls from jump-table
 * reverse engineering — nr=10 (HW_RESET, 1-byte arg, default 2) calls
 * silfp_hw_reset(); nr=26 (SET_MODE, 1-byte arg) rejects values > 3, as
 * the vendor handler does. HW_RESET is gated by mindone_fp_hwreset (see
 * F3296 below): GPIO 157's true role on this board is unconfirmed
 * (F3284). Full offsets/disasm: MINDONE-MODULES-NOTES-0901. */
#define SILFP_IOC_HW_RESET	_IOW(SILFP_IOC_MAGIC, 10, char)
#define SILFP_IOC_SET_MODE	_IOWR(SILFP_IOC_MAGIC, 26, char)

/* F3296: enabled by default. Without a hardware reset the sensor never
 * responds (chipid=0,0,0); with it, chipid=6193a012. The earlier worry
 * about GPIO 157 itself was wrong — the real crash was an unchecked
 * pinctrl pointer (MINDONE-FP-PINCHECK). Param kept to allow disabling
 * without a rebuild. */
static bool mindone_fp_hwreset = true;
module_param(mindone_fp_hwreset, bool, 0644);
MODULE_PARM_DESC(mindone_fp_hwreset,
	"1 = ioctl cmd 10 actually pulses the sensor reset (GPIO 157); default 0");

static unsigned char mindone_fp_mode;

/* The vendor driver copies EXACTLY 7 bytes, though the buffer is declared as 10 — matched here. */
static const char mindone_fp_version[7] = "v0.3.8";

/* Byte-for-byte copy of the vendor module's .data+0x188. Not invented: the
 * path and speed are read directly from the dump; the rest is carried over
 * as-is rather than guessed. */
static const unsigned char mindone_fp_config[48] = {
	0x00, 0x08, 0x64, 0x00,  /* field 0: 6555648 */
	0x80, 0x84, 0x1e, 0x00,  /* field 1: 2000000 — SPI speed, Hz */
	'/', 'd', 'e', 'v', '/', 's', 'p', 'i', 'd', 'e', 'v', '1', '.', '0', 0x00, 0x00,
	0x00, 0x00, 0x01, 0x00,  /* field at offset 24: 65536 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
#define SILFP_IOC_EXIT		_IO(SILFP_IOC_MAGIC, 1)
#define SILFP_IOC_RESET		_IO(SILFP_IOC_MAGIC, 2)
#define SILFP_IOC_ENABLE_IRQ	_IO(SILFP_IOC_MAGIC, 3)
#define SILFP_IOC_DISABLE_IRQ	_IO(SILFP_IOC_MAGIC, 4)
#define SILFP_IOC_PINCTRL	_IOW(SILFP_IOC_MAGIC, 5, int)
#define SILFP_IOC_WAIT_IRQ	_IO(SILFP_IOC_MAGIC, 6)
#define SILFP_IOC_KEYEVENT	_IOW(SILFP_IOC_MAGIC, 7, int)

/* MINDONE-SILFP-IOCMAP (F3243, 31.08): the ioctl numbers above (0..7) were
 * reverse-engineered from a different interface version — this device's
 * real HAL never sends them. Reversing the actual vendor sileadfp.ko
 * (5.10.233, vendor_boot-5b-DEFAULT.img) jump tables via relocations gave
 * the real map (F3242): the five below cover 76 of 81 ioctl calls seen
 * during one boot. */
#define SILFP_IOC_ENABLE_IRQ_HAL	_IO(SILFP_IOC_MAGIC, 11)  /* vendor: spin_lock + enable_irq */
#define SILFP_IOC_DISABLE_IRQ_HAL	_IO(SILFP_IOC_MAGIC, 12) /* vendor: spin_lock + disable_irq_nosync */
#define SILFP_IOC_POLL_A		_IO(SILFP_IOC_MAGIC, 23)   /* vendor: returns -ENOENT (no events) */
#define SILFP_IOC_POLL_B		_IO(SILFP_IOC_MAGIC, 24)   /* vendor: returns -ENOENT (no events) */
#define SILFP_IOC_WAKE_HOLD	_IOW(SILFP_IOC_MAGIC, 27, char) /* vendor: pm_wakeup_ws_event(ws, 25000ms) */

/* silfp_hw_reset() pulse selector values, confirmed from disasm
 * (mov w9,#5 default-substitution, and the `tst w19,#0xff; mov w8,#3`
 * pattern for the second stage) — 5 == reset pulse len default in ms.
 */
#define SILFP_RESET_LOW_MS_DEFAULT	5
#define SILFP_RESET_HIGH_MS_DEFAULT	3

struct silfp_event {
	struct list_head list;
	u8 data;
};

struct silead_fp_dev {
	struct cdev cdev;
	dev_t devt;
	struct device *device;
	struct platform_device *pdev;
	struct list_head device_entry;

	struct mutex ops_lock;
	int users;

	/* SPI-over-TEE */
	struct pinctrl *pinctrl;
	struct pinctrl_state *pins_default;
	struct pinctrl_state *pins_rst_high;
	struct pinctrl_state *pins_rst_low;
	struct pinctrl_state *pins_irq_rst_high;
	struct pinctrl_state *pins_irq_rst_low;

	int irq;
	bool irq_enabled;
	bool irq_wake_enabled;
	spinlock_t irq_lock;

	int rst_gpio;

	struct input_dev *input;

	struct work_struct work;
	struct completion irq_completion;
	wait_queue_head_t read_queue;
	spinlock_t event_lock;
	struct list_head event_list;
	bool event_readable;

	struct notifier_block disp_notif;

	struct wakeup_source *ws_irq;
	struct wakeup_source *ws_hal;

	u32 bootmode;
	u8 lasttouchmode;
};

/* Globals, matching the shipped .ko's own top-level symbols. */
static struct silead_fp_dev *g_fp_dev;
static struct class *silfp_class;
static struct workqueue_struct *silfp_wq;
static LIST_HEAD(device_list);
static DEFINE_MUTEX(device_list_lock);
static struct proc_dir_entry *silfp_proc_entry;

/* Runtime debug-print gate: prints are skipped once sil_debug_level >= 2
 * (confirmed: every debug printk site does `cmp level,#2; b.hs skip`).
 * Default 3 (from .data), i.e. debug prints suppressed unless lowered
 * via module param.
 */
static int sil_debug_level = 3;
module_param(sil_debug_level, int, 0644);

static int rst_gpio = -1;
module_param(rst_gpio, int, 0644);

/* vendor_name — zero-initialized 32-byte .bss buffer in the shipped
 * .ko, never seen written from any traced call site in this pass; kept
 * for symbol-table fidelity only.
 */
static char __maybe_unused vendor_name[32];

#define SIL_LOG(fmt, ...) \
	do { \
		if (sil_debug_level < 2) \
			pr_notice("4[+silead_fp-] " fmt, ##__VA_ARGS__); \
	} while (0)

/* smt_conf — opaque SPI/TEE session-config blob passed to the trustlet.
 * Bytes copied verbatim from the shipped .ko's .data+0x4 (80 bytes);
 * field meanings not decoded, only reproduced.
 */
static const u8 smt_conf[80] = {
	0x0f, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00,
	0x1e, 0x00, 0x00, 0x00, 0x1e, 0x00, 0x00, 0x00,
	0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
	0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
	0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* TANAME — unidentified 16-byte constant blob, copied verbatim from the
 * shipped .ko's .rodata+0x1498. NOT a TEE session UUID: the real
 * tee_spi_transfer() (drivers/tee/tkcore/core/peridev.c, this tree)
 * hardcodes its own SENSOR_DETECTOR_TA_UUID internally and takes
 * no UUID argument from the caller, so this constant's role in
 * sileadfp itself was not identified; reproduced for fidelity only.
 */
static const u8 TANAME[16] = {
	0x51, 0x1e, 0xad, 0x0d, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* Default fp-keys keymap, overridden from the DT "fp-keys" property in
 * silfp_resource_init(). Raw default content copied from .rodata+0x1400
 * (7 pairs, {index, 0}); the DT override supplies the real Linux
 * keycodes.
 */
static u32 keymap[7][2] = {
	{ 1, 0 }, { 2, 0 }, { 3, 0 }, { 4, 0 }, { 5, 0 }, { 6, 0 }, { 7, 0 },
};

/*
 * tee_spi_transfer() — declared in tee_fp.h (copied verbatim from
 * drivers/tee/tkcore/include/linux/tee_fp.h, this tree; real
 * EXPORT_SYMBOL implementation lives in tkcore's
 * drivers/tee/tkcore/core/peridev.c, invokes the TEE session and does
 * `memcpy(outbuf, inbuf, size)` followed by a TEEC_MEMREF_TEMP_INOUT
 * command, i.e. a synchronous "write inbuf, read result into outbuf"
 * transaction of `size` bytes against a `conf`/`conf_size` session
 * blob). This confirms w1=80 seen at every call site in the
 * disassembly is simply `sizeof(smt_conf)` (conf_size), not a magic ID
 * as first guessed — smt_conf[] is exactly 80 bytes. Module dependency
 * on tkcore matches modinfo depends="mtk_disp_notify,tkcore"; resolved
 * at module-load time via depmod, same as the shipped .ko (this symbol
 * is intentionally left unresolved at link/modpost time here too, per
 * KBUILD_MODPOST_WARN=1 — the trustlet/TA implementation itself is out
 * of scope for a kernel module reconstruction).
 */

/*
 * st_tee_spi_transfer() — thin wrapper, reproduced exactly from
 * disassembly: tee_spi_transfer(&smt_conf, sizeof(smt_conf), a, b, c).
 */
int st_tee_spi_transfer(void *inbuf, void *outbuf, u32 size)
{
	return tee_spi_transfer((void *)smt_conf, sizeof(smt_conf),
				 inbuf, outbuf, size);
}

/*
 * silfp_spi_read_id() — reads the sensor chip ID via the TEE. Builds a
 * short (6-byte) transaction and hands it to tee_spi_transfer() directly
 * (not through the st_ wrapper, confirmed: same &smt_conf/80 args passed
 * inline). Local transaction descriptor layout reproduced to the same
 * size class (6-byte inbuf/outbuf, plus a separate larger
 * list_head-carrying local descriptor that IS list-validated
 * (__list_add_valid) but whose address is never actually passed to
 * tee_spi_transfer() — vestigial spi_message/spi_transfer-shaped
 * boilerplate that also encodes an unused-by-us `speed_hz`-looking field
 * (32-bit value 2000000, i.e. 2 MHz) — not reproduced here as it has no
 * effect on the actual TEE call.
 *
 * Two values below were re-verified byte-for-byte against
 * disasm.txt/reloc_all.txt in a later independent re-check (2026-08-19,
 * second pass) and CORRECTED from the first reconstruction pass:
 *  - inbuf[0]: disasm is `mov w9, #252 ; stur w9, [x29, #-24]` right
 *    before the call — inbuf[0] is 0xfc (252), not 0x80 as first
 *    guessed. inbuf[1..5] are confirmed zero (`sturh wzr, [x29, #-20]`
 *    completes the 6-byte zero-fill of the tail).
 *  - id assembly: the disasm does *one* unaligned 4-byte load,
 *    `ldur w19, [x29, #-14]`, where outbuf==x29-16, i.e. it reads
 *    outbuf+2 as a native (little-endian) u32 — rx[0]/rx[1] are
 *    discarded, and id is built from rx[2..5] in LE order, not a
 *    manual big-endian composition of rx[0..3] as first guessed. For
 *    silfp_check_chip()'s `(id>>16)==0x6193` to hold, a genuine sensor
 *    must return rx[4]==0x93, rx[5]==0x61.
 */
u32 silfp_spi_read_id(void)
{
	struct {
		struct list_head list;
		u8 tx[6];
		u8 rx[6];
	} xfer = { 0 };
	u32 id = 0;

	INIT_LIST_HEAD(&xfer.list);
	xfer.tx[0] = 0xfc;

	tee_spi_transfer((void *)smt_conf, sizeof(smt_conf),
			  &xfer.tx, &xfer.rx, 6);

	id = (u32)xfer.rx[2] | ((u32)xfer.rx[3] << 8) |
	     ((u32)xfer.rx[4] << 16) | ((u32)xfer.rx[5] << 24);

	SIL_LOG("[%s] chip id raw: %#x\n", __func__, id);

	return id;
}

/*
 * silfp_check_chip() — id>>16 must equal 0x6193 (GSL6193 family).
 * Comparison confirmed exact from disasm literal immediate 24979.
 */
int silfp_check_chip(void)
{
	u32 id = silfp_spi_read_id();

	if ((id >> 16) != SILFP_CHIP_ID_MASK) {
		SIL_LOG("[%s] unsupported chipid: %#x\n", __func__, id);
		return -1;
	}

	SIL_LOG("[%s] support chipid: %#x\n", __func__, id);
	return 0;
}

/*
 * silfp_hw_reset() — pulses the reset line through the pinctrl "rst-*"
 * states, with an optional extra "irq_rst-*" pulse when the device also
 * needs the IRQ line toggled as part of reset (gated on a field read at
 * struct-offset +224 in the original, reproduced here as
 * fp_dev->irq_wake_enabled reuse — see file header on struct-offset
 * fidelity). ms: reset pulse width, 0 selects the 5/3 ms defaults
 * (confirmed: `mov w9,#5` substituted when the low-byte of the requested
 * width is zero).
 */
/* MINDONE-FP-PINCHECK (F3296): silfp_hw_reset() called pinctrl_select_state()
 * four times with no pointer check, which NULL-derefed the kernel on 31.08
 * (module load already logs "there is not valid maps for state default" —
 * lookup failed, pointers invalid). GPIO 157 itself never toggled — the
 * bug was ours, not the pin. Full panic trace: MINDONE-MODULES-NOTES-0901.
 * This wrapper silently skips the switch when a state is unavailable. */
static void silfp_pin_select(struct silead_fp_dev *fp_dev,
			     struct pinctrl_state *st, const char *name)
{
	if (IS_ERR_OR_NULL(fp_dev->pinctrl) || IS_ERR_OR_NULL(st)) {
		pr_debug("[+silead_fp-] MINDONE-FP-PINCHECK: state %s unavailable, skipping\n",
			name);
		return;
	}
	pinctrl_select_state(fp_dev->pinctrl, st);
}

void silfp_hw_reset(struct silead_fp_dev *fp_dev, u32 ms)
{
	u32 low_ms = ms & 0xff;
	u32 high_ms;

	if (sil_debug_level >= 2) {
		SIL_LOG("[%s] pinctrl error\n", __func__);
		goto do_reset;
	}

do_reset:
	/* MINDONE-FP-RESETLOG (F3280): marker before GPIO 157 goes low. */
	pr_debug("[+silead_fp-] MINDONE-FP-HWRESET: pulling rst-low (GPIO 157), ms=%u\n", ms);
	silfp_pin_select(fp_dev, fp_dev->pins_rst_low, "rst-low");

	if (fp_dev->irq_wake_enabled)
		silfp_pin_select(fp_dev, fp_dev->pins_irq_rst_low, "irq-rst-low");

	if (!low_ms)
		low_ms = SILFP_RESET_LOW_MS_DEFAULT;
	mdelay(low_ms);

	silfp_pin_select(fp_dev, fp_dev->pins_rst_high, "rst-high");

	if (fp_dev->irq_wake_enabled)
		silfp_pin_select(fp_dev, fp_dev->pins_irq_rst_high, "irq-rst-high");

	high_ms = (ms & 0xff) ? (ms & 0xff) : SILFP_RESET_HIGH_MS_DEFAULT;
	mdelay(high_ms);
}

/*
 * silfp_netlink_send() — despite the name (kept for symbol-table
 * fidelity, per file header), this is NOT a netlink kernel socket: it
 * kmalloc()s a small event node, links it under fp_dev->event_lock onto
 * fp_dev->event_list, and wake_up()s fp_dev->read_queue. Confirmed 1:1
 * from disassembly (kmem_cache_alloc_trace + __list_add_valid +
 * spin_lock_irqsave/unlock + __wake_up, no netlink_* imports anywhere in
 * the object).
 */
void silfp_netlink_send(struct silead_fp_dev *fp_dev, u8 event)
{
	struct silfp_event *node;
	unsigned long flags;

	if (!fp_dev)
		return;

	node = kmalloc(sizeof(*node), GFP_ATOMIC);
	if (!node)
		return;

	node->data = event;

	spin_lock_irqsave(&fp_dev->event_lock, flags);
	list_add_tail(&node->list, &fp_dev->event_list);
	spin_unlock_irqrestore(&fp_dev->event_lock, flags);

	wake_up_interruptible(&fp_dev->read_queue);
}

/*
 * silfp_touch_event_handler() — EXPORT_SYMBOL'd (confirmed via
 * __ksymtab_silfp_touch_event_handler in the shipped .ko: this is a
 * public entry point other drivers/modules can call directly, e.g. a
 * companion touch/gesture path). Debounces against fp_dev->lasttouchmode
 * so repeat calls with the same low bit are dropped, maps the direction
 * bit to event 6 (touch down) or 7 (touch up), forwards through
 * silfp_netlink_send(), and grabs a short wakeup_source hold via
 * pm_wakeup_ws_event() (2500 jiffies-to-ms, confirmed literal) so the
 * event survives suspend races.
 */
int silfp_touch_event_handler(u8 *touchdata)
{
	struct silead_fp_dev *fp_dev;
	u8 mode;
	u8 event;

	if (!touchdata)
		return 0;

	fp_dev = g_fp_dev;
	if (!fp_dev)
		return 0;

	mode = touchdata[0];
	if (mode == fp_dev->lasttouchmode)
		return 0;

	event = (mode & 1) ? 6 : 7;

	silfp_netlink_send(g_fp_dev, event);

	fp_dev->lasttouchmode = mode;
	if (fp_dev->ws_hal)
		pm_wakeup_ws_event(fp_dev->ws_hal, 2500, false);	/* MINDONE-FP-HOLD: 2.5 s, not 2500 jiffies (F3813) */

	return 0;
}
EXPORT_SYMBOL(silfp_touch_event_handler);

static irqreturn_t silfp_irq_handler(int irq, void *data)
{
	struct silead_fp_dev *fp_dev = data;

	if (fp_dev->ws_irq)
		pm_wakeup_ws_event(fp_dev->ws_irq, 2500, false);	/* MINDONE-FP-HOLD (F3813) */

	queue_work_on(WORK_CPU_UNBOUND, silfp_wq, &fp_dev->work);
	complete(&fp_dev->irq_completion);

	return IRQ_HANDLED;
}

static void silfp_work_func(struct work_struct *work)
{
	struct silead_fp_dev *fp_dev =
		container_of(work, struct silead_fp_dev, work);

	SIL_LOG("[%s]\n", __func__);
	silfp_netlink_send(fp_dev, 1);
}

/*
 * silfp_fb_callback() — mtk_disp_notify blank/unblank callback. On each
 * transition sends a netlink-style event so the userspace fingerprint
 * HAL can adjust sensor state (screen-on/off gates fingerprint scanning
 * on many devices). Exact event numbering for each disp_notify state was
 * not fully resolved in this pass (see file header); reproduced with the
 * confirmed call shape (silfp_netlink_send() called from here, guarded
 * by g_fp_dev/data-null checks that print "silfp_data/disp_status is
 * null" — confirmed rodata string — when either is unavailable).
 */
static int silfp_fb_callback(struct notifier_block *nb, unsigned long event,
			      void *data)
{
	struct silead_fp_dev *fp_dev =
		container_of(nb, struct silead_fp_dev, disp_notif);

	if (!g_fp_dev || !data) {
		SIL_LOG("[+silead_fp-] silfp_data/disp_status is null\n");
		return NOTIFY_DONE;
	}

	SIL_LOG("notifier,event:%lu, not care\n", event);

	switch (event) {
	case MTK_DISP_EARLY_EVENT_BLANK:
		silfp_netlink_send(fp_dev, 8);
		break;
	case MTK_DISP_EVENT_BLANK:
		silfp_netlink_send(fp_dev, 9);
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static ssize_t silfp_read(struct file *filp, char __user *buf, size_t count,
			   loff_t *pos)
{
	struct silead_fp_dev *fp_dev = filp->private_data;
	struct silfp_event *node = NULL;
	unsigned long flags;
	DEFINE_WAIT(wait);
	int ret;

	if (count < 1)
		return -EINVAL;

	for (;;) {
		spin_lock_irqsave(&fp_dev->event_lock, flags);
		if (!list_empty(&fp_dev->event_list)) {
			node = list_first_entry(&fp_dev->event_list,
						 struct silfp_event, list);
			list_del(&node->list);
		}
		spin_unlock_irqrestore(&fp_dev->event_lock, flags);

		if (node)
			break;

		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;

		prepare_to_wait_event(&fp_dev->read_queue, &wait,
				       TASK_INTERRUPTIBLE);
		if (list_empty(&fp_dev->event_list))
			schedule();
		finish_wait(&fp_dev->read_queue, &wait);

		if (signal_pending(current))
			return -ERESTARTSYS;
	}

	ret = copy_to_user(buf, &node->data, 1);
	kfree(node);
	if (ret)
		return -EFAULT;

	return 1;
}

static __poll_t silfp_poll(struct file *filp, struct poll_table_struct *wait)
{
	struct silead_fp_dev *fp_dev = filp->private_data;
	__poll_t mask = 0;

	poll_wait(filp, &fp_dev->read_queue, wait);

	if (!list_empty(&fp_dev->event_list))
		mask |= EPOLLIN | EPOLLRDNORM;

	return mask;
}

static int silfp_open(struct inode *inode, struct file *filp)
{
	struct silead_fp_dev *fp_dev;
	int ret = -ENXIO;

	mutex_lock(&device_list_lock);
	list_for_each_entry(fp_dev, &device_list, device_entry) {
		if (fp_dev->devt == inode->i_rdev) {
			ret = 0;
			break;
		}
	}

	if (!ret) {
		SIL_LOG("[%s]\n", __func__);
		filp->private_data = fp_dev;
		nonseekable_open(inode, filp);
		fp_dev->users++;
	}
	mutex_unlock(&device_list_lock);

	return ret;
}

static int silfp_release(struct inode *inode, struct file *filp)
{
	struct silead_fp_dev *fp_dev = filp->private_data;

	mutex_lock(&device_list_lock);
	if (fp_dev->users > 0)
		fp_dev->users--;
	mutex_unlock(&device_list_lock);

	return 0;
}

/*
 * silfp_ioctl() — see file header: operations are confirmed, exact
 * per-command numeric encoding is best-effort. Magic 's' confirmed
 * exact.
 */
static long silfp_ioctl(struct file *filp, unsigned int cmd,
			 unsigned long arg)
{
	struct silead_fp_dev *fp_dev = filp->private_data;
	unsigned long flags;
	int ret = 0;
	int val;

	if (_IOC_TYPE(cmd) != SILFP_IOC_MAGIC)
		return -ENOTTY;

	switch (cmd) {
	case SILFP_IOC_INIT:
		SIL_LOG("[%s] INIT\n", __func__);
		break;

	case SILFP_IOC_EXIT:
		SIL_LOG("[%s] EXIT\n", __func__);
		break;

	case SILFP_IOC_RESET:
		/* MINDONE-FP-RESETLOG (F3280): this branch used to be silent, so a
		 * hardware-reset call left no trace. Reset pulls GPIO 157 low
		 * (pinmux 0x9d00, state_reset_low/high); the DT declares that same
		 * pin both as flash HWEN (false declaration, F3257) and as
		 * fingerprint reset. Logged unconditionally so the call shows up
		 * in the host-side capture. */
		pr_info("[+silead_fp-] MINDONE-FP-RESET: SILFP_IOC_RESET called\n");
		silfp_hw_reset(fp_dev, 0);
		pr_info("[+silead_fp-] MINDONE-FP-RESET: returned\n");
		break;

	case SILFP_IOC_ENABLE_IRQ:
	case SILFP_IOC_ENABLE_IRQ_HAL:
		spin_lock_irqsave(&fp_dev->irq_lock, flags);
		if (!fp_dev->irq_enabled) {
			enable_irq(fp_dev->irq);
			fp_dev->irq_enabled = true;
		}
		spin_unlock_irqrestore(&fp_dev->irq_lock, flags);
		break;

	case SILFP_IOC_DISABLE_IRQ:
	case SILFP_IOC_DISABLE_IRQ_HAL:
		spin_lock_irqsave(&fp_dev->irq_lock, flags);
		if (fp_dev->irq_enabled) {
			disable_irq_nosync(fp_dev->irq);
			fp_dev->irq_enabled = false;
		}
		spin_unlock_irqrestore(&fp_dev->irq_lock, flags);
		break;

	case SILFP_IOC_PINCTRL:
		if (get_user(val, (int __user *)arg))
			return -EFAULT;
		if (val)
			pinctrl_select_state(fp_dev->pinctrl,
					      fp_dev->pins_rst_high);
		else
			pinctrl_select_state(fp_dev->pinctrl,
					      fp_dev->pins_rst_low);
		break;

	case SILFP_IOC_WAIT_IRQ:
		reinit_completion(&fp_dev->irq_completion);
		ret = wait_for_completion_interruptible(&fp_dev->irq_completion);
		break;

	case SILFP_IOC_KEYEVENT:
		if (get_user(val, (int __user *)arg))
			return -EFAULT;
		if (fp_dev->input && val >= 0 && val < 7) {
			input_report_key(fp_dev->input, keymap[val][1], 1);
			input_sync(fp_dev->input);
			input_report_key(fp_dev->input, keymap[val][1], 0);
			input_sync(fp_dev->input);
		}
		break;

	/* MINDONE-SILFP-IOCMAP (F3243): the commands the HAL actually sends. */
	case SILFP_IOC_POLL_A:
	case SILFP_IOC_POLL_B:
		/* Vendor behavior: check debug level, return -ENOENT ("no events").
		 * Must not return -EINVAL here — different semantics; the HAL
		 * polls these ~14 times per boot and expects exactly -ENOENT. */
		ret = -ENOENT;
		break;

	case SILFP_IOC_WAKE_HOLD: {
		/* Vendor (0x1b18): copy_from_user 1 byte; if nonzero,
		 * pm_wakeup_ws_event(ws, jiffies_to_msecs(2500), false); returns 0.
		 * The HAL's most frequent ioctl: 38 calls per boot. This is what
		 * actually holds the wakeup source our port only ever registered
		 * but never held (F3239). */
		char hold = 0;

		if (!(void __user *)arg)
			return -EINVAL;
		if (copy_from_user(&hold, (void __user *)arg, 1))
			return -EFAULT;
		if (hold && fp_dev->ws_irq)
			pm_wakeup_ws_event(fp_dev->ws_irq, 2500, false);	/* MINDONE-FP-HOLD (F3813) */
		ret = 0;
		break;
	}

	case SILFP_IOC_HW_RESET: {
		unsigned char ms = 2;	/* vendor default when arg is NULL */

		if ((void __user *)arg &&
		    copy_from_user(&ms, (void __user *)arg, 1))
			return -EFAULT;
		if (mindone_fp_hwreset) {
			pr_debug("[+silead_fp-] MINDONE-FP-HWRESET: reset, ms=%u\n", ms);
			silfp_hw_reset(fp_dev, ms);
		} else {
			pr_debug("[+silead_fp-] MINDONE-FP-HWRESET: skipped (ms=%u), "
				"enable via mindone_fp_hwreset=1\n", ms);
		}
		ret = 0;
		break;
	}

	case SILFP_IOC_SET_MODE: {
		unsigned char mode = 0;

		if (!(void __user *)arg)
			return -EINVAL;
		if (copy_from_user(&mode, (void __user *)arg, 1))
			return -EFAULT;
		if (mode > 3)		/* vendor handler rejects > 3 */
			return -EINVAL;
		mindone_fp_mode = mode;
		SIL_LOG("[%s] SET_MODE %u\n", __func__, mode);
		ret = 0;
		break;
	}

	case SILFP_IOC_GET_VERSION:
		if (!(void __user *)arg)
			return -EINVAL;
		if (copy_to_user((void __user *)arg, mindone_fp_version,
				 sizeof(mindone_fp_version)))
			return -EFAULT;
		SIL_LOG("[%s] GET_VERSION\n", __func__);
		ret = 0;
		break;

	case SILFP_IOC_GET_CONFIG:
		if (!(void __user *)arg)
			return -EINVAL;
		if (copy_to_user((void __user *)arg, mindone_fp_config,
				 sizeof(mindone_fp_config)))
			return -EFAULT;
		SIL_LOG("[%s] GET_CONFIG\n", __func__);
		ret = 0;
		break;

	default:
		/* MINDONE-FP-UNKNOWN (F3275): this branch used to stay silent and
		 * return -EINVAL, so there was no way to see which command was
		 * missing — HAL opens /dev/silead_fp, then fails with "init dev
		 * fail" / "Could not init silead device (-1008)" and the trail
		 * went cold. Logging nr/dir/size here is what let the missing
		 * commands above be identified. Uses pr_info, not SIL_LOG: SIL_LOG's
		 * threshold is inverted (prints only when sil_debug_level < 2) and
		 * is easy to silence by raising the debug level. */
		pr_info("[+silead_fp-] MINDONE-FP-UNKNOWN: cmd=0x%x nr=%u dir=%u size=%u\n",
			cmd, _IOC_NR(cmd), _IOC_DIR(cmd), _IOC_SIZE(cmd));
		return -EINVAL;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long silfp_compat_ioctl(struct file *filp, unsigned int cmd,
				unsigned long arg)
{
	return silfp_ioctl(filp, cmd, (unsigned long)(u32)arg);
}
#endif

static int silfp_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "sil_debug_level: %d\n", sil_debug_level);
	return 0;
}

static int silfp_proc_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, silfp_proc_show, NULL);
}

static const struct proc_ops silfp_proc_fops = {
	.proc_open = silfp_proc_open,
	.proc_read = seq_read,
	.proc_release = single_release,
};

static const struct file_operations silfp_dev_fops = {
	.owner = THIS_MODULE,
	.read = silfp_read,
	.poll = silfp_poll,
	.open = silfp_open,
	.release = silfp_release,
	.unlocked_ioctl = silfp_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = silfp_compat_ioctl,
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0)
	.llseek = no_llseek,
#else	/* no_llseek removed in 6.12 */
	.llseek = noop_llseek,
#endif
};

/*
 * silfp_resource_init() — DT lookup (via the "sil,silead_fp-pins"
 * compatible node), pinctrl states, reset GPIO, IRQ, and the fp-keys
 * input device. Call sequence confirmed from disassembly:
 * of_find_compatible_node -> irq_of_parse_and_map,
 * of_find_compatible_node -> of_find_device_by_node -> devm_pinctrl_get,
 * pinctrl_lookup_state x5 ("irq-init"/"rst-high"/"rst-low"/
 * "irq_rst-high"/"irq_rst-low"), pinctrl_select_state, gpio_request +
 * gpio_to_desc + gpiod_direction_output_raw (SILFP_RST_PIN),
 * request_threaded_irq + irq_set_irq_wake, spin_lock_irqsave +
 * disable_irq_nosync (IRQ starts disabled until userspace enables it),
 * input_allocate_device/input_register_device ("fp-keys").
 */
static int silfp_resource_init(struct silead_fp_dev *fp_dev,
				struct platform_device *pdev)
{
	struct device_node *pins_np;
	struct platform_device *pins_pdev;
	unsigned long flags;
	int i, ret;

	pins_np = of_find_compatible_node(NULL, NULL, "sil,silead_fp-pins");
	if (!pins_np) {
		dev_err(&pdev->dev, "%s: failed to find pins node\n",
			__func__);
		return -ENODEV;
	}

	fp_dev->irq = irq_of_parse_and_map(pins_np, 0);

	pins_pdev = of_find_device_by_node(pins_np);
	if (!pins_pdev)
		return -ENODEV;

	fp_dev->pinctrl = devm_pinctrl_get(&pins_pdev->dev);
	if (IS_ERR(fp_dev->pinctrl))
		return PTR_ERR(fp_dev->pinctrl);

	fp_dev->pins_default = pinctrl_lookup_state(fp_dev->pinctrl,
						     "irq-init");
	fp_dev->pins_rst_high = pinctrl_lookup_state(fp_dev->pinctrl,
						      "rst-high");
	fp_dev->pins_rst_low = pinctrl_lookup_state(fp_dev->pinctrl,
						     "rst-low");
	fp_dev->pins_irq_rst_high = pinctrl_lookup_state(fp_dev->pinctrl,
							  "irq_rst-high");
	fp_dev->pins_irq_rst_low = pinctrl_lookup_state(fp_dev->pinctrl,
							 "irq_rst-low");

	if (!IS_ERR_OR_NULL(fp_dev->pins_default))
		pinctrl_select_state(fp_dev->pinctrl, fp_dev->pins_default);

	/* The shipped .ko imports only raw gpio_request()/gpio_to_desc(),
	 * never any of_get_named_gpio() or of_property_read_u32() helper —
	 * so the reset GPIO number is not resolved via a generic OF
	 * accessor on the kernel side in the original (that call graph
	 * was not traced further
	 * further in this pass; "fp_id" is confirmed only as a debug-log
	 * label string in .rodata, not confirmed as a gpio DT property
	 * name). Exposed as a module parameter here so it stays board-
	 * configurable without inventing an unverified OF lookup.
	 */
	fp_dev->rst_gpio = rst_gpio;
	if (gpio_is_valid(fp_dev->rst_gpio)) {
		ret = gpio_request(fp_dev->rst_gpio, "SILFP_RST_PIN");
		if (!ret)
			gpiod_direction_output_raw(
				gpio_to_desc(fp_dev->rst_gpio), 1);
	}

	ret = request_threaded_irq(fp_dev->irq, NULL, silfp_irq_handler,
				    IRQF_TRIGGER_RISING | IRQF_ONESHOT,
				    "silead_fp", fp_dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "%s: request_irq failed, ret=%d\n",
			__func__, ret);
		return ret;
	}

	irq_set_irq_wake(fp_dev->irq, 1);
	fp_dev->irq_wake_enabled = true;

	/* IRQ starts disabled; userspace enables it via SILFP_IOC_ENABLE_IRQ
	 * once it is ready to receive touch events (confirmed pattern).
	 */
	spin_lock_irqsave(&fp_dev->irq_lock, flags);
	disable_irq_nosync(fp_dev->irq);
	fp_dev->irq_enabled = false;
	spin_unlock_irqrestore(&fp_dev->irq_lock, flags);

	fp_dev->input = input_allocate_device();
	if (fp_dev->input) {
		fp_dev->input->name = SILFP_INPUT_NAME;
		for (i = 0; i < 7; i++)
			if (keymap[i][1])
				set_bit(keymap[i][1], fp_dev->input->keybit);
		set_bit(EV_KEY, fp_dev->input->evbit);

		ret = input_register_device(fp_dev->input);
		if (ret) {
			input_free_device(fp_dev->input);
			fp_dev->input = NULL;
		}
	}

	return 0;
}

/*
 * silfp_resource_deinit() — mirror of resource_init: disable+free irq,
 * release the reset gpio back to input, unregister the input device,
 * remove the proc entry, and send a final EXIT event. Confirmed call
 * sequence from disassembly.
 */
static void silfp_resource_deinit(struct silead_fp_dev *fp_dev)
{
	unsigned long flags;

	spin_lock_irqsave(&fp_dev->irq_lock, flags);
	if (fp_dev->irq_enabled) {
		disable_irq_nosync(fp_dev->irq);
		fp_dev->irq_enabled = false;
	}
	spin_unlock_irqrestore(&fp_dev->irq_lock, flags);

	free_irq(fp_dev->irq, fp_dev);

	if (gpio_is_valid(fp_dev->rst_gpio))
		gpiod_direction_input(gpio_to_desc(fp_dev->rst_gpio));

	if (fp_dev->input)
		input_unregister_device(fp_dev->input);

	if (silfp_proc_entry)
		remove_proc_entry("silfp", NULL);

	silfp_netlink_send(fp_dev, 0);
}

static int silfp_probe(struct platform_device *pdev)
{
	struct silead_fp_dev *fp_dev;
	int ret;

	fp_dev = kzalloc(sizeof(*fp_dev), GFP_KERNEL);
	if (!fp_dev)
		return -ENOMEM;

	fp_dev->pdev = pdev;
	fp_dev->users = 0;
	mutex_init(&fp_dev->ops_lock);
	spin_lock_init(&fp_dev->irq_lock);
	spin_lock_init(&fp_dev->event_lock);
	INIT_LIST_HEAD(&fp_dev->event_list);
	INIT_LIST_HEAD(&fp_dev->device_entry);
	init_waitqueue_head(&fp_dev->read_queue);
	init_completion(&fp_dev->irq_completion);
	INIT_WORK(&fp_dev->work, silfp_work_func);

	fp_dev->ws_irq = wakeup_source_register(NULL, "silfp_wakelock");
	fp_dev->ws_hal = wakeup_source_register(NULL, "silfp_wakelock_hal");

	mutex_lock(&device_list_lock);
	fp_dev->devt = MKDEV(0, 0);
	ret = alloc_chrdev_region(&fp_dev->devt, 0, 1, SILFP_CLASS_NAME);
	if (ret < 0) {
		mutex_unlock(&device_list_lock);
		goto err_free;
	}

	fp_dev->device = device_create(silfp_class, &pdev->dev, fp_dev->devt,
					fp_dev, SILFP_CLASS_NAME);
	if (IS_ERR(fp_dev->device)) {
		ret = PTR_ERR(fp_dev->device);
		unregister_chrdev_region(fp_dev->devt, 1);
		mutex_unlock(&device_list_lock);
		goto err_free;
	}

	list_add(&fp_dev->device_entry, &device_list);
	mutex_unlock(&device_list_lock);

	cdev_init(&fp_dev->cdev, &silfp_dev_fops);
	fp_dev->cdev.owner = THIS_MODULE;
	ret = cdev_add(&fp_dev->cdev, fp_dev->devt, 1);
	if (ret) {
		dev_err(&pdev->dev, "%s: cdev_add failed, ret=%d\n",
			__func__, ret);
		goto err_device;
	}

	ret = silfp_resource_init(fp_dev, pdev);
	if (ret)
		SIL_LOG("[%s] resource init incomplete, ret=%d\n",
			__func__, ret);

	fp_dev->disp_notif.notifier_call = silfp_fb_callback;
	ret = mtk_disp_notifier_register("silead_fp", &fp_dev->disp_notif);
	if (ret)
		SIL_LOG("[+silead_fp-] Failed to register disp notifier client\n");

	platform_set_drvdata(pdev, fp_dev);
	g_fp_dev = fp_dev;

	silfp_proc_entry = proc_create("silfp", 0, NULL, &silfp_proc_fops);

	return 0;

err_device:
	mutex_lock(&device_list_lock);
	list_del(&fp_dev->device_entry);
	device_destroy(silfp_class, fp_dev->devt);
	unregister_chrdev_region(fp_dev->devt, 1);
	mutex_unlock(&device_list_lock);
err_free:
	if (fp_dev->ws_irq)
		wakeup_source_unregister(fp_dev->ws_irq);
	if (fp_dev->ws_hal)
		wakeup_source_unregister(fp_dev->ws_hal);
	kfree(fp_dev);
	return ret;
}

static void silfp_remove(struct platform_device *pdev)
{
	struct silead_fp_dev *fp_dev = platform_get_drvdata(pdev);

	if (!fp_dev)
		return;

	/* MINDONE-FP-NOTIFIER (F3288): registers into the display notifier chain
	 * (mtk_disp_notifier_register, see silfp_probe()) but never unregistered.
	 * After rmmod the chain held a pointer into freed memory; the next
	 * screen event panicked the kernel through it (dead black screen; full
	 * trace and vendor-comparison in MINDONE-MODULES-NOTES-0901).
	 * We reload modules live, so unregister first, before freeing anything
	 * else. */
	mtk_disp_notifier_unregister(&fp_dev->disp_notif);

	silfp_resource_deinit(fp_dev);

	if (fp_dev->ws_irq)
		wakeup_source_unregister(fp_dev->ws_irq);
	if (fp_dev->ws_hal)
		wakeup_source_unregister(fp_dev->ws_hal);

	destroy_workqueue(silfp_wq);

	mutex_lock(&device_list_lock);
	cdev_del(&fp_dev->cdev);
	list_del(&fp_dev->device_entry);
	device_destroy(silfp_class, fp_dev->devt);
	unregister_chrdev_region(fp_dev->devt, 1);
	mutex_unlock(&device_list_lock);

	g_fp_dev = NULL;
	kfree(fp_dev);

}

static const struct of_device_id sildev_dt_ids[] = {
	{ .compatible = "sil,silead_fp", },
	{ .compatible = "sil,silead-fp", },
	{ .compatible = "sil,fingerprint", },
	{ .compatible = "sil,silead_fp-pins", },
	{},
};
MODULE_DEVICE_TABLE(of, sildev_dt_ids);

static struct platform_driver silfp_driver = {
	.probe = silfp_probe,
	.remove_new = silfp_remove,
	.driver = {
		.name = SILFP_CLASS_NAME,
		.of_match_table = sildev_dt_ids,
	},
};

/* MINDONE-FP-NOSUSPWAKE (01.09.2026, F3396): the fingerprint IRQ is armed
 * as a system wakeup source (irq_set_irq_wake, 1). During s2idle it fires
 * spontaneously (bench: ~7/20 forced-suspend cycles woke on IRQ silead_fp),
 * ending the sleep and, over hundreds of cycles, degrading the PMIC/panel.
 * No fingerprint-to-wake path exists in this OS (the HAL that would gate
 * scanning is absent), and the power key wakes the device anyway. So drop
 * the fp IRQ's wakeup capability on suspend entry and restore it on resume
 * — other wakeup sources are unaffected, and fp works normally while awake. */
/* MINDONE-FP-NOSUSPWAKE v2 (F3396/F3397): disable_irq_wake alone did NOT stop
 * the fp waking s2idle (bench: still 13/20) — the IRQ stays ENABLED, so on
 * every spontaneous edge the handler runs and takes silfp_wakelock, which
 * ends the sleep. Masking the IRQ itself (disable_irq) on suspend entry stops
 * the handler from running at all; re-enable on resume. Balanced with our own
 * flag so the HAL's enable/disable refcount is not disturbed. The sensor works
 * normally while awake; deep-idle fp-wake is not a working feature here (no
 * HAL wake path) and the power key wakes the device. */
static bool mindone_fp_irq_masked;

static int mindone_fp_pm_event(struct notifier_block *nb,
			       unsigned long event, void *unused)
{
	struct silead_fp_dev *fp_dev = g_fp_dev;
	unsigned long flags;

	if (!fp_dev || fp_dev->irq <= 0)
		return NOTIFY_DONE;
	if (event == PM_SUSPEND_PREPARE && !mindone_fp_irq_masked) {
		spin_lock_irqsave(&fp_dev->irq_lock, flags);
		disable_irq_nosync(fp_dev->irq);
		mindone_fp_irq_masked = true;
		spin_unlock_irqrestore(&fp_dev->irq_lock, flags);
		pr_debug("MINDONE-FP-NOSUSPWAKE: suspend -> fp irq MASKED\n");
	} else if (event == PM_POST_SUSPEND && mindone_fp_irq_masked) {
		spin_lock_irqsave(&fp_dev->irq_lock, flags);
		enable_irq(fp_dev->irq);
		mindone_fp_irq_masked = false;
		spin_unlock_irqrestore(&fp_dev->irq_lock, flags);
		pr_debug("MINDONE-FP-NOSUSPWAKE: resume -> fp irq UNMASKED\n");
	}
	return NOTIFY_DONE;
}

static struct notifier_block mindone_fp_pm_nb = {
	.notifier_call = mindone_fp_pm_event,
};

static int silfp_dev_init(void)
{
	int ret;
	dev_t devt;

	pr_notice("silead_fp: %s\n", SILFP_DRV_VERSION);
	register_pm_notifier(&mindone_fp_pm_nb);

	ret = __register_chrdev(0, 0, 256, SILFP_DEV_NAME, &silfp_dev_fops);
	if (ret < 0)
		return ret;

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 4, 0)
	silfp_class = class_create(THIS_MODULE, SILFP_CLASS_NAME);
#else	/* class_create dropped the owner arg in 6.4 */
	silfp_class = class_create(SILFP_CLASS_NAME);
#endif
	if (IS_ERR(silfp_class)) {
		devt = MKDEV(ret, 0);
		__unregister_chrdev(MAJOR(devt), 0, 256, SILFP_DEV_NAME);
		return PTR_ERR(silfp_class);
	}

	ret = platform_driver_register(&silfp_driver);
	if (ret) {
		class_destroy(silfp_class);
		__unregister_chrdev(0, 0, 256, SILFP_DEV_NAME);
		return ret;
	}

	silfp_wq = alloc_workqueue("%s", WQ_UNBOUND | WQ_HIGHPRI, 1,
				    SILFP_WQ_NAME);

	return 0;
}

static void silfp_dev_exit(void)
{
	unregister_pm_notifier(&mindone_fp_pm_nb);
	platform_driver_unregister(&silfp_driver);
	class_destroy(silfp_class);
	__unregister_chrdev(0, 0, 256, SILFP_DEV_NAME);
}

module_init(silfp_dev_init);
module_exit(silfp_dev_exit);

MODULE_AUTHOR("Bill Yu <billyu@silead.com>");
MODULE_DESCRIPTION("Gigadevice/Silead Fingerprint driver for GSL6xxx/GSL7xxx/GSL8xxx series.");
MODULE_LICENSE("GPL");
MODULE_ALIAS("sil:silead_fp");

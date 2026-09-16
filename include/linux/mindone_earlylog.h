/* SPDX-License-Identifier: GPL-2.0 */
/* MindOne early-boot RAM log: pre-pstore boot visibility for kernel 6.1
 * on iKKO MindOne (MT8781/MT6789). Kernel 6.1 hangs on real hardware
 * before fs/pstore registers its ramoops console, so nothing reaches
 * /sys/fs/pstore or expdb; this reuses the spare tail of the already-
 * confirmed ramoops physical window (0x48090000/0xe0000) for two
 * independent early capture tiers (checkpoint + console), read back via
 * a physical-memory dump of the window described below. */
#ifndef __MINDONE_EARLYLOG_H
#define __MINDONE_EARLYLOG_H

#include <linux/types.h>

/* Physical window: spare tail of the confirmed SYS_PSTORE_RAW region.
 * Whole region is 0x48090000/0xe0000; ramoops claims the first 0xc0000
 * (console 0x80000 + record 0x30000 + pmsg 0x10000). Kconfig
 * defaults below point at the unused last 0x20000 of that same window;
 * override only if the ramoops layout above ever changes.
 */
#define MINDONE_EARLYLOG_BASE	((phys_addr_t)CONFIG_MINDONE_EARLYLOG_BASE)
#define MINDONE_EARLYLOG_SIZE	((resource_size_t)CONFIG_MINDONE_EARLYLOG_SIZE)

/* sub-layout inside [BASE, BASE+SIZE) */
#define MINDONE_CKPT_OFF	0x0000
#define MINDONE_CKPT_SIZE	0x1000		/* 4K header, checkpoint tier */
#define MINDONE_CONSOLE_OFF	0x1000
#define MINDONE_CONSOLE_SIZE	(CONFIG_MINDONE_EARLYLOG_SIZE - MINDONE_CKPT_SIZE) /* ~124K */

#define MINDONE_CKPT_MAGIC	0x4d316b30U	/* "M1k0" */
#define MINDONE_CONSOLE_MAGIC	0x4d316c67U	/* "M1lg" */

/* checkpoint slot layout, all u32, within MINDONE_CKPT_OFF..+MINDONE_CKPT_SIZE */
#define MINDONE_CKPT_SLOT_MAGIC		0	/* MINDONE_CKPT_MAGIC once any checkpoint fires */
#define MINDONE_CKPT_SLOT_LAST_ID	1	/* id of the most recently written checkpoint */
#define MINDONE_CKPT_SLOT_MAX_ID	2	/* highest id ever reached (monotonic) */
#define MINDONE_CKPT_SLOT_HITCOUNT	3	/* total checkpoint writes this boot attempt */
#define MINDONE_CKPT_SLOT_HEAD_MAGIC	4	/* byte 0x10: Tier-0 head.S write, see below */

/* Tier-0: absolute-earliest checkpoint, 2 plain instructions in
 * head.S::primary_entry before the MMU is enabled (raw physical store,
 * safe against a hard reset). Distinct magic/slot from the C-tier so
 * HEAD_MAGIC-without-MAGIC narrows a hang to setup_arch()'s prologue,
 * and neither firing means the hang is at/before primary_entry. */
#define MINDONE_HEAD_MAGIC	0x4d316831U	/* "M1h1", head.S, distinct from MINDONE_CKPT_MAGIC */

/* checkpoint IDs, in expected boot order.
 * 0 is deliberately unused (a freshly-reset region reads all zero, which
 * must not be mistaken for a valid checkpoint).
 */
enum mindone_ckpt_id {
	MINDONE_CKPT_ARCH_ENTRY = 1,		/* setup_arch(): early_ioremap_init() done */
	MINDONE_CKPT_FDT_SCANNED,		/* setup_machine_fdt() returned */
	MINDONE_CKPT_PAGING_INIT,		/* paging_init() returned: linear map is live */
	MINDONE_CKPT_ARCH_EXIT,		/* setup_arch(): about to call early_ioremap_reset() */
	MINDONE_CKPT_CONSOLE_REGISTERED,	/* Tier-1 console live (early_initcall) */
	MINDONE_CKPT_MAX,
};

#ifdef CONFIG_MINDONE_EARLY_RAMLOG
/* Tier-2, valid ONLY while called from within setup_arch() -- see comment
 * block above. Calling this after early_ioremap_reset() has run is a bug
 * (early_ioremap's fixmap slots may be in use for something else by
 * then); Tier-1 stamps MINDONE_CKPT_CONSOLE_REGISTERED itself instead,
 * through its own already-mapped vmap() window.
 */
void mindone_earlylog_arch_checkpoint(enum mindone_ckpt_id id);

/* Claims [MINDONE_EARLYLOG_BASE, +MINDONE_EARLYLOG_SIZE) via
 * memblock_reserve() so the page allocator never hands these pages out
 * later in the same boot (window is ordinary pfn_valid() RAM, not
 * firmware-excluded). Call once,
 * as early as memblock is usable; setup_arch() does so right after
 * setup_machine_fdt(). Idempotent. */
void mindone_earlylog_arch_reserve(void);
#else
static inline void mindone_earlylog_arch_checkpoint(enum mindone_ckpt_id id) {}
static inline void mindone_earlylog_arch_reserve(void) {}
#endif

#endif /* __MINDONE_EARLYLOG_H */

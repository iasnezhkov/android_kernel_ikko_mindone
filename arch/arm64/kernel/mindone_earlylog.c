// SPDX-License-Identifier: GPL-2.0
/* MINDONE: Tier-2 early-boot checkpoint trail, called only from
 * setup_arch() inside the early_ioremap_init()..early_ioremap_reset()
 * window. Minimal: 4 words, map -> write -> unmap every time. Uses
 * early_ioremap() (uncached FIXMAP_PAGE_IO, not cached early_memremap())
 * so a completed write survives a hard watchdog reset right after.
 * Best-effort, mapping failure silently ignored. Details:
 * include/linux/mindone_earlylog.h. */
#include <linux/io.h>
#include <linux/memblock.h>
#include <linux/mindone_earlylog.h>
#include <asm/early_ioremap.h>

#define CKPT_REG(base, slot)	((void __iomem *)((base) + (slot) * sizeof(u32)))

/* "round 2" fix: this window is ordinary pfn_valid() RAM, not
 * firmware-excluded, so without an explicit reservation checkpoints
 * could be written during setup_arch() and then silently overwritten
 * later in the same boot (page allocator, or the ~732s spent on the
 * auto-reverted rollback kernel before expdb is ever read). This
 * function closes that gap. */
void __init mindone_earlylog_arch_reserve(void)
{
	memblock_reserve(MINDONE_EARLYLOG_BASE, MINDONE_EARLYLOG_SIZE);
}

void __init mindone_earlylog_arch_checkpoint(enum mindone_ckpt_id id)
{
	void __iomem *base;
	u32 max_id;

	if ((int)id <= 0 || id >= MINDONE_CKPT_MAX)
		return;

	base = early_ioremap(MINDONE_EARLYLOG_BASE + MINDONE_CKPT_OFF,
			      MINDONE_CKPT_SIZE);
	if (!base)
		return;

	max_id = readl_relaxed(CKPT_REG(base, MINDONE_CKPT_SLOT_MAX_ID));
	if ((u32)id > max_id)
		max_id = (u32)id;

	writel_relaxed((u32)id, CKPT_REG(base, MINDONE_CKPT_SLOT_LAST_ID));
	writel_relaxed(max_id, CKPT_REG(base, MINDONE_CKPT_SLOT_MAX_ID));
	writel_relaxed(readl_relaxed(CKPT_REG(base, MINDONE_CKPT_SLOT_HITCOUNT)) + 1,
		       CKPT_REG(base, MINDONE_CKPT_SLOT_HITCOUNT));
	wmb();
	/* magic written last: its presence marks the rest of the struct as
	 * fully (not torn-)written.
	 */
	writel_relaxed(MINDONE_CKPT_MAGIC, CKPT_REG(base, MINDONE_CKPT_SLOT_MAGIC));
	wmb();

	early_iounmap(base, MINDONE_CKPT_SIZE);
}

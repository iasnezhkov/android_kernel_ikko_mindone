// SPDX-License-Identifier: GPL-2.0
/* MINDONE: early ring log -- Tier-B (staged) and Tier-C (setup_arch()-time,
 * via early_ioremap()) writers. See include/linux/mindone_ringlog.h for
 * the four-tier design. Tier-C reuses the early_ioremap()/FIXMAP_PAGE_IO
 * reasoning already proven in mindone_earlylog.c: an uncached write is
 * immediately visible in DRAM, surviving a hard watchdog reset one
 * instruction later. */
#include <linux/io.h>
#include <linux/memblock.h>
#include <linux/mindone_ringlog.h>
#include <asm/early_ioremap.h>

/* Tier-B staging array: plain kernel-image .init.data, writable the
 * instant the image itself is mapped (__primary_switched(), well before
 * any DRAM-wide mapping exists) -- see the big comment in
 * include/linux/mindone_ringlog.h. Slot 0 is reserved for the Tier-B
 * write made directly from head.S (MRL_AFTER_MMU_ON); C callers
 * (currently only start_kernel()'s MRL_START_KERNEL) start at slot 1,
 * hence the initialiser below.
 */
struct mindone_ringlog_staged_entry
	mindone_ringlog_staged[MINDONE_RINGLOG_STAGED_MAX] __initdata;

static unsigned int mindone_ringlog_staged_next __initdata = 3; /* slots 0-2 written directly from asm (KASLR before/after, AFTER_MMU_ON) */

void __init mindone_ringlog_stage_local(enum mindone_ringlog_stage stage,
					 u32 reg0, u32 reg1)
{
	unsigned int i = mindone_ringlog_staged_next;

	if (i >= MINDONE_RINGLOG_STAGED_MAX)
		return;

	mindone_ringlog_staged[i].stage = (u32)stage;
	mindone_ringlog_staged[i].reg0 = reg0;
	mindone_ringlog_staged[i].reg1 = reg1;
	mindone_ringlog_staged[i].valid = 1;
	mindone_ringlog_staged_next = i + 1;
}

/* Core Tier-C record writer: maps the header, computes the next slot,
 * writes it, advances write_off/seq. Best-effort -- any mapping failure
 * is silently ignored, this must never be able to itself hang or crash
 * the boot it exists to observe.
 */
static void __init mindone_ringlog_write_one(enum mindone_ringlog_stage stage,
					       u32 reg0, u32 reg1)
{
	void __iomem *hdr;
	void __iomem *rec;
	u32 write_off, seq, new_off;
	phys_addr_t rec_phys;

	hdr = early_ioremap(MINDONE_RINGLOG_BASE, MINDONE_RINGLOG_HDR_SIZE);
	if (!hdr)
		return;

	write_off = readl_relaxed(hdr + MINDONE_RL_HDR_WRITE_OFF) & MINDONE_RINGLOG_BODY_MASK;
	seq = readl_relaxed(hdr + MINDONE_RL_HDR_SEQ);

	rec_phys = MINDONE_RINGLOG_BASE + MINDONE_RINGLOG_HDR_SIZE + write_off;
	rec = early_ioremap(rec_phys, MINDONE_RINGLOG_REC_SIZE);
	if (rec) {
		writel_relaxed(reg0, rec + MINDONE_RL_REC_REG0);
		writel_relaxed(reg1, rec + MINDONE_RL_REC_REG1);
		writel_relaxed(seq, rec + MINDONE_RL_REC_SEQ);
		wmb();
		writel_relaxed(((u32)MINDONE_RL_CANARY << MINDONE_RL_CANARY_SHIFT) |
				((u32)stage & MINDONE_RL_STAGE_MASK),
			       rec + MINDONE_RL_REC_STAGE);
		wmb();
		early_iounmap(rec, MINDONE_RINGLOG_REC_SIZE);
	}

	new_off = (write_off + MINDONE_RINGLOG_REC_SIZE) & MINDONE_RINGLOG_BODY_MASK;
	if (new_off <= write_off)
		writel_relaxed(1, hdr + MINDONE_RL_HDR_WRAPPED);
	writel_relaxed(new_off, hdr + MINDONE_RL_HDR_WRITE_OFF);
	writel_relaxed(seq + 1, hdr + MINDONE_RL_HDR_SEQ);
	writel_relaxed(MINDONE_RINGLOG_VERSION, hdr + MINDONE_RL_HDR_VERSION);
	wmb();
	writel_relaxed(MINDONE_RINGLOG_MAGIC, hdr + MINDONE_RL_HDR_MAGIC);
	wmb();

	early_iounmap(hdr, MINDONE_RINGLOG_HDR_SIZE);
}

static bool mindone_ringlog_flushed __initdata;

/* Drains mindone_ringlog_staged[] into the real ring, in slot order (==
 * chronological order: slot 0 is always the earliest Tier-B write, slot 1
 * the next, etc). Called automatically by the first mindone_ringlog_arch_mark(),
 * i.e. as soon as Tier-C itself becomes available.
 */
static void __init mindone_ringlog_flush_staged(void)
{
	unsigned int i;

	if (mindone_ringlog_flushed)
		return;
	mindone_ringlog_flushed = true;

	for (i = 0; i < MINDONE_RINGLOG_STAGED_MAX; i++) {
		if (!mindone_ringlog_staged[i].valid)
			continue;
		mindone_ringlog_write_one((enum mindone_ringlog_stage)mindone_ringlog_staged[i].stage,
					   mindone_ringlog_staged[i].reg0,
					   mindone_ringlog_staged[i].reg1);
	}
}

/* Claims the ring's physical window via memblock_reserve() so the page
 * allocator never hands these pages out later in the same boot (ordinary
 * pfn_valid() System RAM, not firmware-excluded). Call once, from
 * setup_arch() right after setup_machine_fdt() -- same spot
 * mindone_earlylog_arch_reserve() uses for its own window. */
void __init mindone_ringlog_arch_reserve(void)
{
	memblock_reserve(MINDONE_RINGLOG_BASE, MINDONE_RINGLOG_HDR_SIZE + MINDONE_RINGLOG_BODY_SIZE);
}

void __init mindone_ringlog_arch_mark(enum mindone_ringlog_stage stage, u32 reg0, u32 reg1)
{
	mindone_ringlog_flush_staged();
	mindone_ringlog_write_one(stage, reg0, reg1);
}

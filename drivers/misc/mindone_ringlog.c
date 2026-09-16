// SPDX-License-Identifier: GPL-2.0
/* MINDONE: early ring log -- Tier-D (post-setup_arch, long-lived
 * mapping) writer, plus the FIRST_PRINTK checkpoint. setup_arch() already
 * tore down Tier-C's fixmap by the time this runs, and the region is
 * ordinary pfn_valid() RAM (plain ioremap() would WARN_ON/NULL), hence
 * the same vmap()-of-existing-pages workaround as mindone_earlylog.c,
 * duplicated to keep the two mechanisms independent. Full design:
 * include/linux/mindone_ringlog.h. */
#include <linux/console.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/mindone_ringlog.h>

static void *mindone_rl_base;		/* vmap of [BASE, BASE+HDR_SIZE+BODY_SIZE) */
static DEFINE_RAW_SPINLOCK(mindone_rl_lock);
static bool mindone_rl_first_printk_seen;

static inline void rset32(unsigned int off, u32 val)
{
	*(volatile u32 *)(mindone_rl_base + off) = val;
}

static inline u32 rget32(unsigned int off)
{
	return *(volatile u32 *)(mindone_rl_base + off);
}

/* Port of fs/pstore/ram_core.c::persistent_ram_vmap(), identical in
 * spirit to mindone_vmap_ram() in drivers/misc/mindone_earlylog.c.
 */
static void *mindone_rl_vmap(phys_addr_t start, size_t size)
{
	unsigned int page_count = DIV_ROUND_UP(size + offset_in_page(start), PAGE_SIZE);
	phys_addr_t page_start = start - offset_in_page(start);
	struct page **pages;
	void *vaddr;
	unsigned int i;

	pages = kmalloc_array(page_count, sizeof(*pages), GFP_KERNEL);
	if (!pages)
		return NULL;
	for (i = 0; i < page_count; i++)
		pages[i] = pfn_to_page((page_start >> PAGE_SHIFT) + i);
	vaddr = vmap(pages, page_count, VM_MAP | VM_IOREMAP, pgprot_noncached(PAGE_KERNEL));
	kfree(pages);
	if (!vaddr)
		return NULL;
	return vaddr + offset_in_page(start);
}

static bool mindone_rl_ensure_mapped(void)
{
	if (mindone_rl_base)
		return true;
	mindone_rl_base = mindone_rl_vmap(MINDONE_RINGLOG_BASE,
					   MINDONE_RINGLOG_HDR_SIZE + MINDONE_RINGLOG_BODY_SIZE);
	if (!mindone_rl_base)
		pr_notice("mindone_ringlog: vmap(0x%llx) failed, Tier-D disabled\n",
			  (unsigned long long)MINDONE_RINGLOG_BASE);
	return mindone_rl_base != NULL;
}

/* Returns the (pre-wrap) write_off this record/header slot landed at, and
 * advances write_off/seq by exactly one MINDONE_RINGLOG_REC_SIZE slot.
 * Caller already holds mindone_rl_lock.
 */
static u32 mindone_rl_advance(u32 *out_seq)
{
	u32 write_off = rget32(MINDONE_RL_HDR_WRITE_OFF) & MINDONE_RINGLOG_BODY_MASK;
	u32 seq = rget32(MINDONE_RL_HDR_SEQ);
	u32 new_off = (write_off + MINDONE_RINGLOG_REC_SIZE) & MINDONE_RINGLOG_BODY_MASK;

	*out_seq = seq;
	if (new_off <= write_off)
		rset32(MINDONE_RL_HDR_WRAPPED, 1);
	rset32(MINDONE_RL_HDR_WRITE_OFF, new_off);
	rset32(MINDONE_RL_HDR_SEQ, seq + 1);
	rset32(MINDONE_RL_HDR_VERSION, MINDONE_RINGLOG_VERSION);
	wmb();
	rset32(MINDONE_RL_HDR_MAGIC, MINDONE_RINGLOG_MAGIC);
	wmb();
	return write_off;
}

void mindone_ringlog_mark(enum mindone_ringlog_stage stage, u32 reg0, u32 reg1)
{
	unsigned long flags;
	u32 write_off, seq;

	raw_spin_lock_irqsave(&mindone_rl_lock, flags);
	if (!mindone_rl_ensure_mapped())
		goto out;

	write_off = rget32(MINDONE_RL_HDR_WRITE_OFF) & MINDONE_RINGLOG_BODY_MASK;
	seq = rget32(MINDONE_RL_HDR_SEQ);

	rset32(MINDONE_RINGLOG_HDR_SIZE + write_off + MINDONE_RL_REC_REG0, reg0);
	rset32(MINDONE_RINGLOG_HDR_SIZE + write_off + MINDONE_RL_REC_REG1, reg1);
	rset32(MINDONE_RINGLOG_HDR_SIZE + write_off + MINDONE_RL_REC_SEQ, seq);
	wmb();
	rset32(MINDONE_RINGLOG_HDR_SIZE + write_off + MINDONE_RL_REC_STAGE,
	       ((u32)MINDONE_RL_CANARY << MINDONE_RL_CANARY_SHIFT) |
	       ((u32)stage & MINDONE_RL_STAGE_MASK));
	wmb();

	mindone_rl_advance(&seq);
out:
	raw_spin_unlock_irqrestore(&mindone_rl_lock, flags);
}
EXPORT_SYMBOL_GPL(mindone_ringlog_mark);

/* A string record reuses the same 16-byte slot stream: one header slot
 * (canary + MINDONE_RL_STRING_TAG + stage in bits[7:0]; reg0 = length)
 * followed by ceil(len/16) raw continuation slots. Best-effort, silently
 * truncates to MINDONE_RL_STR_MAX, never blocks/loops unbounded. */
#define MINDONE_RL_STR_MAX	240

void mindone_ringlog_str(enum mindone_ringlog_stage stage, const char *s, size_t len)
{
	unsigned long flags;
	u32 write_off, seq, hdr_word;
	size_t i, n;

	if (len > MINDONE_RL_STR_MAX)
		len = MINDONE_RL_STR_MAX;

	raw_spin_lock_irqsave(&mindone_rl_lock, flags);
	if (!mindone_rl_ensure_mapped())
		goto out;

	hdr_word = ((u32)MINDONE_RL_CANARY << MINDONE_RL_CANARY_SHIFT) |
		   ((u32)MINDONE_RL_STRING_TAG << MINDONE_RL_STRING_SHIFT) |
		   ((u32)stage & 0xff);

	write_off = rget32(MINDONE_RL_HDR_WRITE_OFF) & MINDONE_RINGLOG_BODY_MASK;
	seq = rget32(MINDONE_RL_HDR_SEQ);

	rset32(MINDONE_RINGLOG_HDR_SIZE + write_off + MINDONE_RL_REC_REG0, (u32)len);
	rset32(MINDONE_RINGLOG_HDR_SIZE + write_off + MINDONE_RL_REC_REG1, 0);
	rset32(MINDONE_RINGLOG_HDR_SIZE + write_off + MINDONE_RL_REC_SEQ, seq);
	wmb();
	rset32(MINDONE_RINGLOG_HDR_SIZE + write_off + MINDONE_RL_REC_STAGE, hdr_word);
	wmb();
	mindone_rl_advance(&seq);

	for (i = 0; i < len; i += MINDONE_RINGLOG_REC_SIZE) {
		n = len - i;
		if (n > MINDONE_RINGLOG_REC_SIZE)
			n = MINDONE_RINGLOG_REC_SIZE;

		write_off = rget32(MINDONE_RL_HDR_WRITE_OFF) & MINDONE_RINGLOG_BODY_MASK;

		/* FIX: this mapping is
		 * Device-nGnRnE, which FAULTS on unaligned accesses; memcpy()
		 * uses unaligned ldr/str for non-word lengths (confirmed
		 * Alignment Fault cause in QEMU). Copy strictly 4 bytes at a
		 * time via rset32() instead -- the cacheable SOURCE (s[])
		 * can stay unaligned, only the Device-mapped destination
		 * can't. */
		{
			unsigned int off = MINDONE_RINGLOG_HDR_SIZE + write_off;
			unsigned int k;

			for (k = 0; k < MINDONE_RINGLOG_REC_SIZE; k += 4) {
				u32 word = 0;
				unsigned int b;

				for (b = 0; b < 4 && k + b < n; b++)
					word |= (u32)(u8)s[i + k + b] << (8 * b);
				rset32(off + k, word);
			}
		}
		wmb();
		mindone_rl_advance(&seq);
	}
out:
	raw_spin_unlock_irqrestore(&mindone_rl_lock, flags);
}
EXPORT_SYMBOL_GPL(mindone_ringlog_str);

/* early_initcall console, mirroring the proven pattern in
 * drivers/misc/mindone_earlylog.c: CON_PRINTBUFFER makes the FIRST
 * write() callback replay the entire existing log_buf content in one or
 * more chunks, so its arrival timestamps "made it to early_initcall with
 * a working printk pipeline" -- the earliest safe, general point to hook
 * "first printk" without patching printk.c itself. We use the very first
 * chunk of replayed text as a free string-record sample.
 */
static void mindone_rl_console_write(struct console *co, const char *s, unsigned int count)
{
	if (!mindone_rl_first_printk_seen) {
		mindone_rl_first_printk_seen = true;
		mindone_ringlog_mark(MRL_FIRST_PRINTK, count, 0);
		mindone_ringlog_str(MRL_FIRST_PRINTK, s, count);
	}
}

static struct console mindone_rl_console = {
	.name	= "mindone-rlg",
	.write	= mindone_rl_console_write,
	.flags	= CON_PRINTBUFFER | CON_ENABLED | CON_ANYTIME,
	.index	= -1,
};

static int __init mindone_ringlog_console_init(void)
{
	register_console(&mindone_rl_console);
	return 0;
}
early_initcall(mindone_ringlog_console_init);

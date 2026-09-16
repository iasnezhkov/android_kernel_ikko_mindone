// SPDX-License-Identifier: GPL-2.0
/* MINDONE: Tier-1 early-boot console mirror -- a struct console
 * registered from an early_initcall, earlier than pstore's own backend.
 * Uses vmap()-of-existing-pages (ioremap/ioremap_wc WARN_ON/NULL on this
 * ordinary RAM window), same technique as persistent_ram_vmap(). Ring:
 * u32 magic/text_size/write_off/wrapped header, then a wrapping text
 * buffer. Full design: include/linux/mindone_earlylog.h. */
#include <linux/console.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/mindone_earlylog.h>

#define MINDONE_HDR_WORDS	4	/* magic, text_size, write_off, wrapped */
#define MINDONE_HDR_BYTES	(MINDONE_HDR_WORDS * sizeof(u32))

static void *mindone_base;		/* whole CONFIG_MINDONE_EARLYLOG_SIZE window */
static void *mindone_console_hdr;	/* = mindone_base + MINDONE_CONSOLE_OFF */
static void *mindone_console_text;	/* = mindone_console_hdr + MINDONE_HDR_BYTES */
static u32 mindone_text_size;		/* MINDONE_CONSOLE_SIZE - MINDONE_HDR_BYTES */
static DEFINE_RAW_SPINLOCK(mindone_lock);

static inline void mset32(void *base, unsigned int off, u32 val)
{
	*(volatile u32 *)(base + off) = val;
}

static inline u32 mget32(void *base, unsigned int off)
{
	return *(volatile u32 *)(base + off);
}

/*
 * Port of fs/pstore/ram_core.c::persistent_ram_vmap() / stock
 * log_store.c::remap_lowmem() -- vmap() the existing struct pages for
 * [start, start+size) with a non-cached prot, instead of ioremap(),
 * because this window is ordinary pfn_valid() System RAM (see file
 * header comment). VM_IOREMAP is set for the same reason
 * persistent_ram_vmap() sets it: keeps vread()/kmap_atomic() (kcore)
 * from tripping over this mapping.
 */
static void *mindone_vmap_ram(phys_addr_t start, size_t size)
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

	vaddr = vmap(pages, page_count, VM_MAP | VM_IOREMAP,
		     pgprot_noncached(PAGE_KERNEL));
	kfree(pages);
	if (!vaddr)
		return NULL;

	return vaddr + offset_in_page(start);
}

static void mindone_console_write(struct console *co, const char *s,
				   unsigned int count)
{
	unsigned long flags;
	u32 off, first, second;

	if (!mindone_console_text || !count)
		return;

	if (count > mindone_text_size)
		count = mindone_text_size;

	raw_spin_lock_irqsave(&mindone_lock, flags);

	off = mget32(mindone_console_hdr, 0x8);
	if (off >= mindone_text_size)
		off = 0;

	first = min_t(u32, count, mindone_text_size - off);
	memcpy(mindone_console_text + off, s, first);
	second = count - first;
	if (second) {
		memcpy(mindone_console_text, s + first, second);
		mset32(mindone_console_hdr, 0xc, 1);	/* wrapped */
	}

	off = (off + count) % mindone_text_size;
	mset32(mindone_console_hdr, 0x8, off);
	wmb();

	raw_spin_unlock_irqrestore(&mindone_lock, flags);
}

static struct console mindone_console = {
	.name	= "mindone-erl",
	.write	= mindone_console_write,
	.flags	= CON_PRINTBUFFER | CON_ENABLED | CON_ANYTIME,
	.index	= -1,
};

static int __init mindone_earlylog_console_init(void)
{
	u32 max_id;

	mindone_base = mindone_vmap_ram(MINDONE_EARLYLOG_BASE, MINDONE_EARLYLOG_SIZE);
	if (!mindone_base) {
		pr_notice("mindone_earlylog: vmap(0x%llx, 0x%llx) failed, no early console\n",
			  (unsigned long long)MINDONE_EARLYLOG_BASE,
			  (unsigned long long)MINDONE_EARLYLOG_SIZE);
		return -ENOMEM;
	}

	mindone_console_hdr = mindone_base + MINDONE_CONSOLE_OFF;
	mindone_console_text = mindone_console_hdr + MINDONE_HDR_BYTES;
	mindone_text_size = MINDONE_CONSOLE_SIZE - MINDONE_HDR_BYTES;

	/* Fresh header for THIS boot attempt -- expdb/LK will have already
	 * copied out whatever the previous attempt left here on its own
	 * abnormal-boot reset, so it is safe (and correct: we want THIS
	 * attempt's data, not a confusing mix) to start the ring over.
	 */
	mset32(mindone_console_hdr, 0x8, 0);		/* write_off */
	mset32(mindone_console_hdr, 0xc, 0);		/* wrapped */
	mset32(mindone_console_hdr, 0x4, mindone_text_size);
	mset32(mindone_console_hdr, 0x0, MINDONE_CONSOLE_MAGIC);
	wmb();

	register_console(&mindone_console);

	/* Stamp the Tier-2 checkpoint slot ourselves: early_ioremap's
	 * fixmap window is long gone by early_initcall time (setup_arch()
	 * returned ages ago), but we already have this whole window
	 * mapped, and the checkpoint sub-region is just the first
	 * MINDONE_CKPT_SIZE bytes of the same window.
	 */
	mset32(mindone_base, MINDONE_CKPT_OFF + 0x4, (u32)MINDONE_CKPT_CONSOLE_REGISTERED);
	max_id = mget32(mindone_base, MINDONE_CKPT_OFF + 0x8);
	if ((u32)MINDONE_CKPT_CONSOLE_REGISTERED > max_id)
		mset32(mindone_base, MINDONE_CKPT_OFF + 0x8, (u32)MINDONE_CKPT_CONSOLE_REGISTERED);
	mset32(mindone_base, MINDONE_CKPT_OFF + 0x0, MINDONE_CKPT_MAGIC);
	wmb();

	pr_notice("mindone_earlylog: console live at phys 0x%llx (text %u bytes)\n",
		  (unsigned long long)(MINDONE_EARLYLOG_BASE + MINDONE_CONSOLE_OFF),
		  mindone_text_size);
	return 0;
}
early_initcall(mindone_earlylog_console_init);

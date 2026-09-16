/* SPDX-License-Identifier: GPL-2.0 */
/* MINDONE: early-boot ring log -- a wrapping ring of timestamped,
 * register-carrying checkpoint records, written into the MIDDLE (not the
 * head, which proved unsafe) of the `aee_lk` physical region, independent
 * of printk/console/pstore and of whether the MMU is on. Four write tiers
 * (A: raw pre-MMU str, proven; B: staged; C: early_ioremap(); D: vmap()),
 * only Tier-A independently confirmed on this hardware so far. */
#ifndef __MINDONE_RINGLOG_H
#define __MINDONE_RINGLOG_H

#include <linux/types.h>

/* CONFIG_MINDONE_RINGLOG_BASE's default (0x50B00000) already IS
 * `aee_lk` head (0x50700000) + 0x400000 -- the offset is baked into the
 * Kconfig default directly (see drivers/misc/Kconfig) rather than kept
 * as a separate symbol, so every consumer (asm and C alike) only ever
 * needs to know one absolute address.
 */
#define MINDONE_RINGLOG_BASE   ((phys_addr_t)CONFIG_MINDONE_RINGLOG_BASE)     /* 0x50b00000: aee_lk + 0x400000 */
#define MINDONE_RINGLOG_SIZE   ((resource_size_t)CONFIG_MINDONE_RINGLOG_SIZE) /* 0x300000, 3 MiB reservation */

#define MINDONE_RINGLOG_HDR_SIZE   0x40        /* head reserved for magic/index */
#define MINDONE_RINGLOG_BODY_SIZE  0x200000    /* 2 MiB ring body, power of 2 -- fits well inside the 3 MiB reservation */
#define MINDONE_RINGLOG_BODY_MASK  (MINDONE_RINGLOG_BODY_SIZE - 1)
#define MINDONE_RINGLOG_REC_SIZE   16          /* bytes per record slot */

#define MINDONE_RINGLOG_MAGIC      0x524c4b31U /* "RLK1", little-endian on disk: 31 4b 4c 52 */
#define MINDONE_RINGLOG_VERSION    1U

/* header word offsets (within MINDONE_RINGLOG_BASE) */
#define MINDONE_RL_HDR_MAGIC        0x00
#define MINDONE_RL_HDR_VERSION      0x04
#define MINDONE_RL_HDR_WRITE_OFF    0x08   /* next record offset within body, wraps */
#define MINDONE_RL_HDR_WRAPPED      0x0c
#define MINDONE_RL_HDR_SEQ          0x10   /* monotonic global record counter */
#define MINDONE_RL_HDR_BOOT_COUNT   0x14   /* incremented once per instrumented boot */

/* record word offsets, relative to each 16-byte slot */
#define MINDONE_RL_REC_STAGE   0x0
#define MINDONE_RL_REC_REG0    0x4
#define MINDONE_RL_REC_REG1    0x8
#define MINDONE_RL_REC_SEQ     0xc

#define MINDONE_RL_CANARY_SHIFT   24
#define MINDONE_RL_CANARY         0xc7U    /* top byte of every valid stage word */
#define MINDONE_RL_STAGE_MASK     0xffffU  /* low 16 bits = stage id */
#define MINDONE_RL_STRING_SHIFT   16
#define MINDONE_RL_STRING_TAG     0xffU    /* byte 23:16 == this => string record */

/* stage IDs, in expected chronological order. 0 is deliberately unused. */
enum mindone_ringlog_stage {
    MRL_PRIMARY_ENTRY = 1,      /* primary_entry(): absolute first instruction, MMU off -- Tier-A, PROVEN mechanism */
    MRL_AFTER_PRESERVE_ARGS,    /* after bl preserve_boot_args, MMU off -- Tier-A, PROVEN mechanism */
    MRL_AFTER_MMU_STATE_EL,     /* after record_mmu_state()+init_kernel_el(), MMU still off -- Tier-A, PROVEN mechanism */
    MRL_AFTER_MMU_ON,           /* __primary_switched(): MMU on, BSS cleared, kernel image mapped -- Tier-B (staged; drained via Tier-C, UNPROVEN) */
    MRL_START_KERNEL,           /* start_kernel(): first C statement -- Tier-B (staged; drained via Tier-C, UNPROVEN) */
    MRL_SETUP_ARCH_ENTRY,       /* setup_arch(): early_ioremap_init() done, ring reachable -- Tier-C, UNPROVEN on this hardware */
    MRL_FDT_SCANNED,            /* setup_arch(): setup_machine_fdt() returned -- Tier-C, UNPROVEN */
    MRL_PAGING_INIT,            /* setup_arch(): paging_init() returned -- Tier-C, UNPROVEN */
    MRL_SETUP_ARCH_EXIT,        /* setup_arch(): about to call early_ioremap_reset() -- Tier-C, UNPROVEN */
    MRL_FIRST_PRINTK,           /* first replay through our early_initcall console -- Tier-D (vmap), UNPROVEN */
    MRL_BEFORE_KASLR_INIT,      /* __primary_switch(): immediately before bl __pi_kaslr_early_init -- Tier-B (staged) */
    MRL_AFTER_KASLR_INIT,       /* __primary_switch(): immediately after __pi_kaslr_early_init returns -- Tier-B (staged) */
    MRL_MAX,
};

/* Tier-A: raw physical writer, MMU-off only, implemented purely in
 * head.S (ringlog_mark_asm) -- the only tier with an independent
 * positive control on this hardware.
 * Tier-B: "staged" writer for the window between MMU-on and
 * early_ioremap_init(), where no DRAM-wide mapping exists yet. Appends
 * to a small static array, drained into the real ring once Tier-C
 * becomes available -- so its observable success still depends on
 * Tier-C. */
#ifdef CONFIG_MINDONE_RINGLOG

#define MINDONE_RINGLOG_STAGED_MAX  4

struct mindone_ringlog_staged_entry {
    u32 valid;
    u32 stage;
    u32 reg0;
    u32 reg1;
};

extern struct mindone_ringlog_staged_entry
    mindone_ringlog_staged[MINDONE_RINGLOG_STAGED_MAX];

/* C helper for Tier-B, callable from ordinary (mapped, MMU-on) C code
 * such as the very first line of start_kernel(). Not IRQ/SMP-safe by
 * design -- boot is strictly single-core and single-threaded at every
 * call site this is used from.
 */
void mindone_ringlog_stage_local(enum mindone_ringlog_stage stage, u32 reg0, u32 reg1);

/* Tier-C: setup_arch()-time writer via early_ioremap(); valid only
 * between early_ioremap_init() and early_ioremap_reset() inside
 * setup_arch(). Drains the Tier-B staging array on first call.
 */
void mindone_ringlog_arch_reserve(void);
void mindone_ringlog_arch_mark(enum mindone_ringlog_stage stage, u32 reg0, u32 reg1);

/* Tier-D: post-setup_arch writer via a long-lived vmap() mapping,
 * lazily created on first use. Safe from early_initcall onward.
 */
void mindone_ringlog_mark(enum mindone_ringlog_stage stage, u32 reg0, u32 reg1);
void mindone_ringlog_str(enum mindone_ringlog_stage stage, const char *s, size_t len);

#else
static inline void mindone_ringlog_stage_local(enum mindone_ringlog_stage stage, u32 reg0, u32 reg1) {}
static inline void mindone_ringlog_arch_reserve(void) {}
static inline void mindone_ringlog_arch_mark(enum mindone_ringlog_stage stage, u32 reg0, u32 reg1) {}
static inline void mindone_ringlog_mark(enum mindone_ringlog_stage stage, u32 reg0, u32 reg1) {}
static inline void mindone_ringlog_str(enum mindone_ringlog_stage stage, const char *s, size_t len) {}
#endif

#endif /* __MINDONE_RINGLOG_H */

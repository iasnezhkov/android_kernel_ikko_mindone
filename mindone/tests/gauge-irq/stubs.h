/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#define EPROBE_DEFER 517
#define bm_debug(...) ((void)0)
struct irq_desc { unsigned int depth; };
struct mtk_gauge { int irq_no[GAUGE_IRQ_MAX]; };
static struct irq_desc descriptor;
static bool descriptor_present = true;
static int lookup_calls, enable_calls, disable_calls;
static struct irq_desc *irq_to_desc(int number)
{
    assert(number == 25);
    lookup_calls++;
    return descriptor_present ? &descriptor : NULL;
}
static void enable_irq(int number)
{
    assert(number == 25 && descriptor.depth == 1);
    enable_calls++;
    descriptor.depth--;
}
static void disable_irq_nosync(int number)
{
    assert(number == 25 && descriptor.depth == 0);
    disable_calls++;
    descriptor.depth++;
}

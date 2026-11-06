/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t u32;
#define __iomem
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "CHECK failed line=%d: %s\n", __LINE__, #x); exit(1); } } while (0)
#define pr_debug(...) ((void)0)
#define WDOG_HW_RUNNING 0
@CONSTANTS@
struct device_node { bool disable_extrst, toprgu; };
struct device { struct device_node *of_node; };
struct watchdog_device { unsigned timeout; unsigned long status; void *data; };
struct mtk_wdt_dev {
    struct watchdog_device wdt_dev;
    void *wdt_base;
    bool disable_wdt_extrst, reset_by_toprgu;
};
static u32 registers[8];
static unsigned mode_writes, length_writes, kicks, parses, property_reads;
static void *watchdog_get_drvdata(struct watchdog_device *wdt) { return wdt->data; }
static void set_bit(unsigned bit, unsigned long *status) { *status |= 1UL << bit; }
static void clear_bit(unsigned bit, unsigned long *status) { *status &= ~(1UL << bit); }
static u32 readl(void *addr) { return *(u32 *)addr; }
static u32 ioread32(void *addr) { return readl(addr); }
static void writel(u32 value, void *addr)
{
    CHECK(addr == registers || addr == registers + 1 || addr == registers + 2);
    if (addr == registers) mode_writes++;
    if (addr == registers + 1) length_writes++;
    if (addr == registers + 2) kicks++;
    *(u32 *)addr = value;
}
static void iowrite32(u32 value, void *addr) { writel(value, addr); }
static void mtk_wdt_parse_dt(struct device_node *np, struct watchdog_device *wdt) { parses++; }
static bool of_property_read_bool(struct device_node *np, const char *name)
{
    property_reads++;
    if (!strcmp(name, "mediatek,disable-extrst")) return np->disable_extrst;
    CHECK(!strcmp(name, "mediatek,reset-by-toprgu"));
    return np->toprgu;
}
@FUNCTIONS@

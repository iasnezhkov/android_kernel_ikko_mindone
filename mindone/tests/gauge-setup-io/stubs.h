/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define GENMASK(high, low) (((~0U) >> (31 - (high))) & ((~0U) << (low)))
#define BIT(index) (1U << (index))
#define IS_ENABLED(option) 0
#define bm_debug(...) ((void)0)
#define bm_err(...) ((void)0)
#define PTR_ERR(pointer) ((int)(intptr_t)(pointer))
#define IS_ERR(pointer) ((uintptr_t)(pointer) >= (uintptr_t)-4095)
typedef uint8_t u8;
typedef uint32_t u32;
struct device { int unused; };
struct platform_device { struct device dev; };
struct mtk_battery { struct { int car_tune_value; } fg_cust_data; };
struct mtk_gauge {
    void *regmap;
    struct platform_device *pdev;
    struct mtk_battery *gm;
    struct { int r_fg_value, vbat2_det_counter, vbat2_det_time; } hw_status;
    bool vbat_l_en, vbat_h_en;
};
struct mtk_gauge_sysfs_field_info { int unused; };
struct nvmem_cell { int unused; };
static unsigned int registers[0x3000];
static bool irq_enabled[GAUGE_IRQ_MAX];
static int operations, failure_at, operation_errno, enables, disables;
static int enable_errno, disable_errno;
static int cell_get_errno, cell_read_errno, cell_write_errno, cell_reads, cell_writes, cell_puts;
static bool cell_short_read, cell_short_write, ambiguous_write;
static u8 rtc;
static struct nvmem_cell cell;
static int regmap_read(void *map, unsigned int address, unsigned int *value)
{
    operations++;
    if (operations == failure_at)
        return operation_errno;
    assert(address < 0x3000);
    *value = registers[address];
    return 0;
}
static int regmap_write(void *map, unsigned int address, unsigned int value)
{
    operations++;
    if (operations == failure_at && !ambiguous_write)
        return operation_errno;
    registers[address] = value;
    if (address == RG_FGADC_CON1)
        registers[address] = FG_LATCHDATA_ST_MASK;
    return operations == failure_at ? operation_errno : 0;
}
static int regmap_update_bits(void *map, unsigned int address, unsigned int mask, unsigned int value)
{
    operations++;
    if (operations == failure_at && !ambiguous_write)
        return operation_errno;
    registers[address] = (registers[address] & ~mask) | (value & mask);
    if (address == RG_FGADC_CON1)
        registers[address] &= ~FG_LATCHDATA_ST_MASK;
    return operations == failure_at ? operation_errno : 0;
}
static int disable_gauge_irq(struct mtk_gauge *gauge, enum gauge_irq irq)
{
    disables++;
    if (disable_errno)
        return disable_errno;
    irq_enabled[irq] = false;
    return 0;
}
static int enable_gauge_irq(struct mtk_gauge *gauge, enum gauge_irq irq)
{
    enables++;
    if (enable_errno)
        return enable_errno;
    irq_enabled[irq] = true;
    return 0;
}
static void mdelay(int delay) {}
static struct nvmem_cell *nvmem_cell_get(struct device *device, const char *name)
{
    assert(!strcmp(name, "state-of-charge"));
    return cell_get_errno ? (void *)(intptr_t)cell_get_errno : &cell;
}
static void *nvmem_cell_read(struct nvmem_cell *source, size_t *length)
{
    assert(source == &cell);
    cell_reads++;
    if (cell_read_errno)
        return (void *)(intptr_t)cell_read_errno;
    u8 *value = malloc(1);
    *value = rtc;
    *length = cell_short_read ? 0 : 1;
    return value;
}
static int nvmem_cell_write(struct nvmem_cell *source, void *value, size_t length)
{
    assert(source == &cell && length == 1);
    cell_writes++;
    if (cell_write_errno)
        return cell_write_errno;
    if (cell_short_write)
        return 0;
    rtc = *(u8 *)value;
    return 1;
}
static void nvmem_cell_put(struct nvmem_cell *source) { assert(source == &cell); cell_puts++; }
static void kfree(void *pointer) { free(pointer); }

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t u8;
struct device { int unused; };
struct platform_device { struct device dev; };
struct mtk_gauge { void *regmap; struct platform_device *pdev; };
struct mtk_gauge_sysfs_field_info { int unused; };
struct nvmem_cell { int unused; };
enum gauge_property {
	GAUGE_PROP_2SEC_REBOOT, GAUGE_PROP_PL_CHARGING_STATUS,
	GAUGE_PROP_MONITER_PLCHG_STATUS, GAUGE_PROP_BAT_PLUG_STATUS,
	GAUGE_PROP_IS_NVRAM_FAIL_MODE, GAUGE_PROP_MONITOR_SOFF_VALIDTIME,
	GAUGE_PROP_CON0_SOC
};
#define bm_err(...) mock_log(__VA_ARGS__)
#define bm_debug(...) mock_log(__VA_ARGS__)
#define IS_ERR(pointer) ((uintptr_t)(pointer) >= (uintptr_t)-4095)
#define PTR_ERR(pointer) ((int)(intptr_t)(pointer))
static int reg_error;
static unsigned int reg_value;
static int cell_get_error;
static int cell_read_error;
static size_t cell_length = 1;
static u8 cell_value;
static int cell_refs;
static int buffers;
static struct nvmem_cell cell;

static void mock_log(const char *format, ...)
{
}

static int regmap_read(void *map, int address, unsigned int *value)
{
	if (reg_error)
		return reg_error;
	*value = reg_value;
	return 0;
}

static int regmap_update_bits(void *map, int address, unsigned int mask, unsigned int value)
{
	return 0;
}

static void mdelay(int milliseconds)
{
}

static struct nvmem_cell *nvmem_cell_get(struct device *dev, const char *name)
{
	assert(strcmp(name, "state-of-charge") == 0);
	if (cell_get_error)
		return (void *)(intptr_t)cell_get_error;
	cell_refs++;
	return &cell;
}

static u8 *nvmem_cell_read(struct nvmem_cell *input, size_t *length)
{
	assert(input == &cell && cell_refs == 1);
	if (cell_read_error)
		return (void *)(intptr_t)cell_read_error;
	if (length)
		*length = cell_length;
	u8 *out = malloc(cell_length ? cell_length : 1);
	assert(out);
	if (cell_length)
		out[0] = cell_value;
	buffers++;
	return out;
}

static void nvmem_cell_put(struct nvmem_cell *input)
{
	assert(input == &cell && cell_refs == 1);
	cell_refs--;
}

static void kfree(void *pointer)
{
	assert(buffers == 1);
	buffers--;
	free(pointer);
}

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define READ_ONCE(value) (value)
#define WRITE_ONCE(value, new_value) ((value) = (new_value))
#define cmpxchg(pointer, old, new) (*(pointer) == (old) ? (*(pointer) = (new), (old)) : *(pointer))
#define ENOTSUPP EOPNOTSUPP
#define UNIT_TRANS_10 10
#define RG_FGADC_CON1 1
#define container_of(pointer, type, member) ((type *)((char *)(pointer) - offsetof(type, member)))
#define list_for_each(position, head) for (position = (head)->next; position != (head); position = position->next)
#define bm_err(...) mock_log(__VA_ARGS__)
#define bm_debug(...) mock_log(__VA_ARGS__)
#define clamp_t(type, value, lower, upper) ((type)(value) < (type)(lower) ? (type)(lower) : (type)(value) > (type)(upper) ? (type)(upper) : (type)(value))
#define lockdep_assert_held(lock) assert((lock)->held)

enum gauge_property {
	GAUGE_PROP_COULOMB,
	GAUGE_PROP_RESET,
	GAUGE_PROP_COULOMB_HT_INTERRUPT,
	GAUGE_PROP_COULOMB_LT_INTERRUPT,
	GAUGE_PROP_READONLY,
	GAUGE_PROP_COUNT,
};

enum battery_property {
	BAT_PROP_FG_RESET,
	BAT_PROP_READONLY,
	BAT_PROP_COUNT,
};

struct mutex {
	bool held;
	int rank;
};

struct list_head {
	struct list_head *next;
	struct list_head *prev;
};

struct device_attribute {
	int token;
};

struct device {
	void *data;
};

struct power_supply {
	void *data;
	int refs;
};

struct mtk_gauge;
struct mtk_battery;

struct mtk_gauge_sysfs_field_info {
	struct device_attribute attr;
	enum gauge_property prop;
	int (*set)(struct mtk_gauge *, struct mtk_gauge_sysfs_field_info *, int);
	int (*get)(struct mtk_gauge *, struct mtk_gauge_sysfs_field_info *, int *);
};

struct mtk_battery_sysfs_field_info {
	enum battery_property prop;
	int (*set)(struct mtk_battery *, struct mtk_battery_sysfs_field_info *, int);
};

struct mtk_battery_algo {
	bool active;
	int fg_c_d0_car;
	int prev_car_bat0;
	int zcv_learn_car;
	bool zcv_learn_valid;
	int sw_cycle_car;
	int sw_cycle_ncar;
	bool sw_cycle_valid;
	int car;
	int fg_c_d0_ocv;
	int fg_c_d0_dod;
	int fg_c_d0_soc;
	int fg_c_dod;
	int fg_c_soc;
	int quse_tb1;
	int aging_factor;
};

struct gauge_consumer {
	struct device *dev;
	char *name;
	long start;
	long end;
	int variable;
	struct list_head list;
};

struct mtk_coulomb_service {
	bool init;
	struct mutex coulomb_lock;
	struct mutex hw_coulomb_lock;
	unsigned long reset_coulomb;
	struct list_head coulomb_head_plus;
	struct list_head coulomb_head_minus;
};

struct fuel_gauge_table_custom_data { int unused; };
struct fuel_gauge_custom_data { int unused; };

struct mtk_battery {
	bool kernel_setup_in_progress, kernel_setup_mutated;
	int kernel_setup_error;
	struct mutex algo_lock;
	bool disableGM30;
	struct gauge_consumer coulomb_plus, coulomb_minus, uisoc_plus, uisoc_minus;
	int coulomb_int_gap, coulomb_int_ht, coulomb_int_lt, uisoc_int_ht_gap, uisoc_int_lt_gap;
	struct mtk_battery_algo algo;
	struct mtk_gauge *gauge;
	struct mtk_coulomb_service cs;
	struct { int car_diff; } log;
	int64_t sw_iavg_time;
	int sw_iavg_car;
	int bat_cycle_car;
	int bat_cycle;
	int model_capacity_mah;
	struct fuel_gauge_table_custom_data fg_table_cust_data;
	struct fuel_gauge_custom_data fg_cust_data;
};

struct mtk_gauge {
	struct mutex ops_lock;
	struct mtk_battery *gm;
	struct mtk_gauge_sysfs_field_info *attr;
	bool charge_counter_valid;
	int charge_counter_car;
	long long charge_counter_uah;
	int charge_counter_full_uah;
	int coulomb_car;
	void *regmap;
};

static int gauge_reset_set(struct mtk_gauge *, struct mtk_gauge_sysfs_field_info *, int);
static int battery_reset_set(struct mtk_battery *, struct mtk_battery_sysfs_field_info *, int);
int gauge_set_property(enum gauge_property, int);
int battery_set_property(enum battery_property, int);
int battery_reset_charge_locked(struct mtk_battery *);

static struct power_supply gauge_psy;
static struct power_supply battery_psy;
static bool missing_gauge_psy;
static bool missing_battery_psy;
static int hw_car;
static int car_reads;
static int read_error_at;
static bool post_read_error;
static int reset_error;
static int reset_increment;
static int hardware_writes;
static bool reset_done;
static int advance_read;
static int advance_delta;
static int daemon_cycle_calls;
static int wakes;
static int interrupt_values[GAUGE_PROP_COUNT];
static int interrupt_error, interrupt_calls, interrupt_error_at;
static bool interrupt_clears_setup;
static struct mutex *held_locks[8];
static int held_count;

static void mock_log(const char *format, ...)
{
}

static void mutex_lock(struct mutex *lock)
{
	assert(!lock->held);
	assert(held_count < 8);
	if (held_count)
		assert(held_locks[held_count - 1]->rank < lock->rank);
	lock->held = true;
	held_locks[held_count++] = lock;
}

static void mutex_unlock(struct mutex *lock)
{
	assert(lock->held && held_count > 0);
	assert(held_locks[held_count - 1] == lock);
	lock->held = false;
	held_count--;
}

static void init_head(struct list_head *head)
{
	head->next = head;
	head->prev = head;
}

static void add_tail(struct list_head *entry, struct list_head *head)
{
	entry->prev = head->prev;
	entry->next = head;
	head->prev->next = entry;
	head->prev = entry;
}

static bool list_empty(struct list_head *head)
{
	return head->next == head;
}
static void list_del_init(struct list_head *entry)
{
	entry->next->prev = entry->prev;
	entry->prev->next = entry->next;
	init_head(entry);
}
static void list_add(struct list_head *entry, struct list_head *head)
{
	entry->next = head->next;
	entry->prev = head;
	head->next->prev = entry;
	head->next = entry;
}
static const char *dev_name(struct device *device)
{
	return "fixture";
}

static struct power_supply *power_supply_get_by_name(const char *name)
{
	struct power_supply *psy;
	if (strcmp(name, "mtk-gauge") == 0) {
		if (missing_gauge_psy)
			return NULL;
		psy = &gauge_psy;
	} else {
		assert(strcmp(name, "battery") == 0);
		if (missing_battery_psy)
			return NULL;
		psy = &battery_psy;
	}
	psy->refs++;
	return psy;
}

static void *power_supply_get_drvdata(struct power_supply *psy)
{
	return psy->data;
}

static void power_supply_put(struct power_supply *psy)
{
	assert(psy->refs > 0);
	psy->refs--;
}

static void *dev_get_drvdata(struct device *dev)
{
	return dev->data;
}

static int kstrtoint(const char *buffer, unsigned int base, int *value)
{
	char *end;
	long parsed = strtol(buffer, &end, base);
	if (end == buffer || (*end && strcmp(end, "\n") != 0))
		return -EINVAL;
	*value = parsed;
	return 0;
}

static int coulomb_get(struct mtk_gauge *gauge,
	struct mtk_gauge_sysfs_field_info *attr, int *value)
{
	lockdep_assert_held(&gauge->ops_lock);
	*value = gauge->coulomb_car;
	car_reads++;
	if (car_reads == read_error_at || (post_read_error && reset_done))
		return -EIO;
	if (car_reads == advance_read)
		hw_car += advance_delta;
	*value = hw_car;
	gauge->coulomb_car = hw_car;
	return 0;
}

static int interrupt_set(struct mtk_gauge *gauge,
	struct mtk_gauge_sysfs_field_info *attr, int value)
{
	lockdep_assert_held(&gauge->ops_lock);
	interrupt_values[attr->prop] = value;
	if (interrupt_clears_setup) gauge->gm->kernel_setup_in_progress = false;
	interrupt_calls++;
	return !interrupt_error_at || interrupt_calls == interrupt_error_at ? interrupt_error : 0;
}

static int regmap_update_bits(void *regmap, int reg, int mask, int value)
{
	struct mtk_gauge *gauge = regmap;
	lockdep_assert_held(&gauge->ops_lock);
	assert(reg == RG_FGADC_CON1 && mask == 0x0600 && value == 0x1f00);
	hardware_writes++;
	if (reset_error)
		return reset_error;
	hw_car = reset_increment;
	reset_done = true;
	return 0;
}

static void mdelay(int duration)
{
	assert(duration == 1);
}

static void fg_sw_bat_cycle_accu(struct mtk_battery *gm)
{
	daemon_cycle_calls++;
}

static int mtk_battery_get_learned_q_max(struct mtk_battery *gm)
{
	return gm->model_capacity_mah;
}

static int64_t ktime_get_boottime(void)
{
	return 123456;
}

static void gauge_coulomb_dump_list(struct mtk_battery *gm)
{
}

static void wake_up_gauge_coulomb(struct mtk_battery *gm)
{
	wakes++;
	mutex_lock(&gm->cs.hw_coulomb_lock);
	assert(gauge_set_property(GAUGE_PROP_COULOMB_HT_INTERRUPT, 300) == 0);
	assert(gauge_set_property(GAUGE_PROP_COULOMB_LT_INTERRUPT, 300) == 0);
	mutex_unlock(&gm->cs.hw_coulomb_lock);
}

static int OCV_to_DOD_c(struct mtk_battery *gm, int voltage)
{
	assert(voltage >= 34000 && voltage <= 44000);
	return 44000 - voltage;
}

static void fgr_update_quse(struct mtk_battery *gm, int table)
{
	assert(table == 1 && gm->algo.quse_tb1 > 0);
}

static struct mtk_gauge_sysfs_field_info gauge_attrs[GAUGE_PROP_COUNT] = {
	[GAUGE_PROP_COULOMB] = { .prop = GAUGE_PROP_COULOMB, .get = coulomb_get },
	[GAUGE_PROP_RESET] = { .prop = GAUGE_PROP_RESET, .set = gauge_reset_set },
	[GAUGE_PROP_COULOMB_HT_INTERRUPT] = { .prop = GAUGE_PROP_COULOMB_HT_INTERRUPT, .set = interrupt_set },
	[GAUGE_PROP_COULOMB_LT_INTERRUPT] = { .prop = GAUGE_PROP_COULOMB_LT_INTERRUPT, .set = interrupt_set },
	[GAUGE_PROP_READONLY] = { .prop = GAUGE_PROP_READONLY },
};

static struct mtk_battery_sysfs_field_info battery_sysfs_field_tbl[BAT_PROP_COUNT] = {
	[BAT_PROP_FG_RESET] = { .prop = BAT_PROP_FG_RESET, .set = battery_reset_set },
	[BAT_PROP_READONLY] = { .prop = BAT_PROP_READONLY },
};

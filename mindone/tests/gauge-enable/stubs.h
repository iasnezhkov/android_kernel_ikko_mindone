#define smp_load_acquire(pointer) (*(pointer))
#define smp_store_release(pointer, value) (*(pointer) = (value))
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WRITE_ONCE(value, new_value) ((value) = (new_value))
#define bm_err(...) mock_log(__VA_ARGS__)

enum {
	FG_INTR_FG_TIME,
	FG_INTR_BAT_INT1_HT,
	FG_INTR_BAT_INT1_LT,
	FG_INTR_BAT_INT2_HT,
	FG_INTR_BAT_INT2_LT,
	FG_INTR_BAT_TIME_INT,
	FG_INTR_IAVG,
	FG_INTR_VBAT2_L,
	FG_INTR_VBAT2_H,
	FG_INTR_BAT_INT1_CHECK,
	FG_INTR_BAT_INT2_CHECK,
};

struct mutex {
	bool held;
};

struct mtk_battery {
	int init_flag;
	struct mutex algo_lock;
	struct {
		bool active;
		int aging_factor;
		int ui_soc;
		bool zcv_learn_valid;
		int zcv_learn_car;
		bool sw_cycle_valid;
		int sw_cycle_ncar;
	} algo;
	int ui_soc;
	int bat_cycle;
	int published_soc;
};

static int initializations;
static int publications;
static int interrupts;

static void mock_log(const char *format, ...)
{
}

static void mutex_lock(struct mutex *lock)
{
	assert(!lock->held);
	lock->held = true;
}

static void mutex_unlock(struct mutex *lock)
{
	assert(lock->held);
	lock->held = false;
}

static int battery_kernel_setup_ready(struct mtk_battery *gm)
{
	return 0;
}

static int battery_algo_init(struct mtk_battery *gm)
{
	assert(gm->algo_lock.held && !gm->algo.active);
	initializations++;
	gm->algo.aging_factor = 10000;
	gm->algo.ui_soc = 8000;
	gm->ui_soc = 80;
	return 0;
}

void wakeup_fg_algo(struct mtk_battery *gm, int interrupt)
{
	assert(!gm->algo_lock.held);
	interrupts++;
}

static void battery_update(struct mtk_battery *gm)
{
	assert(!gm->algo_lock.held);
	publications++;
	gm->published_soc = gm->ui_soc;
}

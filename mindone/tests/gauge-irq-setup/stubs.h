#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define EPROBE_DEFER 517
#define EREMOTEIO 121
#define EUCLEAN 117
#define cmpxchg(pointer, old, new) (*(pointer) == (old) ? (*(pointer) = (new), (old)) : *(pointer))
#define READ_ONCE(value) (value)
#define WRITE_ONCE(value, new_value) ((value) = (new_value))
#define smp_load_acquire(pointer) (*(pointer))
#define smp_store_release(pointer, value) (*(pointer) = (value))
#define GFP_KERNEL 0
#define IRQF_ONESHOT 1
#define IRQF_TRIGGER_HIGH 2
#define IRQF_NO_AUTOEN 4
#define ALARM_BOOTTIME 0
#define BMLOG_ERROR_LEVEL 3
#define POWER_SUPPLY_TYPE_UNKNOWN 0
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define bm_err(...) ((void)0)
#define bm_debug(...) ((void)0)
#define pr_err(...) ((void)0)
#define IS_ENABLED(option) 0
#define PTR_ERR(pointer) ((int)(intptr_t)(pointer))
#define IS_ERR(pointer) ((uintptr_t)(pointer) >= (uintptr_t)-4095)
#define IS_ERR_OR_NULL(pointer) (!(pointer) || IS_ERR(pointer))
#define INIT_LIST_HEAD(pointer) (*(pointer) = 1)
#define INIT_WORK(...) ((void)0)
#define spin_lock_init(pointer) (*(pointer) = 1)
#define init_waitqueue_head(pointer) (*(pointer) = 1)
#define atomic_set(pointer, value) (*(pointer) = (value))
#define alarm_init(...) ((void)0)
typedef int irqreturn_t;
typedef irqreturn_t (*irq_handler_t)(int, void *);
struct irq_desc { unsigned int depth; };
struct device { struct device *parent; void *data; int kobj; void *of_node; };
struct platform_device { struct device dev; };
struct mtk_battery;
struct mtk_gauge_sysfs_field_info { int unused; };
struct notifier_block { int (*notifier_call)(struct notifier_block *, unsigned long, void *); };
struct wakeup_source { int unused; };
struct task_struct { int unused; };
struct power_supply { struct device dev; };
struct power_supply_desc {
    const char *name;
    int type;
    const int *properties;
    unsigned int num_properties;
    int (*get_property)(void);
    int (*set_property)(void);
};
struct power_supply_config { void *drv_data; };
struct mt6397_chip { void *regmap; };
struct iio_channel { int unused; };
struct attribute_group { int unused; };
struct mutex { int initialized; bool held; };
struct gauge_consumer { int (*callback)(struct mtk_battery *); };
struct mtk_coulomb_service {
    struct mtk_battery *gm;
    int coulomb_head_plus, coulomb_head_minus;
    struct mutex coulomb_lock, hw_coulomb_lock;
    int slock, wait_que, in_sleep;
    struct wakeup_source *wlock;
    struct notifier_block pm_nb;
    int pre_coulomb;
    bool init;
};
struct mtk_gauge {
    int irq_no[GAUGE_IRQ_MAX];
    bool irq_requested[GAUGE_IRQ_MAX];
    struct platform_device *pdev;
    struct mtk_battery *gm;
    struct mt6397_chip *chip;
    void *regmap;
    struct mutex ops_lock;
    struct iio_channel *chan_bat_temp, *chan_bat_voltage, *chan_bif;
    struct iio_channel *chan_ptim_bat_voltage, *chan_ptim_r;
    struct { int car_tune_value, r_fg_value; } hw_status;
    const int *attr;
    struct power_supply_desc psy_desc;
    struct power_supply_config psy_cfg;
    struct power_supply *psy;
    int (*sw_nafg_irq)(struct mtk_battery *);
    int (*sw_vbat_h_irq)(struct mtk_battery *);
    int (*sw_vbat_l_irq)(struct mtk_battery *);
};
struct battery_data { struct power_supply *psy; int bat_batt_temp; };
struct fuel_gauge_custom_data { int car_tune_value; };
struct mtk_battery {
    struct mtk_gauge *gauge;
    struct mtk_coulomb_service cs;
    struct { bool active; } algo;
    int init_flag, kernel_setup_error, kernel_service_error;
    void *mtk_battery_sk;
    bool disableGM30, disable_bs_psy, in_sleep, is_probe_done;
    int fixed_bat_tmp, log_level, sw_iavg_gap, wait_que;
    const int *tmp_table;
    struct mutex fg_update_lock, algo_lock;
    struct gauge_consumer coulomb_plus, coulomb_minus, uisoc_plus, uisoc_minus;
    int tracking_timer, one_percent_timer, sw_uisoc_timer;
    int tracking_timer_work, one_percent_timer_work, sw_uisoc_timer_work;
    struct notifier_block pm_nb;
    struct battery_data bs_data;
    struct fuel_gauge_custom_data fg_cust_data;
    int pl_two_sec_reboot, bat_cycle_car;
    void (*shutdown)(struct mtk_battery *);
    int (*suspend)(struct mtk_battery *, int);
    int (*resume)(struct mtk_battery *);
    struct power_supply *bms_psy;
};
enum { GAUGE_PROP_HW_VERSION, GAUGE_PROP_COULOMB, GAUGE_PROP_2SEC_REBOOT };
enum { HWCID_ADDR = 123 };
static const int fg_temp_table[] = {0}, gauge_properties[] = {0};
static const int mt6358_sysfs_field_tbl[] = {0};
static const struct attribute_group mt6358_sysfs_attr_group, battery_daemon_attr_group;
static struct irq_desc descriptors[GAUGE_IRQ_MAX];
static struct wakeup_source wakelock;
static struct task_struct task;
static struct iio_channel channel;
static struct power_supply battery_supply, gauge_supply, bms_supply;
static void *allocations[8];
static int allocation_count;
static int request_calls, free_calls, requested_live;
static int request_fail_at, request_errno, car_errno, hw_errno, hw_id;
static int battery_thread_errno, battery_notifier_errno, battery_sysfs_errno;
static int files_errno, wake_fail, notifier_errno, thread_fail, battery_psy_errno;
static int gauge_psy_errno, group_errno, netlink_errno, mapping_errno;
static int wake_live, notifier_live, worker_live, battery_worker_live, file_groups;
static int thermal_live, supply_live, gauge_groups, netlink_live, misc_calls, activation_calls;
static bool kernel_algo;
static struct mtk_gauge *active_gauge;
static int hw_version_get(struct mtk_gauge *, struct mtk_gauge_sysfs_field_info *, int *);
static void mutex_init(struct mutex *lock) { lock->initialized = 1; }
static void mutex_lock(struct mutex *lock) { assert(!lock->held); lock->held = true; }
static void mutex_unlock(struct mutex *lock) { assert(lock->held); lock->held = false; }
static struct irq_desc *irq_to_desc(int number)
{
    return number >= 100 && number < 100 + GAUGE_IRQ_MAX ? &descriptors[number - 100] : NULL;
}
static void enable_irq(int number) { irq_to_desc(number)->depth--; }
static void disable_irq_nosync(int number) { irq_to_desc(number)->depth++; }
static int devm_request_threaded_irq(struct device *device, int number,
    irq_handler_t top, irq_handler_t threaded, int flags, const char *name, void *data)
{
    struct mtk_battery *gm = data;
    assert(device == &gm->gauge->pdev->dev && !top && threaded && name);
    assert(flags == (IRQF_ONESHOT | IRQF_TRIGGER_HIGH | IRQF_NO_AUTOEN));
    assert(irq_to_desc(number));
    request_calls++;
    if (request_calls == request_fail_at)
        return request_errno;
    requested_live++;
    descriptors[number - 100].depth = 1;
    return 0;
}
static void devm_free_irq(struct device *device, int number, void *data)
{
    struct mtk_battery *gm = data;
    assert(device == &gm->gauge->pdev->dev && gm->gauge->irq_requested[number - 100]);
    assert(requested_live > 0);
    free_calls++;
    requested_live--;
}
static int regmap_read(void *regmap, int address, int *value)
{
    assert(address == HWCID_ADDR);
    if (hw_errno)
        return hw_errno;
    *value = hw_id;
    return 0;
}
static int gauge_get_property(int property, int *value)
{
    if (property == GAUGE_PROP_HW_VERSION)
        return hw_version_get(active_gauge, NULL, value);
    assert(property == GAUGE_PROP_COULOMB);
    if (car_errno)
        return car_errno;
    *value = 456;
    return 0;
}
static int gauge_get_int_property(int property) { return 0; }
static int gauge_set_property(int property, int value) { return 0; }
static struct wakeup_source *wakeup_source_register(struct device *device, const char *name)
{
    if (wake_fail)
        return NULL;
    wake_live++;
    return &wakelock;
}
static void wakeup_source_unregister(struct wakeup_source *source)
{
    assert(source == &wakelock && wake_live == 1 && worker_live == 0);
    wake_live--;
}
static int register_pm_notifier(struct notifier_block *notifier)
{
    if (active_gauge && notifier == &active_gauge->gm->pm_nb && battery_notifier_errno)
        return battery_notifier_errno;
    if (notifier_errno)
        return notifier_errno;
    notifier_live++;
    return 0;
}
static void unregister_pm_notifier(struct notifier_block *notifier)
{
    assert(notifier_live > 0 && worker_live == 0);
    notifier_live--;
}
static int gauge_coulomb_thread(void *data) { return 0; }
static int battery_update_routine(void *data) { return 0; }
static struct task_struct *kthread_run(int (*entry)(void *), void *data, const char *name)
{
    if (entry == gauge_coulomb_thread) {
        if (thread_fail)
            return (void *)(intptr_t)-EAGAIN;
        struct mtk_coulomb_service *cs = data;
        assert(cs->init && cs->wlock && requested_live == 12);
        worker_live++;
    } else {
        assert(entry == battery_update_routine && worker_live == 1);
        if (battery_thread_errno) return (void *)(intptr_t)battery_thread_errno;
        battery_worker_live++;
    }
    return &task;
}
static int system_pm_notify(struct notifier_block *nb, unsigned long event, void *data) { return 0; }
static irqreturn_t coulomb_irq(int irq, void *data) { return 0; }
static irqreturn_t zcv_irq(int irq, void *data) { return 0; }
static irqreturn_t vbat_h_irq(int irq, void *data) { return 0; }
static irqreturn_t vbat_l_irq(int irq, void *data) { return 0; }
static irqreturn_t nafg_irq(int irq, void *data) { return 0; }
static irqreturn_t bat_plugout_irq(int irq, void *data) { return 0; }
static irqreturn_t cycle_irq(int irq, void *data) { return 0; }
static irqreturn_t iavg_h_irq(int irq, void *data) { return 0; }
static irqreturn_t iavg_l_irq(int irq, void *data) { return 0; }
static irqreturn_t bat_temp_irq(int irq, void *data) { return 0; }
static int nafg_irq_handler(struct mtk_battery *gm) { return 0; }
static int sw_vbat_h_irq_handler(struct mtk_battery *gm) { return 0; }
static int sw_vbat_l_irq_handler(struct mtk_battery *gm) { return 0; }
static bool is_daemon_support(struct mtk_battery *gm) { return true; }
static void mtk_battery_shutdown(struct mtk_battery *gm) {}
static int mtk_battery_suspend(struct mtk_battery *gm, int state) { return 0; }
static int mtk_battery_resume(struct mtk_battery *gm) { return 0; }
static void sw_iavg_init(struct mtk_battery *gm) {}
static int devm_device_add_group(struct device *device, const struct attribute_group *group)
{
    assert(group == &battery_daemon_attr_group);
    if (files_errno)
        return files_errno;
    file_groups++;
    return 0;
}
static void *dev_get_drvdata(struct device *device) { return device->data; }
static void dev_set_drvdata(struct device *device, void *value) { device->data = value; }
static void fg_check_bootmode(struct device *device, struct mtk_battery *gm) {}
static void fg_check_lk_swocv(struct device *device, struct mtk_battery *gm) {}
static void fg_custom_init_from_header(struct mtk_battery *gm) {}
static void fg_custom_init_from_dts(struct platform_device *pdev, struct mtk_battery *gm) {}
static int fg_coulomb_int_h_handler(struct mtk_battery *gm) { return 0; }
static int fg_coulomb_int_l_handler(struct mtk_battery *gm) { return 0; }
static int fg_bat_int2_h_handler(struct mtk_battery *gm) { return 0; }
static int fg_bat_int2_l_handler(struct mtk_battery *gm) { return 0; }
static void gauge_coulomb_consumer_init(struct gauge_consumer *consumer, struct device *device, char *name) {}
static void fg_drv_thread_hrtimer_init(struct mtk_battery *gm) {}
static int battery_sysfs_create_group(struct power_supply *supply) { return battery_sysfs_errno; }
static int force_get_tbat(struct mtk_battery *gm, bool force) { return 25; }
static void mtk_power_misc_init(struct mtk_battery *gm) { misc_calls++; }
static bool is_recovery_mode(void) { return false; }
static int battery_algo_init(struct mtk_battery *gm) { activation_calls++; return 0; }
static struct power_supply *power_supply_get_by_name(const char *name) { return &bms_supply; }
static struct iio_channel *devm_iio_channel_get(struct device *device, const char *name) { return &channel; }
static void *devm_kzalloc(struct device *device, size_t size, int flags)
{
    assert(allocation_count < 8);
    return allocations[allocation_count++] = calloc(1, size);
}
static int platform_get_irq_byname(struct platform_device *pdev, const char *name)
{
    static const char *names[] = { "COULOMB_H", "COULOMB_L", "VBAT_H", "VBAT_L", "NAFG", "BAT_OUT", "ZCV", "FG_N_CHARGE_L", "FG_IAVG_H", "FG_IAVG_L", "BAT_TMP_H", "BAT_TMP_L" };
    for (int i = 0; i < GAUGE_IRQ_MAX; i++)
        if (!strcmp(name, names[i]))
            return mapping_errno ? mapping_errno : 100 + i;
    abort();
}
static int dev_err_probe(struct device *device, int error, const char *format, int index) { return error; }
static int battery_psy_init(struct platform_device *pdev)
{
    if (battery_psy_errno)
        return battery_psy_errno;
    struct mtk_gauge *gauge = pdev->dev.data;
    struct mtk_battery *gm = devm_kzalloc(&pdev->dev, sizeof(*gm), 0);
    gauge->gm = gm;
    active_gauge = gauge;
    gm->gauge = gauge;
    gm->bs_data.psy = &battery_supply;
    thermal_live++;
    supply_live++;
    return 0;
}
static int psy_gauge_get_property(void) { return 0; }
static int psy_gauge_set_property(void) { return 0; }
static struct power_supply *power_supply_register(struct device *device, struct power_supply_desc *desc, struct power_supply_config *config)
{
    if (gauge_psy_errno)
        return (void *)(intptr_t)gauge_psy_errno;
    supply_live++;
    return &gauge_supply;
}
static void power_supply_unregister(struct power_supply *supply)
{
    assert(supply && !IS_ERR(supply) && supply_live > 0 && worker_live == 0);
    supply_live--;
}
static int mt6358_sysfs_create_group(struct mtk_gauge *gauge)
{
    if (group_errno)
        return group_errno;
    gauge_groups++;
    return 0;
}
static void sysfs_remove_group(int *kobj, const struct attribute_group *group)
{
    assert(group == &mt6358_sysfs_attr_group && gauge_groups == 1);
    gauge_groups--;
}
static int initial_set(struct mtk_gauge *gauge, void *attr, int value) { return 0; }
static int bat_create_netlink(struct platform_device *pdev)
{
    if (netlink_errno)
        return netlink_errno;
    struct mtk_gauge *gauge = pdev->dev.data;
    gauge->gm->mtk_battery_sk = gauge;
    netlink_live++;
    return 0;
}
static void netlink_kernel_release(void *socket)
{
    assert(socket && netlink_live == 1 && worker_live == 0);
    netlink_live--;
}
static int adc_cali_cdev_init(struct platform_device *pdev) { return 0; }
static void battery_psy_unregister_thermal(struct battery_data *data)
{
    assert(thermal_live == 1 && worker_live == 0);
    thermal_live--;
}

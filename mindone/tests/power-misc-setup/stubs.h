/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#include <stddef.h>
struct blocking_notifier_head { struct notifier_block *head; int rwsem; };
static struct blocking_notifier_head power_supply_notifier;
enum { SYSTEM_BOOTING, SYSTEM_RUNNING };
static int system_state = SYSTEM_RUNNING;
static int misc_thread_errno, misc_thread_calls, misc_worker_live, misc_wakes;
static int misc_registered, early_activation, early_activation_ret;
static int misc_publications;
static struct mtk_battery *complete_during_acquire;
#define unlikely(value) (value)
#define WARN(condition, ...) ((void)(condition))
#define rcu_assign_pointer(pointer, value) ((pointer) = (value))
#define trace_notifier_register(pointer) (misc_registered++)
#define BATTERY_SHUTDOWN_TEMPERATURE 60
#define NOTIFY_DONE 0
#define smp_load_acquire(pointer) probe_acquire(pointer)
#define container_of(pointer, type, field) ((type *)((char *)(pointer) - offsetof(type, field)))
static void down_write(int *lock) { assert(!*lock); *lock = 1; }
static void up_write(int *lock) { assert(*lock); *lock = 0; }
static int power_misc_routine_thread(void *data) { return 0; }
static int battery_enable_kernel_algo(struct mtk_battery *gm);
static struct mtk_battery *get_mtk_battery(void) { return active_gauge ? active_gauge->gm : NULL; }
static void wake_up(int *queue) { assert(*queue == 1); misc_wakes++; }
static void battery_update(struct mtk_battery *gm) { assert(gm->algo.active && gm->init_flag == 1); misc_publications++; }
static bool probe_acquire(bool *done)
{
    if (complete_during_acquire) {
        assert(done == &complete_during_acquire->is_probe_done);
        complete_during_acquire->kernel_service_error = -EINTR;
        complete_during_acquire->is_probe_done = true;
        complete_during_acquire = NULL;
    }
    return *done;
}
static struct task_struct *misc_kthread(int (*entry)(void *), void *data, const char *name)
{
    struct mtk_battery *gm = data;
    assert(entry == power_misc_routine_thread && strcmp(name, "power_misc_thread") == 0);
    assert(gm->sdc.lock.initialized && gm->sdc.wait_que == 1);
    misc_thread_calls++;
    if (misc_thread_errno)
        return (void *)(intptr_t)misc_thread_errno;
    misc_worker_live++;
    return &task;
}

/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
typedef uint32_t u32;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "CHECK failed line=%d: %s\n", __LINE__, #x); exit(1); } } while (0)
#define chr_err(...) ((void)0)
#define MAX_ALG_NO 2
#define EVENT_RECHARGE 2
#define EVENT_DISCHARGE 3
#define smp_load_acquire(p) (*(p))
struct eta6965_device { int unused; };
struct power_supply { int unused; };
struct eta6965_platform { bool active; struct eta6965_device *eta; struct power_supply *psy; };
struct mutex { bool held; unsigned locks, unlocks; };
static struct mutex eta6965_binding_lock;
static struct eta6965_platform *eta6965_otg_binding;
static unsigned changed, enables, alg_stops, srcu_locks, srcu_unlocks;
static int enable_result;
static struct power_supply *expected_psy;
static void mutex_lock(struct mutex *lock) { CHECK(!lock->held); lock->held = true; lock->locks++; }
static void mutex_unlock(struct mutex *lock) { CHECK(lock->held); lock->held = false; lock->unlocks++; }
static void power_supply_changed(struct power_supply *psy)
{
    CHECK(eta6965_binding_lock.held && psy == expected_psy);
    changed++;
}
struct charger_device;
struct charger_ops { int (*event)(struct charger_device *, u32, u32); };
struct charger_device { struct eta6965_device *data; const struct charger_ops *ops; struct { int srcu; } evt_nh; };
static void *charger_get_data(struct charger_device *dev) { return dev->data; }
static int srcu_read_lock(int *srcu) { CHECK(*srcu == 0); *srcu = 1; srcu_locks++; return 7; }
static void srcu_read_unlock(int *srcu, int index) { CHECK(*srcu == 1 && index == 7); *srcu = 0; srcu_unlocks++; }
struct chg_alg_device { int unused; };
struct mtk_charger { struct charger_device *chg1_dev; struct chg_alg_device *alg[MAX_ALG_NO]; };
static int chg_alg_stop_algo(struct chg_alg_device *alg) { alg_stops++; return 0; }
static int charger_dev_enable(struct charger_device *dev, bool en)
{
    CHECK(en);
    enables++;
    return enable_result;
}
@FUNCTIONS@

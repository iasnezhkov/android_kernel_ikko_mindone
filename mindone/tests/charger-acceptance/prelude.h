#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define WARN_ON(a) (a)
#define IS_ERR(a) ((uintptr_t)(a) >= (uintptr_t)-4095)
#define IS_ERR_OR_NULL(a) (!(a) || IS_ERR(a))
#define PTR_ERR(a) ((int)(intptr_t)(a))
#define PAGE_SIZE 4096
#define DEFINE_MUTEX(name) struct fixture_mutex name = { .native = PTHREAD_MUTEX_INITIALIZER }
#define dev_info(...) ((void)0)
#define pr_info(...) ((void)0)
#define pr_notice(...) ((void)0)
#define chr_err(...) ((void)0)
#define chr_debug(...) ((void)0)
typedef int atomic_t;
typedef uint32_t u32;
struct device { int unused; };
struct kobject { int unused; };
struct kobj_attribute { int unused; };
struct fixture_mutex { pthread_mutex_t native; pthread_t owner; bool held; };
#if CHECKED_REQUESTS
static pthread_mutex_t sequence_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sequence_changed = PTHREAD_COND_INITIALIZER;
static _Thread_local bool observe_lock;
static bool provider_paused, provider_entered, provider_release, waiter_blocked, waiter_done;
static void sequence_wait(void) {
    struct timespec deadline;
    assert(!clock_gettime(CLOCK_REALTIME, &deadline));
    deadline.tv_sec += 3;
    assert(!pthread_cond_timedwait(&sequence_changed, &sequence_lock, &deadline));
}
static void mutex_lock(struct fixture_mutex *mutex) {
    int ret = pthread_mutex_trylock(&mutex->native);
    if (ret == EBUSY) {
        if (observe_lock) {
            assert(!pthread_mutex_lock(&sequence_lock));
            waiter_blocked = true;
            assert(!pthread_cond_broadcast(&sequence_changed));
            assert(!pthread_mutex_unlock(&sequence_lock));
        }
        assert(!pthread_mutex_lock(&mutex->native));
    } else {
        assert(!ret);
    }
    mutex->owner = pthread_self();
    mutex->held = true;
}
static void mutex_unlock(struct fixture_mutex *mutex) {
    assert(mutex->held && pthread_equal(mutex->owner, pthread_self()));
    mutex->held = false;
    assert(!pthread_mutex_unlock(&mutex->native));
}
#endif
struct thermal_cooling_device { void *devdata; };
struct charger_ops;
struct charger_device { const struct charger_ops *ops; int cc, iin, cc_errno, iin_errno; };
struct chg_alg_device { int state; };
#define ALG_RUNNING 1
#define EPROBE_DEFER 517

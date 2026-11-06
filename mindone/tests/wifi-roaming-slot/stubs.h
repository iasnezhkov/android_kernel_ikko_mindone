/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "CHECK_FAIL: %s at %d scenario=%u\n", #x, __LINE__, scenario); exit(2); } } while (0)
#define PARAM_MAC_ADDR_LEN 6
#define MAC_ADDR_LEN 6
#define GLUE_FLAG_HALT_BIT 0
#define GLUE_FLAG_OID_BIT 2
#define GLUE_FLAG_FWK_BLACKLIST_BIT 6
#define CFG_SUPPORT_MULTITHREAD 1
#define WLAN_STATUS_FAILURE 1
#define IN
#define DBGLOG(...) ((void)0)
#define BUILD_BUG_ON(x) _Static_assert(!(x), "build contract")
#define READ_ONCE(x) __atomic_load_n(&(x), __ATOMIC_SEQ_CST)
#define STORE(x, value) __atomic_store_n(&(x), value, __ATOMIC_SEQ_CST)
#define offsetofend(type, member) (offsetof(type, member) + sizeof(((type *)0)->member))
static unsigned scenario, initialization_count;
static int main_actor, caller_actor, role_actor, hif_actor, rx_actor;
static _Thread_local void *tls_current;
static _Thread_local unsigned atomic_context;
#define current tls_current
#define in_interrupt() (atomic_context & 1)
#define irqs_disabled() (atomic_context & 2)
#define in_atomic() (atomic_context & 4)
static unsigned long virtual_time;
static bool resetting;
#define jiffies READ_ONCE(virtual_time)
#define msecs_to_jiffies(ms) ((unsigned long)(ms))
#define time_after_eq(a, b) ((long)((unsigned long)(a) - (unsigned long)(b)) >= 0)
static bool kalIsResetting(void) { return READ_ONCE(resetting); }
static bool test_bit(unsigned bit, unsigned long *flags) { return !!(__atomic_load_n(flags, __ATOMIC_SEQ_CST) & (1ul << bit)); }
static void set_bit(unsigned bit, unsigned long *flags) { __atomic_fetch_or(flags, 1ul << bit, __ATOMIC_SEQ_CST); }
static void clear_bit(unsigned bit, unsigned long *flags) { __atomic_fetch_and(flags, ~(1ul << bit), __ATOMIC_SEQ_CST); }
static bool test_and_clear_bit(unsigned bit, unsigned long *flags) { return !!(__atomic_fetch_and(flags, ~(1ul << bit), __ATOMIC_SEQ_CST) & (1ul << bit)); }
struct mutex { pthread_mutex_t native; void *owner; unsigned cookie; };
static void mutex_init(struct mutex *mutex)
{
    CHECK(pthread_mutex_init(&mutex->native, NULL) == 0);
    STORE(mutex->owner, NULL);
    mutex->cookie = ++initialization_count;
}
static void mutex_lock(struct mutex *mutex)
{
    CHECK(READ_ONCE(mutex->owner) != current);
    CHECK(pthread_mutex_lock(&mutex->native) == 0);
    STORE(mutex->owner, current);
}
static bool mutex_trylock(struct mutex *mutex)
{
    int result = pthread_mutex_trylock(&mutex->native);
    CHECK(result == 0 || result == EBUSY);
    if (result) return false;
    STORE(mutex->owner, current);
    return true;
}
static void mutex_unlock(struct mutex *mutex)
{
    CHECK(READ_ONCE(mutex->owner) == current);
    STORE(mutex->owner, NULL);
    CHECK(pthread_mutex_unlock(&mutex->native) == 0);
}
#define lockdep_assert_held(mutex) CHECK(READ_ONCE((mutex)->owner) == current)
struct completion { pthread_mutex_t lock; pthread_cond_t condition; unsigned count, waiters, cookie; };
static pthread_mutex_t event_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t event_condition = PTHREAD_COND_INITIALIZER;
static void notify_event(void)
{
    pthread_mutex_lock(&event_lock);
    pthread_cond_broadcast(&event_condition);
    pthread_mutex_unlock(&event_lock);
}
static void init_completion(struct completion *completion)
{
    CHECK(pthread_mutex_init(&completion->lock, NULL) == 0);
    CHECK(pthread_cond_init(&completion->condition, NULL) == 0);
    completion->count = completion->waiters = 0;
    completion->cookie = ++initialization_count;
}
static void reinit_completion(struct completion *completion)
{
    pthread_mutex_lock(&completion->lock);
    CHECK(READ_ONCE(completion->waiters) == 0);
    completion->count = 0;
    pthread_mutex_unlock(&completion->lock);
}
static void signal_completion(struct completion *completion, bool all)
{
    pthread_mutex_lock(&completion->lock);
    if (all) completion->count = UINT_MAX;
    else if (completion->count != UINT_MAX) completion->count++;
    pthread_cond_broadcast(&completion->condition);
    pthread_mutex_unlock(&completion->lock);
    notify_event();
}
#define complete(completion) signal_completion(completion, false)
#define complete_all(completion) signal_completion(completion, true)
static bool completion_done(struct completion *completion)
{
    pthread_mutex_lock(&completion->lock);
    bool done = completion->count != 0;
    pthread_mutex_unlock(&completion->lock);
    return done;
}
static unsigned long wait_for_completion_timeout(struct completion *completion, unsigned long timeout)
{
    unsigned long deadline = jiffies + timeout;
    pthread_mutex_lock(&completion->lock);
    __atomic_add_fetch(&completion->waiters, 1, __ATOMIC_SEQ_CST);
    notify_event();
    while (!completion->count && !time_after_eq(jiffies, deadline))
        CHECK(pthread_cond_wait(&completion->condition, &completion->lock) == 0);
    bool done = completion->count != 0;
    if (done && completion->count != UINT_MAX) completion->count--;
    __atomic_sub_fetch(&completion->waiters, 1, __ATOMIC_SEQ_CST);
    pthread_mutex_unlock(&completion->lock);
    notify_event();
    return done;
}
#include "driver-slot-types.inc"
struct ADAPTER { int id; };
struct GL_IO_REQ { int rStatus; };
struct GLUE_INFO {
    uint32_t u4ReadyFlag, u4FWRoamingEnable;
    struct ADAPTER *prAdapter;
    void *main_thread, *hif_thread, *rx_thread;
    unsigned long ulFlag;
    unsigned waitq, waitq_hif, waitq_rx;
    struct completion rHaltComp, rHifHaltComp, rRxHaltComp, rPendComp;
    struct GL_IO_REQ OidEntry;
    struct GL_FWK_BLACKLIST_SLOT rFwkBlacklist;
    uint64_t session_tail;
};
static struct GLUE_INFO glue;
static struct ADAPTER adapters[2] = {{1}, {2}};
static pthread_t owner_thread;
static pthread_mutex_t dispatch_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t dispatch_condition = PTHREAD_COND_INITIALIZER;
static bool dispatch_allowed, owner_started, owner_exited;
static void wake_up_interruptible(unsigned *queue)
{
    CHECK(queue == &glue.waitq || queue == &glue.waitq_hif || queue == &glue.waitq_rx);
    pthread_mutex_lock(&dispatch_lock);
    pthread_cond_broadcast(&dispatch_condition);
    pthread_mutex_unlock(&dispatch_lock);
}
static unsigned pause_mask, released_mask, phase;
static pthread_mutex_t gate_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_condition = PTHREAD_COND_INITIALIZER;
static int policy, active_plans, prepare_calls, prepare_error, last_adapter;
static bool malformed_prepare;
enum { PREPARE = 1, COMMIT = 2, ABORT = 4, ROLE = 8 };
static void gate(unsigned at)
{
    STORE(phase, at);
    notify_event();
    pthread_mutex_lock(&gate_lock);
    while ((READ_ONCE(pause_mask) & at) && !(READ_ONCE(released_mask) & at))
        pthread_cond_wait(&gate_condition, &gate_lock);
    pthread_mutex_unlock(&gate_lock);
}
static void release_gate(unsigned at)
{
    pthread_mutex_lock(&gate_lock);
    __atomic_fetch_or(&released_mask, at, __ATOMIC_SEQ_CST);
    pthread_cond_broadcast(&gate_condition);
    pthread_mutex_unlock(&gate_lock);
}
struct AIS_FWK_BLACKLIST_PLAN { int next; };
static int aisPrepareFwkBlacklist(struct ADAPTER *adapter, uint32_t count,
    const uint8_t bssid[][MAC_ADDR_LEN], struct AIS_FWK_BLACKLIST_PLAN **output)
{
    CHECK(current == &main_actor && READ_ONCE(glue.rFwkBlacklist.state_lock.owner) != current);
    __atomic_add_fetch(&prepare_calls, 1, __ATOMIC_SEQ_CST);
    STORE(last_adapter, adapter->id);
    if (READ_ONCE(prepare_error)) return READ_ONCE(prepare_error);
    if (READ_ONCE(malformed_prepare)) return 0;
    struct AIS_FWK_BLACKLIST_PLAN *plan = malloc(sizeof(*plan));
    CHECK(plan != NULL);
    plan->next = count;
    for (unsigned i = 0; i < count; i++) plan->next += bssid[i][5];
    __atomic_add_fetch(&active_plans, 1, __ATOMIC_SEQ_CST);
    gate(PREPARE);
    *output = plan;
    return 0;
}
static void aisCommitFwkBlacklist(struct AIS_FWK_BLACKLIST_PLAN *plan)
{
    CHECK(current == &main_actor && plan);
    lockdep_assert_held(&glue.rFwkBlacklist.state_lock);
    STORE(policy, plan->next);
    gate(COMMIT);
    free(plan);
    __atomic_sub_fetch(&active_plans, 1, __ATOMIC_SEQ_CST);
}
static void aisAbortFwkBlacklist(struct AIS_FWK_BLACKLIST_PLAN *plan)
{
    CHECK(current == &main_actor && plan && READ_ONCE(glue.rFwkBlacklist.state_lock.owner) != current);
    gate(ABORT);
    free(plan);
    __atomic_sub_fetch(&active_plans, 1, __ATOMIC_SEQ_CST);
}
static void wlanOffWaitWlanThreads(struct completion *completion, void *thread)
{
    if (thread == &main_actor) {
        CHECK(pthread_join(owner_thread, NULL) == 0);
        CHECK(completion_done(completion));
    } else {
        CHECK(thread == &hif_actor || thread == &rx_actor);
        complete_all(completion);
    }
}

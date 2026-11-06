// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
struct call { int result; bool finished; uint32_t count; uint8_t bssid[16][6]; };
static struct call request;
static bool role_finished, role_mutated;
static int role_result;
static void wait_until(bool (*predicate)(void))
{
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += 5;
    pthread_mutex_lock(&event_lock);
    while (!predicate())
        CHECK(pthread_cond_timedwait(&event_condition, &event_lock, &deadline) == 0);
    pthread_mutex_unlock(&event_lock);
}
static bool entered(void) { return READ_ONCE(owner_started); }
static bool caller_waiting(void) { return READ_ONCE(glue.rFwkBlacklist.done.waiters) != 0; }
static bool caller_left_wait(void) { return !READ_ONCE(glue.rFwkBlacklist.done.waiters) && !READ_ONCE(request.finished); }
static bool caller_finished(void) { return READ_ONCE(request.finished); }
static bool role_waiting(void) { return READ_ONCE(glue.rFwkBlacklist.owner_idle.waiters) != 0; }
static bool role_done(void) { return READ_ONCE(role_finished); }
static bool prepared(void) { return READ_ONCE(phase) == PREPARE; }
static bool committing(void) { return READ_ONCE(phase) == COMMIT; }
static bool aborting(void) { return READ_ONCE(phase) == ABORT; }
static void advance(unsigned long ticks)
{
    __atomic_add_fetch(&virtual_time, ticks, __ATOMIC_SEQ_CST);
    struct completion *completions[] = {&glue.rFwkBlacklist.done, &glue.rFwkBlacklist.owner_idle, &glue.rPendComp};
    for (unsigned i = 0; i < 3; i++) {
        pthread_mutex_lock(&completions[i]->lock);
        pthread_cond_broadcast(&completions[i]->condition);
        pthread_mutex_unlock(&completions[i]->lock);
    }
}
static void *owner(void *unused)
{
    (void)unused;
    tls_current = &main_actor;
    CHECK(kalFwkBlacklistOwnerStart(&glue) == 0);
    STORE(owner_started, true);
    notify_event();
    for (;;) {
        pthread_mutex_lock(&dispatch_lock);
        while (!test_bit(GLUE_FLAG_HALT_BIT, &glue.ulFlag) &&
               (!test_bit(GLUE_FLAG_FWK_BLACKLIST_BIT, &glue.ulFlag) || !READ_ONCE(dispatch_allowed)))
            pthread_cond_wait(&dispatch_condition, &dispatch_lock);
        bool halt = test_bit(GLUE_FLAG_HALT_BIT, &glue.ulFlag);
        pthread_mutex_unlock(&dispatch_lock);
        if (halt) break;
        if (test_and_clear_bit(GLUE_FLAG_FWK_BLACKLIST_BIT, &glue.ulFlag))
            kalFwkBlacklistProcess(&glue);
    }
    kalFwkBlacklistOwnerDrain(&glue);
    complete_all(&glue.rHaltComp);
    STORE(owner_exited, true);
    notify_event();
    return NULL;
}
static void *caller(void *unused)
{
    (void)unused;
    tls_current = &caller_actor;
    request.result = kalFwkBlacklistSubmit(&glue, request.count, request.bssid);
    STORE(request.finished, true);
    notify_event();
    return NULL;
}
static void *role(void *unused)
{
    (void)unused;
    tls_current = &role_actor;
    role_result = kalFwkBlacklistRoleBegin(&glue);
    if (!role_result) {
        CHECK(READ_ONCE(active_plans) == 0);
        STORE(role_mutated, true);
        gate(ROLE);
        role_result = kalFwkBlacklistRoleEnd(&glue);
    }
    STORE(role_finished, true);
    notify_event();
    return NULL;
}
static void setup(void)
{
    memset(&glue, 0, sizeof(glue));
    memset(&request, 0, sizeof(request));
    tls_current = &caller_actor;
    atomic_context = 0;
    virtual_time = 0;
    resetting = false;
    owner_started = owner_exited = false;
    dispatch_allowed = true;
    pause_mask = released_mask = phase = 0;
    policy = 17;
    active_plans = prepare_calls = prepare_error = last_adapter = 0;
    malformed_prepare = false;
    role_finished = role_mutated = false;
    role_result = 0;
    glue.session_tail = UINT64_MAX;
    glue.prAdapter = &adapters[0];
    glue.u4ReadyFlag = 1;
    glue.u4FWRoamingEnable = 1;
    glue.main_thread = &main_actor;
    glue.hif_thread = &hif_actor;
    glue.rx_thread = &rx_actor;
    init_completion(&glue.rHaltComp);
    init_completion(&glue.rHifHaltComp);
    init_completion(&glue.rRxHaltComp);
    init_completion(&glue.rPendComp);
    kalFwkBlacklistInit(&glue);
    request.count = 2;
    request.bssid[0][5] = 3;
    request.bssid[1][5] = 5;
}
static void start(void)
{
    CHECK(pthread_create(&owner_thread, NULL, owner, NULL) == 0);
    wait_until(entered);
}
static void dispatch(void)
{
    STORE(dispatch_allowed, true);
    wake_up_interruptible(&glue.waitq);
}
static void stop(void)
{
    kalFwkBlacklistClose(&glue);
    STORE(glue.u4ReadyFlag, 0);
    set_bit(GLUE_FLAG_HALT_BIT, &glue.ulFlag);
    wake_up_interruptible(&glue.waitq);
    wlanOffWaitWlanThreads(&glue.rHaltComp, glue.main_thread);
    kalFwkBlacklistCallerDrain(&glue);
    CHECK(!glue.rFwkBlacklist.owner_live && completion_done(&glue.rFwkBlacklist.owner_idle));
    CHECK(READ_ONCE(active_plans) == 0);
}
static void destroy_completion(struct completion *completion)
{
    CHECK(!READ_ONCE(completion->waiters));
    CHECK(pthread_mutex_destroy(&completion->lock) == 0);
    CHECK(pthread_cond_destroy(&completion->condition) == 0);
}
static void cleanup(void)
{
    CHECK(READ_ONCE(active_plans) == 0);
    destroy_completion(&glue.rFwkBlacklist.done);
    destroy_completion(&glue.rFwkBlacklist.owner_idle);
    destroy_completion(&glue.rHaltComp);
    destroy_completion(&glue.rHifHaltComp);
    destroy_completion(&glue.rRxHaltComp);
    destroy_completion(&glue.rPendComp);
    CHECK(pthread_mutex_destroy(&glue.rFwkBlacklist.caller_lock.native) == 0);
    CHECK(pthread_mutex_destroy(&glue.rFwkBlacklist.state_lock.native) == 0);
    scenario++;
}
int main(void)
{
    alarm(25);
    setup();
    CHECK(kalFwkBlacklistSubmit(&glue, request.count, request.bssid) == -ESHUTDOWN);
    bool enabled = true;
    CHECK(kalFwkBlacklistGetEnabled(&glue, &enabled) == -ESHUTDOWN && enabled);
    CHECK(kalFwkBlacklistSubmit(NULL, 0, NULL) == -EINVAL);
    CHECK(kalFwkBlacklistSubmit(&glue, 17, request.bssid) == -EINVAL);
    CHECK(kalFwkBlacklistSubmit(&glue, 1, NULL) == -EINVAL);
    start();
    CHECK(kalFwkBlacklistGetEnabled(&glue, &enabled) == 0 && enabled);
    STORE(glue.u4FWRoamingEnable, 0);
    CHECK(kalFwkBlacklistGetEnabled(&glue, &enabled) == 0 && !enabled);
    CHECK(kalFwkBlacklistSubmit(&glue, 0, NULL) == 0 && prepare_calls == 0);
    STORE(glue.u4FWRoamingEnable, 1);
    for (unsigned bit = 1; bit <= 4; bit <<= 1) {
        atomic_context = bit;
        CHECK(kalFwkBlacklistSubmit(&glue, 0, NULL) == -EWOULDBLOCK);
    }
    atomic_context = 0;
    tls_current = &main_actor;
    CHECK(kalFwkBlacklistSubmit(&glue, 0, NULL) == -EDEADLK);
    tls_current = &caller_actor;
    CHECK(kalFwkBlacklistSubmit(&glue, request.count, request.bssid) == 0);
    CHECK(policy == 10 && active_plans == 0 && prepare_calls == 1);
    stop(); cleanup();

    setup(); start();
    STORE(prepare_error, -ENOMEM);
    CHECK(kalFwkBlacklistSubmit(&glue, request.count, request.bssid) == -ENOMEM);
    CHECK(policy == 17 && active_plans == 0);
    STORE(prepare_error, 0); STORE(malformed_prepare, true);
    CHECK(kalFwkBlacklistSubmit(&glue, request.count, request.bssid) == -EIO);
    CHECK(policy == 17 && active_plans == 0);
    stop(); cleanup();

    setup(); STORE(dispatch_allowed, false); start();
    pthread_t call_thread;
    CHECK(pthread_create(&call_thread, NULL, caller, NULL) == 0);
    wait_until(caller_waiting);
    advance(5000);
    wait_until(caller_finished);
    CHECK(pthread_join(call_thread, NULL) == 0);
    CHECK(request.result == -ETIMEDOUT && policy == 17);
    CHECK(!completion_done(&glue.rFwkBlacklist.owner_idle));
    CHECK(kalFwkBlacklistSubmit(&glue, 0, NULL) == -EBUSY);
    dispatch();
    CHECK(wait_for_completion_timeout(&glue.rFwkBlacklist.owner_idle, 5000));
    CHECK(policy == 17 && prepare_calls == 0);
    CHECK(kalFwkBlacklistSubmit(&glue, request.count, request.bssid) == 0);
    CHECK(policy == 10);
    stop(); cleanup();

    setup(); STORE(dispatch_allowed, false); start();
    CHECK(pthread_create(&call_thread, NULL, caller, NULL) == 0);
    wait_until(caller_waiting);
    kalFwkBlacklistClose(&glue);
    STORE(glue.u4ReadyFlag, 0);
    set_bit(GLUE_FLAG_HALT_BIT, &glue.ulFlag);
    wake_up_interruptible(&glue.waitq);
    wait_until(caller_finished);
    CHECK(pthread_join(call_thread, NULL) == 0 && request.result == -ESHUTDOWN);
    wlanOffWaitWlanThreads(&glue.rHaltComp, glue.main_thread);
    kalFwkBlacklistCallerDrain(&glue);
    CHECK(policy == 17 && prepare_calls == 0 && completion_done(&glue.rFwkBlacklist.owner_idle));
    cleanup();

    setup(); STORE(pause_mask, PREPARE); start();
    CHECK(pthread_create(&call_thread, NULL, caller, NULL) == 0);
    wait_until(prepared); wait_until(caller_waiting);
    advance(5000);
    wait_until(caller_finished);
    CHECK(pthread_join(call_thread, NULL) == 0);
    CHECK(request.result == -ETIMEDOUT && active_plans == 1 && policy == 17);
    CHECK(!completion_done(&glue.rFwkBlacklist.owner_idle));
    memset(request.bssid, 0xcc, sizeof(request.bssid));
    CHECK(kalFwkBlacklistSubmit(&glue, 0, NULL) == -EBUSY);
    release_gate(PREPARE);
    CHECK(wait_for_completion_timeout(&glue.rFwkBlacklist.owner_idle, 5000));
    CHECK(policy == 17 && active_plans == 0);
    stop(); cleanup();

    setup(); STORE(pause_mask, COMMIT); start();
    CHECK(pthread_create(&call_thread, NULL, caller, NULL) == 0);
    wait_until(committing); wait_until(caller_waiting);
    advance(5000);
    wait_until(caller_left_wait);
    CHECK(!READ_ONCE(request.finished) && policy == 10);
    release_gate(COMMIT);
    wait_until(caller_finished);
    CHECK(pthread_join(call_thread, NULL) == 0 && request.result == 0);
    stop(); cleanup();

    setup(); STORE(pause_mask, PREPARE | ABORT | ROLE); start();
    CHECK(pthread_create(&call_thread, NULL, caller, NULL) == 0);
    wait_until(prepared); wait_until(caller_waiting); advance(5000);
    wait_until(caller_finished); CHECK(pthread_join(call_thread, NULL) == 0);
    pthread_t role_thread;
    CHECK(pthread_create(&role_thread, NULL, role, NULL) == 0);
    wait_until(role_waiting);
    release_gate(PREPARE); wait_until(aborting);
    CHECK(!READ_ONCE(role_mutated) && active_plans == 1);
    release_gate(ABORT);
    while (!READ_ONCE(role_mutated)) usleep(1000);
    CHECK(active_plans == 0 && policy == 17);
    CHECK(kalFwkBlacklistSubmit(&glue, 0, NULL) == -EBUSY);
    release_gate(ROLE); wait_until(role_done);
    CHECK(pthread_join(role_thread, NULL) == 0 && role_result == 0);
    stop(); cleanup();

    setup(); STORE(pause_mask, PREPARE); start();
    CHECK(pthread_create(&call_thread, NULL, caller, NULL) == 0);
    wait_until(prepared); wait_until(caller_waiting); advance(5000);
    wait_until(caller_finished); CHECK(pthread_join(call_thread, NULL) == 0);
    CHECK(pthread_create(&role_thread, NULL, role, NULL) == 0);
    wait_until(role_waiting); advance(5000); wait_until(role_done);
    CHECK(pthread_join(role_thread, NULL) == 0 && role_result == -EBUSY);
    CHECK(!role_mutated && !glue.rFwkBlacklist.role_blocked && glue.rFwkBlacklist.cancelled);
    CHECK(kalFwkBlacklistSubmit(&glue, 0, NULL) == -EBUSY);
    release_gate(PREPARE);
    CHECK(wait_for_completion_timeout(&glue.rFwkBlacklist.owner_idle, 5000));
    CHECK(active_plans == 0 && policy == 17);
    stop(); cleanup();

    setup(); STORE(pause_mask, PREPARE); start();
    CHECK(pthread_create(&call_thread, NULL, caller, NULL) == 0);
    wait_until(prepared); wait_until(caller_waiting);
    kalFwkBlacklistClose(&glue);
    wait_until(caller_finished); CHECK(pthread_join(call_thread, NULL) == 0);
    CHECK(request.result == -ESHUTDOWN && !completion_done(&glue.rFwkBlacklist.owner_idle));
    release_gate(PREPARE);
    CHECK(wait_for_completion_timeout(&glue.rFwkBlacklist.owner_idle, 5000));
    CHECK(policy == 17 && active_plans == 0);
    stop(); cleanup();

    setup(); start();
    CHECK(kalFwkBlacklistRoleBegin(&glue) == 0);
    CHECK(kalFwkBlacklistSubmit(&glue, 0, NULL) == -EBUSY);
    kalFwkBlacklistClose(&glue);
    CHECK(kalFwkBlacklistRoleEnd(&glue) == -ESHUTDOWN);
    CHECK(kalFwkBlacklistSubmit(&glue, 0, NULL) == -ESHUTDOWN);
    stop(); cleanup();

    setup(); start(); stop();
    unsigned saved_initializations = initialization_count;
    unsigned caller_cookie = glue.rFwkBlacklist.caller_lock.cookie;
    unsigned state_cookie = glue.rFwkBlacklist.state_lock.cookie;
    unsigned done_cookie = glue.rFwkBlacklist.done.cookie;
    unsigned idle_cookie = glue.rFwkBlacklist.owner_idle.cookie;
    destroy_completion(&glue.rHaltComp); destroy_completion(&glue.rHifHaltComp);
    destroy_completion(&glue.rRxHaltComp); destroy_completion(&glue.rPendComp);
    CHECK(kalFwkBlacklistResetGlue(&glue) == 0);
    CHECK(!glue.u4ReadyFlag && !glue.prAdapter && !glue.main_thread && !glue.session_tail);
    enabled = true;
    CHECK(!glue.u4FWRoamingEnable && kalFwkBlacklistGetEnabled(&glue, &enabled) == -ESHUTDOWN && enabled);
    CHECK(initialization_count == saved_initializations);
    CHECK(glue.rFwkBlacklist.caller_lock.cookie == caller_cookie && glue.rFwkBlacklist.state_lock.cookie == state_cookie);
    CHECK(glue.rFwkBlacklist.done.cookie == done_cookie && glue.rFwkBlacklist.owner_idle.cookie == idle_cookie);
    CHECK(kalFwkBlacklistSubmit(&glue, 0, NULL) == -ESHUTDOWN);
    init_completion(&glue.rHaltComp); init_completion(&glue.rHifHaltComp);
    init_completion(&glue.rRxHaltComp); init_completion(&glue.rPendComp);
    glue.prAdapter = &adapters[1]; glue.main_thread = &main_actor;
    glue.hif_thread = &hif_actor; glue.rx_thread = &rx_actor;
    glue.u4ReadyFlag = 1;
    glue.u4FWRoamingEnable = 1; STORE(owner_started, false);
    start();
    CHECK(kalFwkBlacklistResetGlue(&glue) == -EBUSY);
    CHECK(kalFwkBlacklistSubmit(&glue, request.count, request.bssid) == 0 && last_adapter == 2);
    stop(); cleanup();
    printf("PASS %u owned roaming slot scenarios\n", scenario);
    return 0;
}

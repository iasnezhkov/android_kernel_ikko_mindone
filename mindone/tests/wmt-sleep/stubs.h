static INT32 opfunc_get_consys_state(P_WMT_OP op);
static INT32 consys_dump_osc_state(P_CONSYS_STATE state);
static INT32 consys_dump_gating_state(P_CONSYS_STATE state);
INT32 consys_sleep_info_read_ctrl(WMT_SLEEP_COUNT_TYPE type, PUINT64 counter, PUINT64 timer);
VOID osal_op_raise_signal(P_OSAL_OP op, INT32 result);
static INT32 wmt_lib_dmp_consys_state(P_CONSYS_STATE_DMP_INFO info, unsigned int times, unsigned int delay);

struct consys_state_dmp_req { CONSYS_STATE_DMP_OP consys_ops[WMT_LIB_DMP_SLOT]; atomic_t version; };
typedef struct {
    unsigned long state;
    struct consys_state_dmp_req state_dmp_req;
    int rActiveOpQ, rFreeOpQ, rWmtdWq, thread, worker_thread;
} DEV_WMT, *P_DEV_WMT;
static DEV_WMT gDevWmt;
static struct { int eDrvStatus[1]; } gMtkWmtCtx;
static int bank_rc[WMT_SLEEP_COUNT_MAX], bank_partial, bank_zero, provider_calls;
static int provider_order[WMT_SLEEP_COUNT_MAX], readable, pcr_calls, sleeps;
static int osc_override, gating_override, spin_depth, sleep_depth, lock_failure;
static int g_sleep_counter_enable, g_sleep_counter_spinlock;
static int put_failure, no_free, no_completion, coredump, copy_failure;
static OSAL_OP host_op;
static char logs[16384], actions[16384];
static unsigned long conn_read_count, conn_write_count;
static struct { uintptr_t mcu_conn_hif_on_base, mcu_cfg_on_base, mcu_top_misc_on_base; } conn_reg;
static UINT32 registers[4096];

static void log_message(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(logs + strlen(logs), sizeof(logs) - strlen(logs), format, args);
    va_end(args);
}
static void action(char code) { size_t end = strlen(actions); assert(end + 1 < sizeof(actions)); actions[end] = code; actions[end + 1] = 0; }
#define WMT_PLAT_PR_INFO(...) log_message(__VA_ARGS__)
#define WMT_INFO_FUNC(...) log_message(__VA_ARGS__)
#define WMT_WARN_FUNC(...) log_message(__VA_ARGS__)
#define WMT_ERR_FUNC(...) log_message(__VA_ARGS__)
#define WMT_PLAT_PR_DBG(...) ((void)0)
#define WMT_DBG_FUNC(...) ((void)0)
#define pr_warn(...) log_message(__VA_ARGS__)
#define osal_sprintf(buffer, ...) sprintf((char *)(buffer), __VA_ARGS__)
#define osal_memset memset
#define osal_assert assert
#define atomic64_read(pointer) (*(pointer))
#define atomic_set(pointer, value) (*(pointer) = (value))
#define atomic_inc(pointer) (++*(pointer))
#define atomic_dec(pointer) (--*(pointer))
#define atomic_dec_and_test(pointer) (--*(pointer) == 0)
#define atomic_add_return(value, pointer) (*(pointer) += (value))
static void osal_lock_unsleepable_lock(int *lock) { (void)lock; assert(spin_depth++ == 0); }
static void osal_unlock_unsleepable_lock(int *lock) { (void)lock; assert(--spin_depth == 0); }
static int osal_lock_sleepable_lock(int *lock) { if (lock_failure) return -1; (*lock)++; sleep_depth++; return 0; }
static int osal_trylock_sleepable_lock(int *lock) { if (*lock) return 0; (*lock)++; sleep_depth++; return 1; }
static void osal_unlock_sleepable_lock(int *lock) { assert(*lock > 0); (*lock)--; assert(--sleep_depth >= 0); }
static int osal_test_bit(int bit, unsigned long *state) { return (*state >> bit) & 1; }
static int wmt_lib_get_drv_status(int type) { assert(type == WMTDRV_TYPE_WMT); return gMtkWmtCtx.eDrvStatus[type]; }
static int mtk_consys_check_reg_readable(void) { action('R'); return readable; }
static UINT32 wmt_plat_read_cpupcr(void) { action('P'); pcr_calls++; return 42; }
static void osal_get_local_time(UINT64 *seconds, ULONG *nanoseconds) { *seconds = 1; *nanoseconds = 2; }
static void osal_sleep_ms(int delay) { assert(delay > 0); action('D'); sleeps++; }
static int mtk_consys_dump_osc_state(P_CONSYS_STATE state) { action('O'); return osc_override != INT_MIN ? osc_override : consys_dump_osc_state(state); }
static int mtk_wcn_consys_dump_gating_state(P_CONSYS_STATE state) { action('G'); return gating_override != INT_MIN ? gating_override : consys_dump_gating_state(state); }
static UINT32 reg_read(uintptr_t address) { assert(address && address / 4 < 4096); conn_read_count++; action('r'); return registers[address / 4]; }
static void reg_write(uintptr_t address, UINT32 value) { assert(address && address / 4 < 4096); conn_write_count++; action('w'); registers[address / 4] = value; }
#define CONSYS_REG_READ reg_read
#define CONSYS_REG_WRITE reg_write
static void udelay(unsigned int delay) { assert(delay == 40 || delay == 100); action(delay == 40 ? '4' : '1'); }
static int enable_fn(UINT32 enabled) { return (int)enabled; }
static int clear_fn(void) { return 0; }
static int read_fn(WMT_SLEEP_COUNT_TYPE bank, PUINT64 counter, PUINT64 timer)
{
    assert(spin_depth == 1 && provider_calls < WMT_SLEEP_COUNT_MAX);
    provider_order[provider_calls++] = bank;
    action('0' + bank);
    if (!bank_rc[bank] || bank_partial) {
        *counter = bank_zero ? 0 : 100 + bank;
        *timer = bank_zero ? 0 : 1000 + bank;
    }
    return bank_rc[bank];
}
static struct {
    int (*consys_ic_sleep_info_enable_ctrl)(UINT32);
    int (*consys_ic_sleep_info_read_ctrl)(WMT_SLEEP_COUNT_TYPE, PUINT64, PUINT64);
    int (*consys_ic_sleep_info_clear)(void);
} table, *wmt_consys_ic_ops = &table;
static int mtk_wcn_stp_coredump_start_get(void) { return coredump; }
static void osal_signal_init(P_OSAL_SIGNAL signal) { signal->raised = 0; }
static void osal_raise_signal(P_OSAL_SIGNAL signal) { signal->raised = 1; }
static int wmt_lib_put_op(int *queue, P_OSAL_OP op)
{
    if (queue == &gDevWmt.rActiveOpQ) return !put_failure;
    assert(queue == &gDevWmt.rFreeOpQ);
    op->result = 123456;
    return 1;
}
static void osal_trigger_event(int *event)
{
    assert(event == &gDevWmt.rWmtdWq);
    if (!no_completion) {
        int result = opfunc_get_consys_state(&host_op.op);
        atomic_dec(&host_op.ref_count);
        osal_op_raise_signal(&host_op, result);
    }
}
static int osal_wait_for_signal_timeout(P_OSAL_SIGNAL signal, int *thread)
{
    assert(thread == &gDevWmt.thread);
    return signal->raised;
}
static P_OSAL_OP wmt_lib_get_free_op(void) { if (no_free) return NULL; memset(&host_op, 0, sizeof(host_op)); return &host_op; }
static int copy_to_user(char *output, const UINT8 *input, size_t count) { if (copy_failure) return 1; memcpy(output, input, count); return 0; }
static CONSYS_STATE_DMP_INFO g_dmp_info, alarm_dmp_info;
static UINT32 g_dump_info_buf_len;
static UINT8 dmp_info_buf[DBG_LOG_STR_SIZE];
static PUINT8 dmp_info_buf_ptr;
static OSAL_SLEEPABLE_LOCK g_dump_info_read_lock;

struct net_device {
    const struct net_device_ops *netdev_ops;
    void *private;
    bool present, running;
    unsigned long watchdog_timeo;
    char name[IFNAMSIZ];
};
static struct ccmni_ctl_block *ccmni_ctl_blk[MAX_MD_NUM];
static bool compat_mode;
static int copy_fault, copies, disables, wakes, opens, port_errno;
static unsigned long copied_size;
static const void *copied_pointer;
static uintptr_t compat_input;
static void *compat_output;
static const void *user_request, *user_payload;
static unsigned long user_request_size;
static char order[32];
static size_t order_length;
static void get_queued_pkts(struct work_struct *work);
static void record(char value) { assert(order_length + 1 < sizeof(order)); order[order_length++] = value; }
static bool in_compat_syscall(void) { return compat_mode; }
static void *compat_ptr(compat_uptr_t pointer) {
    return pointer == compat_input ? compat_output : (void *)(uintptr_t)pointer;
}
static int copy_from_user(void *to, const void *from, unsigned long size) {
    copies++;
    copied_pointer = from;
    copied_size = size;
    if (copy_fault || !from ||
        !((from == user_request && size == user_request_size) ||
          (from == user_payload && size == sizeof(struct ccmni_flt_act)))) return 1;
    memcpy(to, from, size);
    return 0;
}
static int copy_to_user(void *to, const void *from, unsigned long size) {
    copied_size = size;
    if (copy_fault || !to) return 1;
    memcpy(to, from, size);
    return 0;
}
static void *netdev_priv(struct net_device *dev) { return dev->private; }
static bool netif_device_present(struct net_device *dev) { return dev->present; }
static bool netif_running(struct net_device *dev) { return dev->running; }
static int atomic_read(const atomic_t *value) { return *value; }
static void atomic_set(atomic_t *value, int new_value) { *value = new_value; }
static void atomic_dec(atomic_t *value) { --*value; }
static void atomic_inc(atomic_t *value) { ++*value; }
static void netif_tx_disable(struct net_device *dev) { (void)dev; disables++; }
static void netif_tx_wake_all_queues(struct net_device *dev) { (void)dev; wakes++; }
static bool cancel_delayed_work(struct delayed_work *work) { record('C'); bool old = work->pending; work->pending = false; return old; }
static bool flush_delayed_work(struct delayed_work *work) {
    record('F');
    if (!work->running) return false;
    work->running = false;
    get_queued_pkts(&work->work);
    return true;
}
#define to_delayed_work(work) ((struct delayed_work *)(work))
#define container_of(pointer, type, member) ((type *)((char *)(pointer) - offsetof(type, member)))
static int port_open(int status, char *name) {
    assert(status == DEV_OPEN && !strcmp(name, "ccmni1"));
    record('P');
    opens++;
    return port_errno;
}
static int ccmni_open(struct net_device *dev) { (void)dev; return 0; }
static int ccmni_close(struct net_device *dev) { (void)dev; return 0; }
static int ccmni_start_xmit(struct sk_buff *skb, struct net_device *dev) { (void)skb; (void)dev; return 0; }
static void ccmni_tx_timeout(struct net_device *dev, unsigned int queue) { (void)dev; (void)queue; }
static int ccmni_change_mtu(struct net_device *dev, int mtu) { (void)dev; (void)mtu; return 0; }
static u16 ccmni_select_queue(struct net_device *dev, struct sk_buff *skb, struct net_device *other) { (void)dev; (void)skb; (void)other; return 0; }

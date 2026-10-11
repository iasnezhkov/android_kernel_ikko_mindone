static struct md_ccif_ctrl ctrl;
static struct ccci_per_md per;
static struct sk_buff packet;
static struct flow_ctrl flow;
static struct port_t port;
static struct port_proxy proxy;
static struct hif_ops ops;
static void *ccci_hif[3];
static struct hif_ops *ccci_hif_op[3];
static int pool_failures, kernel_failures, pool_calls, kernel_calls, sleep_calls, sleep_ms;
static int ring_failures, ring_checks, wait_calls, wait_mode, state_calls, frees, kicks, writes;
static int state_flip_at;
static int dispatch_state, lock_depth, signal_pending, signal_queries, kick_result, write_result;
static int lb_dl_q;
static struct ccci_header sent;
static jmp_buf cutoff;
static int cut_at;

static struct sk_buff *make_packet(void) { packet.data = packet.bytes + NET_SKB_PAD; packet.len = 0; return &packet; }
static struct sk_buff *__alloc_skb_from_pool(int size) { assert(size == 16); if (++pool_calls > cut_at) longjmp(cutoff, 1); return pool_calls <= pool_failures ? NULL : make_packet(); }
static struct sk_buff *__alloc_skb_from_kernel(int size, int flags) { (void)size; assert(flags == GFP_KERNEL || flags == GFP_ATOMIC); kernel_calls++; return kernel_calls <= kernel_failures ? NULL : make_packet(); }
static unsigned int skb_headroom(struct sk_buff *skb) { return (unsigned int)(skb->data - skb->bytes); }
static void *skb_push(struct sk_buff *skb, size_t length) { skb->data -= length; return skb->data; }
static void *skb_pull(struct sk_buff *skb, size_t length) { skb->data += length; return skb->data; }
static void *skb_put(struct sk_buff *skb, size_t length) { void *result = skb->data + skb->len; skb->len += length; assert(skb->data + skb->len <= skb->bytes + sizeof(skb->bytes)); return result; }
static void msleep(unsigned int ms) { sleep_calls++; sleep_ms += ms; }
static void *ccci_hif_get_by_id(unsigned char id) { assert(id == CCIF_HIF_ID); return &ctrl; }
static int ccci_md_get_cap_by_id(int id) { assert(id == MD_SYS1); return MODEM_CAP_TXBUSY_STOP; }
static struct ccci_per_md *ccci_get_per_md_data(int id) { assert(id == MD_SYS1); return &per; }
static int atomic_read(int *value) { return *value; }
#define spin_lock_irqsave(lock, flags) do { assert(!lock_depth); *(lock) = 1; lock_depth++; (flags) = 0; } while (0)
#define spin_unlock_irqrestore(lock, flags) do { assert(lock_depth == 1); *(lock) = 0; lock_depth--; (void)(flags); } while (0)
static int ccci_ringbuf_writeable(int id, void *ring, int length) { assert(id == MD_SYS1 && ring && length == 16 && lock_depth == 1); if (++ring_checks > cut_at) longjmp(cutoff, 1); return ring_checks <= ring_failures ? -1 : length; }
static void ccci_md_inc_tx_seq_num(int id, void *traffic, struct ccci_header *header) { (void)id; (void)traffic; header->seq_num++; }
static void ccci_channel_update_packet_counter(int counter, struct ccci_header *header) { (void)counter; (void)header; }
static void c2k_mem_dump(unsigned char *data, int length) { (void)data; (void)length; }
static int ccci_ch_to_c2k_ch(int state, int channel, int direction) { (void)state; (void)channel; (void)direction; assert(0); return 0; }
static int ccci_ringbuf_write(int id, void *ring, unsigned char *data, int length) { assert(id == MD_SYS1 && ring && lock_depth == 1); sent = *(struct ccci_header *)data; writes++; return write_result ? write_result : length; }
static void ccci_md_add_log_history(void *traffic, int direction, int queue, struct ccci_header *header, int dropped) { (void)traffic; (void)direction; (void)queue; (void)header; (void)dropped; }
static void ccci_free_skb(struct sk_buff *skb) { assert(skb == &packet); frees++; }
static int md_ccif_send(int id, int channel) { assert(id == CCIF_HIF_ID && channel == 0 && lock_depth == 1); kicks++; return kick_result; }
static void ccif_set_busy_queue(struct md_ccif_ctrl *md, int qno) { assert(md == &ctrl && qno == 0); }
static void ccif_clear_busy_queue(struct md_ccif_ctrl *md, int qno) { assert(md == &ctrl && qno == 0); }
static void ccci_port_queue_status_notify(int md, int hif, int queue, int direction, int status) { (void)md; (void)hif; (void)queue; (void)direction; (void)status; }
static int wait_control(int already_woken) { assert(!lock_depth); wait_calls++; if (wait_mode == 1) return -ERESTARTSYS; if (wait_mode == 2) longjmp(cutoff, 2); (void)already_woken; ring_failures = 0; return 0; }
#define wait_event_interruptible_exclusive(queue, condition) ((void)(queue), wait_control(condition))
static int ccci_fsm_get_md_state(int id) { assert(id == MD_SYS1); state_calls++; if (state_calls == state_flip_at) dispatch_state = GATED; return dispatch_state; }
static int ccci_cldma_hif_send_skb(unsigned char hif, int queue, struct sk_buff *skb, int pool, int blocking) { (void)hif; (void)queue; (void)skb; (void)pool; (void)blocking; assert(0); return 0; }
static struct port_t *port_get_by_channel(int id, int channel) { (void)id; (void)channel; return NULL; }

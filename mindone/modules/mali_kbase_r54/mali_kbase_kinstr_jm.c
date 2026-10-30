// SPDX-License-Identifier: GPL-2.0
#include "mali_kbase_kinstr_jm.h"
#include <uapi/gpu/arm/midgard/mali_kbase_kinstr_jm_reader.h>

#include "mali_kbase.h"
#include "mali_kbase_linux.h"

#include <backend/gpu/mali_kbase_jm_rb.h>

#include <asm/barrier.h>
#include <linux/anon_inodes.h>
#include <linux/circ_buf.h>
#include <linux/fs.h>
#include <linux/kref.h>
#include <linux/ktime.h>
#include <linux/log2.h>
#include <linux/mutex.h>
#include <linux/rculist_bl.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/version.h>
#include <linux/version_compat_defs.h>
#include <linux/wait.h>

#if KERNEL_VERSION(4, 16, 0) > LINUX_VERSION_CODE
#include <uapi/linux/eventpoll.h>
#endif

#if KERNEL_VERSION(5, 1, 0) <= LINUX_VERSION_CODE
#include <linux/build_bug.h>
#elif !defined(static_assert)

#define static_assert(e, ...) __static_assert(e, #__VA_ARGS__, #e)
#define __static_assert(e, msg, ...) _Static_assert(e, msg)
#endif

#define PR_ "mali_kbase_kinstr_jm: "

DEFINE_STATIC_KEY_FALSE(basep_kinstr_jm_reader_static_key);

#define KBASE_KINSTR_JM_VERSION 2

struct kbase_kinstr_jm {
	struct hlist_bl_head readers;
	struct kref refcount;
};

struct kbase_kinstr_jm_atom_state_change {
	u64 timestamp;
	s8 state;
	u8 atom;
	u8 flags;
	u8 reserved[1];

	union {
		struct {
			u8 slot;
		} start;
		u8 padding[4];
	} data;
};
static_assert(((1 << 8 * sizeof(((struct kbase_kinstr_jm_atom_state_change *)0)->state)) - 1) >=
	      KBASE_KINSTR_JM_READER_ATOM_STATE_COUNT);

#define KBASE_KINSTR_JM_ATOM_STATE_FLAG_OVERFLOW BIT(0)

struct reader_changes {
	struct kbase_kinstr_jm_atom_state_change *data;
	spinlock_t producer;
	struct mutex consumer;
	u32 head;
	u32 tail;
	u32 size;
	u32 threshold;
};

static inline bool reader_changes_is_valid_size(const size_t size)
{
	const size_t elem_size = sizeof(*((struct reader_changes *)0)->data);
	const size_t size_size = sizeof(((struct reader_changes *)0)->size);
	const size_t size_max = (1ull << (size_size * 8)) - 1;

	return is_power_of_2(size) &&
	       ((size / elem_size) <= size_max);
}

static int reader_changes_init(struct reader_changes *const changes, const size_t size)
{
	BUILD_BUG_ON((PAGE_SIZE % sizeof(*changes->data)) != 0);

	if (!reader_changes_is_valid_size(size)) {
		pr_warn(PR_ "invalid size %zu\n", size);
		return -ERANGE;
	}

	changes->data = vmalloc(size);
	if (!changes->data)
		return -ENOMEM;

	spin_lock_init(&changes->producer);
	mutex_init(&changes->consumer);

	changes->size = size / sizeof(*changes->data);
	changes->threshold =
		min(((size_t)(changes->size)) / 4, ((size_t)(PAGE_SIZE)) / sizeof(*changes->data));

	return (int)changes->size;
}

static void reader_changes_term(struct reader_changes *const changes)
{
	struct kbase_kinstr_jm_atom_state_change *data = NULL;
	unsigned long irq;

	spin_lock_irqsave(&changes->producer, irq);
	swap(changes->data, data);
	spin_unlock_irqrestore(&changes->producer, irq);

	mutex_destroy(&changes->consumer);
	vfree(data);
}

static u32 reader_changes_count_locked(struct reader_changes *const changes)
{
	u32 head;

	lockdep_assert_held_once(&changes->consumer);

	head = smp_load_acquire(&changes->head);

	return CIRC_CNT_TO_END(head, changes->tail, changes->size);
}

static u32 reader_changes_count(struct reader_changes *const changes)
{
	u32 ret;

	mutex_lock(&changes->consumer);
	ret = reader_changes_count_locked(changes);
	mutex_unlock(&changes->consumer);
	return ret;
}

static void reader_changes_push(struct reader_changes *const changes,
				const struct kbase_kinstr_jm_atom_state_change *const change,
				wait_queue_head_t *const wait_queue)
{
	u32 head, tail, size, space;
	unsigned long irq;
	struct kbase_kinstr_jm_atom_state_change *data;

	spin_lock_irqsave(&changes->producer, irq);

	data = changes->data;
	if (!data)
		goto unlock;

	size = changes->size;
	head = changes->head;
	tail = smp_load_acquire(&changes->tail);

	space = CIRC_SPACE(head, tail, size);
	if (space >= 1) {
		data[head] = *change;
		if (space == 1) {
			data[head].flags |= KBASE_KINSTR_JM_ATOM_STATE_FLAG_OVERFLOW;
			pr_warn(PR_ "overflow of circular buffer\n");
		}
		smp_store_release(&changes->head, (head + 1) & (size - 1));
	}

	if (CIRC_CNT(head + 1, tail, size) >= changes->threshold)
		wake_up_interruptible(wait_queue);

unlock:
	spin_unlock_irqrestore(&changes->producer, irq);
}

struct reader {
	struct hlist_bl_node node;
	struct rcu_head rcu_head;
	struct reader_changes changes;
	wait_queue_head_t wait_queue;
	struct kbase_kinstr_jm *context;
};

static struct kbase_kinstr_jm *kbase_kinstr_jm_ref_get(struct kbase_kinstr_jm *const ctx);
static void kbase_kinstr_jm_ref_put(struct kbase_kinstr_jm *const ctx);
static int kbase_kinstr_jm_readers_add(struct kbase_kinstr_jm *const ctx,
				       struct reader *const reader);
static void kbase_kinstr_jm_readers_del(struct kbase_kinstr_jm *const ctx,
					struct reader *const reader);

static void reader_term(struct reader *const reader)
{
	if (!reader)
		return;

	kbase_kinstr_jm_readers_del(reader->context, reader);
	reader_changes_term(&reader->changes);
	kbase_kinstr_jm_ref_put(reader->context);

	kfree_rcu(reader, rcu_head);
}

static int reader_init(struct reader **const out_reader, struct kbase_kinstr_jm *const ctx,
		       size_t const num_changes)
{
	struct reader *reader = NULL;
	const size_t change_size = sizeof(struct kbase_kinstr_jm_atom_state_change);
	int status;

	if (!out_reader || !ctx || !num_changes)
		return -EINVAL;

	reader = kzalloc(sizeof(*reader), GFP_KERNEL);
	if (!reader)
		return -ENOMEM;

	INIT_HLIST_BL_NODE(&reader->node);
	init_waitqueue_head(&reader->wait_queue);

	reader->context = kbase_kinstr_jm_ref_get(ctx);

	status = reader_changes_init(&reader->changes, num_changes * change_size);
	if (status < 0)
		goto fail;

	status = kbase_kinstr_jm_readers_add(ctx, reader);
	if (status < 0)
		goto fail;

	*out_reader = reader;

	return 0;

fail:
	kbase_kinstr_jm_ref_put(reader->context);
	kfree(reader);
	return status;
}

static int reader_release(struct inode *const node, struct file *const file)
{
	struct reader *const reader = file->private_data;

	CSTD_UNUSED(node);

	reader_term(reader);
	file->private_data = NULL;

	return 0;
}

static ssize_t reader_changes_copy_to_user(struct reader_changes *const changes,
					   char __user *buffer, size_t buffer_size)
{
	ssize_t ret = 0;
	struct kbase_kinstr_jm_atom_state_change const *src_buf = READ_ONCE(changes->data);
	size_t const entry_size = sizeof(*src_buf);
	size_t changes_tail, changes_count, read_size;
	size_t copy_size;

#define is_power_of_two(x) ((x) && !((x) & ((x)-1)))
	static_assert(is_power_of_two(sizeof(struct kbase_kinstr_jm_atom_state_change)));
#undef is_power_of_two

	lockdep_assert_held_once(&changes->consumer);

	do {
		changes_tail = changes->tail;
		changes_count = reader_changes_count_locked(changes);

		if (check_mul_overflow(changes_count, entry_size, &copy_size)) {
			ret = -EINVAL;
			goto exit;
		}

		read_size = min(copy_size, buffer_size & ~(entry_size - 1));

		if (!read_size)
			break;

		if (copy_to_user(buffer, &(src_buf[changes_tail]), read_size)) {
			ret = -EFAULT;
			goto exit;
		}

		buffer += read_size;
		buffer_size -= read_size;
		ret += (ssize_t)read_size;
		changes_tail = (changes_tail + read_size / entry_size) & (changes->size - 1);
		smp_store_release(&changes->tail, changes_tail);
	} while (read_size);
exit:
	return ret;
}

static ssize_t reader_read(struct file *const filp, char __user *const buffer,
			   size_t const buffer_size, loff_t *const offset)
{
	struct reader *const reader = filp->private_data;
	struct reader_changes *changes;
	ssize_t ret;

	CSTD_UNUSED(offset);

	if (!reader)
		return -EBADF;

	if (buffer_size < sizeof(struct kbase_kinstr_jm_atom_state_change))
		return -ENOBUFS;

#if KERNEL_VERSION(5, 0, 0) <= LINUX_VERSION_CODE
	if (!access_ok(buffer, buffer_size))
		return -EIO;
#else
	if (!access_ok(VERIFY_WRITE, buffer, buffer_size))
		return -EIO;
#endif

	changes = &reader->changes;

	mutex_lock(&changes->consumer);
	if (!reader_changes_count_locked(changes)) {
		if (filp->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			goto exit;
		}

		if (wait_event_interruptible(reader->wait_queue,
					     !!reader_changes_count_locked(changes))) {
			ret = -EINTR;
			goto exit;
		}
	}

	ret = reader_changes_copy_to_user(changes, buffer, buffer_size);

exit:
	mutex_unlock(&changes->consumer);
	return ret;
}

static __poll_t reader_poll(struct file *const file, struct poll_table_struct *const wait)
{
	struct reader *reader;
	struct reader_changes *changes;
	__poll_t mask = 0;

	if (unlikely(!file || !wait))
		return EPOLLHUP | EPOLLERR;

	reader = file->private_data;
	if (unlikely(!reader))
		return EPOLLHUP | EPOLLERR;

	changes = &reader->changes;
	if (reader_changes_count(changes) >= changes->threshold)
		return EPOLLIN | EPOLLRDNORM;

	poll_wait(file, &reader->wait_queue, wait);

	if (reader_changes_count(changes) > 0)
		mask |= EPOLLIN | EPOLLRDNORM;

	return mask;
}

static const struct file_operations file_operations = { .owner = THIS_MODULE,
#if (KERNEL_VERSION(6, 12, 0) > LINUX_VERSION_CODE)
							.llseek = no_llseek,
#endif
							.read = reader_read,
							.poll = reader_poll,
							.release = reader_release };

static const size_t kbase_kinstr_jm_readers_max = 16;

static void kbase_kinstr_jm_release(struct kref *const ref)
{
	struct kbase_kinstr_jm *const ctx = container_of(ref, struct kbase_kinstr_jm, refcount);

	kfree(ctx);
}

static struct kbase_kinstr_jm *kbase_kinstr_jm_ref_get(struct kbase_kinstr_jm *const ctx)
{
	if (likely(ctx))
		kref_get(&ctx->refcount);
	return ctx;
}

static void kbase_kinstr_jm_ref_put(struct kbase_kinstr_jm *const ctx)
{
	if (likely(ctx))
		kref_put(&ctx->refcount, kbase_kinstr_jm_release);
}

static int kbase_kinstr_jm_readers_add(struct kbase_kinstr_jm *const ctx,
				       struct reader *const reader)
{
	struct hlist_bl_head *const readers = &ctx->readers;
	struct hlist_bl_node *node;
	struct reader *temp;
	size_t count = 0;

	hlist_bl_lock(readers);

	hlist_bl_for_each_entry_rcu(temp, node, readers, node)
		++count;

	if (kbase_kinstr_jm_readers_max < count) {
		hlist_bl_unlock(readers);
		return -ENOMEM;
	}

	hlist_bl_add_head_rcu(&reader->node, readers);

	hlist_bl_unlock(readers);

	static_branch_inc(&basep_kinstr_jm_reader_static_key);

	return 0;
}

static void kbase_kinstr_jm_readers_del(struct kbase_kinstr_jm *const ctx,
					struct reader *const reader)
{
	struct hlist_bl_head *const readers = &ctx->readers;

	hlist_bl_lock(readers);
	hlist_bl_del_rcu(&reader->node);
	hlist_bl_unlock(readers);

	static_branch_dec(&basep_kinstr_jm_reader_static_key);
}

int kbase_kinstr_jm_get_fd(struct kbase_kinstr_jm *const ctx, union kbase_kinstr_jm_fd *jm_fd_arg)
{
	struct kbase_kinstr_jm_fd_in const *in;
	struct reader *reader;
	size_t const change_size = sizeof(struct kbase_kinstr_jm_atom_state_change);
	int status;
	int fd;
	size_t i;

	if (!ctx || !jm_fd_arg)
		return -EINVAL;

	in = &jm_fd_arg->in;

	if (!is_power_of_2(in->count))
		return -EINVAL;

	for (i = 0; i < sizeof(in->padding); ++i)
		if (in->padding[i])
			return -EINVAL;

	status = reader_init(&reader, ctx, in->count);
	if (status < 0)
		return status;

	jm_fd_arg->out.version = KBASE_KINSTR_JM_VERSION;
	jm_fd_arg->out.size = change_size;
	memset(&jm_fd_arg->out.padding, 0, sizeof(jm_fd_arg->out.padding));

	fd = anon_inode_getfd("[mali_kinstr_jm]", &file_operations, reader, O_CLOEXEC);
	if (fd < 0)
		reader_term(reader);

	return fd;
}

int kbase_kinstr_jm_init(struct kbase_kinstr_jm **const out_ctx)
{
	struct kbase_kinstr_jm *ctx = NULL;

	if (!out_ctx)
		return -EINVAL;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	INIT_HLIST_BL_HEAD(&ctx->readers);
	kref_init(&ctx->refcount);

	*out_ctx = ctx;

	return 0;
}

void kbase_kinstr_jm_term(struct kbase_kinstr_jm *const ctx)
{
	kbase_kinstr_jm_ref_put(ctx);
}

void kbasep_kinstr_jm_atom_state(struct kbase_jd_atom *const katom,
				 const enum kbase_kinstr_jm_reader_atom_state state)
{
	struct kbase_context *const kctx = katom->kctx;
	struct kbase_kinstr_jm *const ctx = kctx->kinstr_jm;
	const u8 id = kbase_jd_atom_id(kctx, katom);
	struct kbase_kinstr_jm_atom_state_change change = { .timestamp = ktime_get_raw_ns(),
							    .atom = id,
							    .state = state };
	struct reader *reader;
	struct hlist_bl_node *node;

	WARN(KBASE_KINSTR_JM_READER_ATOM_STATE_COUNT < state || 0 > state,
	     PR_ "unsupported katom (%u) state (%i)", id, state);

	switch (state) {
	case KBASE_KINSTR_JM_READER_ATOM_STATE_START:
		change.data.start.slot = katom->slot_nr;
		break;
	default:
		break;
	}

	rcu_read_lock();
	hlist_bl_for_each_entry_rcu(reader, node, &ctx->readers, node)
		reader_changes_push(&reader->changes, &change, &reader->wait_queue);
	rcu_read_unlock();
}

KBASE_EXPORT_TEST_API(kbasep_kinstr_jm_atom_state);

void kbasep_kinstr_jm_atom_hw_submit(struct kbase_jd_atom *const katom)
{
	struct kbase_context *const kctx = katom->kctx;
	struct kbase_device *const kbdev = kctx->kbdev;
	const unsigned int slot = katom->slot_nr;
	struct kbase_jd_atom *const submitted = kbase_gpu_inspect(kbdev, slot, 0);

	BUILD_BUG_ON(SLOT_RB_SIZE != 2);

	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (WARN_ON(slot >= GPU_MAX_JOB_SLOTS))
		return;
	if (WARN_ON(!submitted))
		return;

	if (submitted == katom)
		kbase_kinstr_jm_atom_state_start(katom);
}

void kbasep_kinstr_jm_atom_hw_release(struct kbase_jd_atom *const katom)
{
	struct kbase_context *const kctx = katom->kctx;
	struct kbase_device *const kbdev = kctx->kbdev;
	const unsigned int slot = katom->slot_nr;
	struct kbase_jd_atom *const submitted = kbase_gpu_inspect(kbdev, slot, 0);
	struct kbase_jd_atom *const queued = kbase_gpu_inspect(kbdev, slot, 1);

	BUILD_BUG_ON(SLOT_RB_SIZE != 2);

	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (WARN_ON(slot >= GPU_MAX_JOB_SLOTS))
		return;
	if (WARN_ON(!submitted))
		return;
	if (WARN_ON((submitted != katom) && (queued != katom)))
		return;

	if (queued == katom)
		return;

	if (katom->gpu_rb_state == KBASE_ATOM_GPU_RB_SUBMITTED)
		kbase_kinstr_jm_atom_state_stop(katom);
	if (queued && queued->gpu_rb_state == KBASE_ATOM_GPU_RB_SUBMITTED)
		kbase_kinstr_jm_atom_state_start(queued);
}

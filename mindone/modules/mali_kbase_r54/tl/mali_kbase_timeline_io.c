// SPDX-License-Identifier: GPL-2.0
#include "mali_kbase_timeline_priv.h"
#include "mali_kbase_tlstream.h"
#include "mali_kbase_tracepoints.h"
#include "mali_kbase_timeline.h"

#include <device/mali_kbase_device.h>

#include <linux/poll.h>
#include <linux/version_compat_defs.h>
#include <linux/anon_inodes.h>
#include <linux/overflow.h>

#if KERNEL_VERSION(4, 16, 0) > LINUX_VERSION_CODE
#include <uapi/linux/eventpoll.h>
#endif

static int kbase_unprivileged_global_profiling;

static int kbase_unprivileged_global_profiling_set(const char *val, const struct kernel_param *kp)
{
	int new_val;
	int ret = kstrtoint(val, 0, &new_val);

	CSTD_UNUSED(kp);

	if (ret == 0) {
		if (new_val < 1)
			return -EINVAL;

		kbase_unprivileged_global_profiling = 1;
	}

	return ret;
}

static const struct kernel_param_ops kbase_global_unprivileged_profiling_ops = {
	.get = param_get_int,
	.set = kbase_unprivileged_global_profiling_set,
};

module_param_cb(kbase_unprivileged_global_profiling, &kbase_global_unprivileged_profiling_ops,
		&kbase_unprivileged_global_profiling, 0600);

static ssize_t kbasep_timeline_io_read(struct file *filp, char __user *buffer, size_t size,
				       loff_t *f_pos);
static __poll_t kbasep_timeline_io_poll(struct file *filp, poll_table *wait);
static int kbasep_timeline_io_release(struct inode *inode, struct file *filp);
static int kbasep_timeline_io_fsync(struct file *filp, loff_t start, loff_t end, int datasync);

static bool timeline_is_permitted(void)
{
#if KERNEL_VERSION(5, 8, 0) <= LINUX_VERSION_CODE
	return kbase_unprivileged_global_profiling || perfmon_capable();
#else
	return kbase_unprivileged_global_profiling || capable(CAP_SYS_ADMIN);
#endif
}

static int kbasep_timeline_io_packet_pending(struct kbase_timeline *timeline,
					     struct kbase_tlstream **ready_stream,
					     unsigned int *rb_idx_raw)
{
	enum tl_stream_type i;

	KBASE_DEBUG_ASSERT(ready_stream);
	KBASE_DEBUG_ASSERT(rb_idx_raw);

	for (i = (enum tl_stream_type)0; i < TL_STREAM_TYPE_COUNT; ++i) {
		struct kbase_tlstream *stream = &timeline->streams[i];
		*rb_idx_raw = (unsigned int)atomic_read(&stream->rbi);

		smp_rmb();
		if ((uint)atomic_read(&stream->wbi) != *rb_idx_raw) {
			*ready_stream = stream;
			return 1;
		}
	}

	return 0;
}

static int kbasep_timeline_has_header_data(struct kbase_timeline *timeline)
{
	return timeline->obj_header_btc || timeline->aux_header_btc
#if MALI_USE_CSF
	       || timeline->csf_tl_reader.tl_header.btc
#endif
		;
}

static inline int copy_stream_header(char __user *buffer, size_t size, ssize_t *copy_len,
				     const char *hdr, size_t hdr_size, size_t *hdr_btc)
{
	const size_t offset = hdr_size - *hdr_btc;
	size_t copy_size;

	if (check_sub_overflow((size_t)size, (size_t)*copy_len, &copy_size))
		return -1;

	copy_size = MIN(copy_size, *hdr_btc);

	if (!*hdr_btc)
		return 0;

	if (WARN_ON(*hdr_btc > hdr_size))
		return -1;

	if (copy_to_user(&buffer[*copy_len], &hdr[offset], copy_size))
		return -1;

	*hdr_btc -= copy_size;
	*copy_len += (ssize_t)copy_size;

	return 0;
}

static inline int kbasep_timeline_copy_headers(struct kbase_timeline *timeline, char __user *buffer,
					       size_t size, ssize_t *copy_len)
{
	if (copy_stream_header(buffer, size, copy_len, obj_desc_header, obj_desc_header_size,
			       &timeline->obj_header_btc))
		return -1;

	if (copy_stream_header(buffer, size, copy_len, aux_desc_header, aux_desc_header_size,
			       &timeline->aux_header_btc))
		return -1;
#if MALI_USE_CSF
	if (copy_stream_header(buffer, size, copy_len, timeline->csf_tl_reader.tl_header.data,
			       timeline->csf_tl_reader.tl_header.size,
			       &timeline->csf_tl_reader.tl_header.btc))
		return -1;
#endif
	return 0;
}

static ssize_t kbasep_timeline_io_read(struct file *filp, char __user *buffer, size_t size,
				       loff_t *f_pos)
{
	ssize_t copy_len = 0;
	struct kbase_timeline *timeline;

	KBASE_DEBUG_ASSERT(filp);
	KBASE_DEBUG_ASSERT(f_pos);

	if (WARN_ON(!filp->private_data))
		return -EFAULT;

	timeline = (struct kbase_timeline *)filp->private_data;

	if (!buffer)
		return -EINVAL;

	if (*f_pos < 0)
		return -EINVAL;

	mutex_lock(&timeline->reader_lock);

	while (copy_len < (ssize_t)size) {
		struct kbase_tlstream *stream = NULL;
		unsigned int rb_idx_raw = 0;
		unsigned int wb_idx_raw;
		unsigned int rb_idx;
		size_t rb_size;

		if (kbasep_timeline_copy_headers(timeline, buffer, size, &copy_len)) {
			copy_len = -EFAULT;
			break;
		}

		if (copy_len > 0) {
			if (!kbasep_timeline_io_packet_pending(timeline, &stream, &rb_idx_raw))
				break;
		} else {
			if (wait_event_interruptible(timeline->event_queue,
						     kbasep_timeline_io_packet_pending(
							     timeline, &stream, &rb_idx_raw))) {
				copy_len = -ERESTARTSYS;
				break;
			}
		}

		if (WARN_ON(!stream)) {
			copy_len = -EFAULT;
			break;
		}

		rb_idx = rb_idx_raw % PACKET_COUNT;
		rb_size = (size_t)atomic_read(&stream->buffer[rb_idx].size);
		if (rb_size > (size_t)((ssize_t)size - copy_len))
			break;
		if (copy_to_user(&buffer[copy_len], stream->buffer[rb_idx].data, rb_size)) {
			copy_len = -EFAULT;
			break;
		}

		smp_rmb();
		wb_idx_raw = (unsigned int)atomic_read(&stream->wbi);

		if (wb_idx_raw - rb_idx_raw < PACKET_COUNT) {
			copy_len += (ssize_t)rb_size;
			atomic_inc(&stream->rbi);
#if MALI_UNIT_TEST
			atomic_add(rb_size, &timeline->bytes_collected);
#endif

		} else {
			const unsigned int new_rb_idx_raw = wb_idx_raw - PACKET_COUNT + 1;

			atomic_set(&stream->rbi, (int)new_rb_idx_raw);
		}
	}

	mutex_unlock(&timeline->reader_lock);

	return copy_len;
}

static __poll_t kbasep_timeline_io_poll(struct file *filp, poll_table *wait)
{
	struct kbase_tlstream *stream;
	unsigned int rb_idx;
	struct kbase_timeline *timeline;

	KBASE_DEBUG_ASSERT(filp);
	KBASE_DEBUG_ASSERT(wait);

	if (WARN_ON(!filp->private_data))
		return EPOLLHUP | EPOLLERR;

	timeline = (struct kbase_timeline *)filp->private_data;

	if (kbasep_timeline_has_header_data(timeline))
		return EPOLLIN | EPOLLRDNORM;

	poll_wait(filp, &timeline->event_queue, wait);
	if (kbasep_timeline_io_packet_pending(timeline, &stream, &rb_idx))
		return EPOLLIN | EPOLLRDNORM;

	return (__poll_t)0;
}

int kbase_timeline_io_acquire(struct kbase_device *kbdev, u32 flags)
{

	static const struct file_operations kbasep_tlstream_fops = {
		.owner = THIS_MODULE,
		.release = kbasep_timeline_io_release,
		.read = kbasep_timeline_io_read,
		.poll = kbasep_timeline_io_poll,
		.fsync = kbasep_timeline_io_fsync,
	};
	int err;

	if (!timeline_is_permitted())
		return -EPERM;

	if (WARN_ON(!kbdev) || (flags & ~BASE_TLSTREAM_FLAGS_MASK))
		return -EINVAL;

	err = kbase_timeline_acquire(kbdev, flags);
	if (err)
		return err;

	err = anon_inode_getfd("[mali_tlstream]", &kbasep_tlstream_fops, kbdev->timeline,
			       O_RDONLY | O_CLOEXEC);
	if (err < 0)
		kbase_timeline_release(kbdev->timeline);

	return err;
}

#if IS_ENABLED(CONFIG_DEBUG_FS)
static int kbasep_timeline_io_open(struct inode *in, struct file *file)
{
	struct kbase_device *const kbdev = in->i_private;

	if (WARN_ON(!kbdev))
		return -EFAULT;

	file->private_data = kbdev->timeline;
	return kbase_timeline_acquire(kbdev, BASE_TLSTREAM_FLAGS_MASK &
						     ~BASE_TLSTREAM_JOB_DUMPING_ENABLED);
}

void kbase_timeline_io_debugfs_init(struct kbase_device *const kbdev)
{
	static const struct file_operations kbasep_tlstream_debugfs_fops = {
		.owner = THIS_MODULE,
		.open = kbasep_timeline_io_open,
		.release = kbasep_timeline_io_release,
		.read = kbasep_timeline_io_read,
		.poll = kbasep_timeline_io_poll,
		.fsync = kbasep_timeline_io_fsync,
	};
	struct dentry *file;

	if (WARN_ON(!kbdev) || WARN_ON(IS_ERR_OR_NULL(kbdev->mali_debugfs_directory)))
		return;

	file = debugfs_create_file("tlstream", 0400, kbdev->mali_debugfs_directory, kbdev,
				   &kbasep_tlstream_debugfs_fops);

	if (IS_ERR_OR_NULL(file))
		dev_warn(kbdev->dev, "Unable to create timeline debugfs entry");
}
#else

void kbase_timeline_io_debugfs_init(struct kbase_device *const kbdev)
{
}
#endif

static int kbasep_timeline_io_release(struct inode *inode, struct file *filp)
{
	CSTD_UNUSED(inode);

	kbase_timeline_release(filp->private_data);
	return 0;
}

static int kbasep_timeline_io_fsync(struct file *filp, loff_t start, loff_t end, int datasync)
{
	CSTD_UNUSED(start);
	CSTD_UNUSED(end);
	CSTD_UNUSED(datasync);

	return kbase_timeline_streams_flush(filp->private_data);
}

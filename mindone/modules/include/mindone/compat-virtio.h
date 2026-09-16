/* SPDX-License-Identifier: GPL-2.0 */
/* mindone/compat-virtio.h — virtio pieces of the 6.1/6.12 compatibility layer (see compat.h). */
#ifndef __MINDONE_COMPAT_VIRTIO_H__
#define __MINDONE_COMPAT_VIRTIO_H__

#include <linux/version.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>

/*
 * virtio_config_ops::find_vqs() takes one virtqueue_info[] since 6.11 instead of the
 * separate callbacks[]/names[]/ctx[] arrays. Implementations declare their parameter
 * list with MINDONE_FIND_VQS_PARAMS and read entries through the accessors.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
#define MINDONE_FIND_VQS_PARAMS \
	struct virtio_device *vdev, unsigned int nvqs, struct virtqueue *vqs[], \
	struct virtqueue_info vqs_info[], struct irq_affinity *desc
#define MINDONE_VQ_CALLBACK(i)		(vqs_info[i].callback)
#define MINDONE_VQ_NAME(i)		(vqs_info[i].name)
#else
#define MINDONE_FIND_VQS_PARAMS \
	struct virtio_device *vdev, unsigned int nvqs, struct virtqueue *vqs[], \
	vq_callback_t *callbacks[], const char *const names[], const bool *ctx, \
	struct irq_affinity *desc
#define MINDONE_VQ_CALLBACK(i)		(callbacks[i])
#define MINDONE_VQ_NAME(i)		(names[i])
#endif

/*
 * virtio_find_vqs() takes virtqueue_info[] since 6.11 too; callers with separate
 * callback/name arrays go through this helper (no ctx, no affinity).
 */
#include <linux/slab.h>
static inline int mindone_virtio_find_vqs(struct virtio_device *vdev, unsigned int nvqs,
					  struct virtqueue *vqs[], vq_callback_t *callbacks[],
					  const char *const names[])
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
	struct virtqueue_info *info;
	unsigned int i;
	int ret;

	info = kcalloc(nvqs, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	for (i = 0; i < nvqs; i++) {
		info[i].name = names[i];
		info[i].callback = callbacks[i];
	}
	ret = virtio_find_vqs(vdev, nvqs, vqs, info, NULL);
	kfree(info);
	return ret;
#else
	return virtio_find_vqs(vdev, nvqs, vqs, callbacks, names, NULL);
#endif
}

#endif /* __MINDONE_COMPAT_VIRTIO_H__ */

// SPDX-License-Identifier: GPL-2.0
#include <mali_kbase.h>
#include <device/mali_kbase_device.h>

#if IS_ENABLED(CONFIG_DEBUG_FS)

static int kbasep_gpu_memory_seq_show(struct seq_file *sfile, void *data)
{
	struct list_head *entry;
	const struct list_head *kbdev_list;

	CSTD_UNUSED(data);

	kbdev_list = kbase_device_get_list();
	list_for_each(entry, kbdev_list) {
		struct kbase_device *kbdev = NULL;
		struct kbase_context *kctx;

		kbdev = list_entry(entry, struct kbase_device, entry);

		seq_printf(sfile, "%-16s  %10u\n", kbdev->devname,
			   atomic_read(&(kbdev->memdev.used_pages)));
		mutex_lock(&kbdev->kctx_list_lock);
		list_for_each_entry(kctx, &kbdev->kctx_list, kctx_list_link) {

			seq_printf(sfile, "  %s-0x%pK %10u\n", "kctx", kctx,
				   atomic_read(&(kctx->used_pages)));
		}
		mutex_unlock(&kbdev->kctx_list_lock);
	}
	kbase_device_put_list(kbdev_list);
	return 0;
}

static int kbasep_gpu_memory_debugfs_open(struct inode *in, struct file *file)
{
	CSTD_UNUSED(in);

	return single_open(file, kbasep_gpu_memory_seq_show, NULL);
}

static const struct file_operations kbasep_gpu_memory_debugfs_fops = {
	.owner = THIS_MODULE,
	.open = kbasep_gpu_memory_debugfs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

void kbasep_gpu_memory_debugfs_init(struct kbase_device *kbdev)
{
	debugfs_create_file("gpu_memory", 0444, kbdev->mali_debugfs_directory, NULL,
			    &kbasep_gpu_memory_debugfs_fops);
}
#else

void kbasep_gpu_memory_debugfs_init(struct kbase_device *kbdev)
{
}
#endif

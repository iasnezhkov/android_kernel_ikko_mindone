// SPDX-License-Identifier: GPL-2.0
#include <linux/debugfs.h>

#include <mali_kbase.h>
#include <mali_kbase_as_fault_debugfs.h>
#include <device/mali_kbase_device.h>

#if IS_ENABLED(CONFIG_DEBUG_FS)
#ifdef CONFIG_MALI_DEBUG

static int kbase_as_fault_read(struct seq_file *sfile, void *data)
{
	uintptr_t as_no = (uintptr_t)sfile->private;

	struct list_head *entry;
	const struct list_head *kbdev_list;
	struct kbase_device *kbdev = NULL;

	CSTD_UNUSED(data);

	kbdev_list = kbase_device_get_list();

	list_for_each(entry, kbdev_list) {
		kbdev = list_entry(entry, struct kbase_device, entry);

		if (kbdev->debugfs_as_read_bitmap & (1ULL << as_no)) {

			kbdev->debugfs_as_read_bitmap &= ~(1ULL << as_no);

			seq_printf(sfile, "%llu\n", (u64)kbdev->as[as_no].pf_data.addr);
		}
	}

	kbase_device_put_list(kbdev_list);

	return 0;
}

static int kbase_as_fault_debugfs_open(struct inode *in, struct file *file)
{
	return single_open(file, kbase_as_fault_read, in->i_private);
}

static const struct file_operations as_fault_fops = {
	.owner = THIS_MODULE,
	.open = kbase_as_fault_debugfs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

#endif
#endif

void kbase_as_fault_debugfs_init(struct kbase_device *kbdev)
{
#if IS_ENABLED(CONFIG_DEBUG_FS)
#ifdef CONFIG_MALI_DEBUG
	uint i;
	char as_name[64];
	struct dentry *debugfs_directory;

	kbdev->debugfs_as_read_bitmap = 0ULL;

	KBASE_DEBUG_ASSERT(kbdev->nr_hw_address_spaces);
	KBASE_DEBUG_ASSERT(sizeof(kbdev->as[0].pf_data.addr) == sizeof(u64));

	debugfs_directory = debugfs_create_dir("address_spaces", kbdev->mali_debugfs_directory);

	if (IS_ERR_OR_NULL(debugfs_directory)) {
		dev_warn(kbdev->dev, "unable to create address_spaces debugfs directory");
	} else {
		for (i = 0; i < (uint)kbdev->nr_hw_address_spaces; i++) {
			if (likely(scnprintf(as_name, ARRAY_SIZE(as_name), "as%u", i)))
				debugfs_create_file(as_name, 0444, debugfs_directory,
						    (void *)(uintptr_t)i, &as_fault_fops);
		}
	}

#else
	CSTD_UNUSED(kbdev);
#endif
#else
	CSTD_UNUSED(kbdev);
#endif
}

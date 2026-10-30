// SPDX-License-Identifier: GPL-2.0
#include "mali_kbase_dvfs_debugfs.h"
#include <mali_kbase.h>
#include <linux/seq_file.h>
#include <mtk_ddk_define.h>

#if IS_ENABLED(CONFIG_DEBUG_FS)

static int kbasep_dvfs_utilization_debugfs_show(struct seq_file *file, void *data)
{
	struct kbase_device *kbdev = file->private;

	CSTD_UNUSED(data);
#if MALI_USE_CSF
#if IS_ENABLED(CONFIG_MALI_MIDGARD_DVFS) && \
	IS_ENABLED(CONFIG_MALI_MTK_DVFS_POLICY)
	seq_printf(file, "busy_time: %u idle_time: %u protm_time: %u\n",
			kbdev->pm.backend.metrics.values.time_busy[0],
			kbdev->pm.backend.metrics.values.time_idle[0],
			kbdev->pm.backend.metrics.values.time_in_protm);
#else
	seq_printf(file, "busy_time: %u idle_time: %u protm_time: %u\n",
		   kbdev->pm.backend.metrics.values.time_busy,
		   kbdev->pm.backend.metrics.values.time_idle,
		   kbdev->pm.backend.metrics.values.time_in_protm);
#endif
#else
	seq_printf(file, "busy_time: %u idle_time: %u\n",
		   kbdev->pm.backend.metrics.values.time_busy,
		   kbdev->pm.backend.metrics.values.time_idle);
#endif

	return 0;
}

static int kbasep_dvfs_utilization_debugfs_open(struct inode *in, struct file *file)
{
	return single_open(file, kbasep_dvfs_utilization_debugfs_show, in->i_private);
}

static const struct file_operations kbasep_dvfs_utilization_debugfs_fops = {
	.open = kbasep_dvfs_utilization_debugfs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

void kbase_dvfs_status_debugfs_init(struct kbase_device *kbdev)
{
	struct dentry *file;
	const mode_t mode = 0444;

	if (WARN_ON(!kbdev || IS_ERR_OR_NULL(kbdev->mali_debugfs_directory)))
		return;

	file = debugfs_create_file("dvfs_utilization", mode, kbdev->mali_debugfs_directory, kbdev,
				   &kbasep_dvfs_utilization_debugfs_fops);

	if (IS_ERR_OR_NULL(file)) {
		dev_warn(kbdev->dev, "Unable to create dvfs debugfs entry");
	}
}

#else

void kbase_dvfs_status_debugfs_init(struct kbase_device *kbdev)
{
}

#endif

// SPDX-License-Identifier: GPL-2.0
#include <mtk_ddk_define.h>
#include <mali_kbase.h>
#include <mali_kbase_config_defaults.h>
#include <hw_access/mali_kbase_hw_access_regmap.h>
#include <mali_kbase_gator.h>
#include <mali_kbase_reg_track.h>
#include <mali_kbase_mem_linux.h>
#ifdef CONFIG_MALI_DEVFREQ
#include <linux/devfreq.h>
#include <backend/gpu/mali_kbase_devfreq.h>
#if IS_ENABLED(CONFIG_DEVFREQ_THERMAL)
#include <ipa/mali_kbase_ipa_debugfs.h>
#endif
#endif
#include "backend/gpu/mali_kbase_model_linux.h"
#include "uapi/gpu/arm/midgard/mali_kbase_mem_profile_debugfs_buf_size.h"
#include "mali_kbase_mem.h"
#include "mali_kbase_mem_pool_debugfs.h"
#include "mali_kbase_mem_pool_group.h"
#include "mali_kbase_debugfs_helper.h"
#include "mali_kbase_regs_history_debugfs.h"
#include <mali_kbase_hwaccess_backend.h>
#include <mali_kbase_hwaccess_time.h>
#if !MALI_USE_CSF
#include <mali_kbase_hwaccess_jm.h>
#endif
#ifdef CONFIG_MALI_PRFCNT_SET_SELECT_VIA_DEBUG_FS
#include <mali_kbase_hwaccess_instr.h>
#endif
#include <mali_kbase_reset_gpu.h>
#include <uapi/gpu/arm/midgard/mali_kbase_ioctl.h>
#if !MALI_USE_CSF
#include "mali_kbase_kinstr_jm.h"
#endif
#include "hwcnt/mali_kbase_hwcnt_context.h"
#include "hwcnt/mali_kbase_hwcnt_virtualizer.h"
#include "mali_kbase_kinstr_prfcnt.h"
#if MALI_USE_CSF
#include "csf/mali_kbase_csf_firmware.h"
#include "csf/mali_kbase_csf_tiler_heap.h"
#include "csf/mali_kbase_csf_csg_debugfs.h"
#include "csf/mali_kbase_csf_cpu_queue.h"
#include "csf/mali_kbase_csf_event.h"
#if IS_ENABLED(CONFIG_MALI_MTK_WHITEBOX_MISSING_DOORBELL)
#include "csf/mali_kbase_csf_db_validation.h"
#endif
#endif
#ifdef CONFIG_MALI_ARBITER_SUPPORT
#include "arbiter/mali_kbase_arbiter_pm.h"
#endif

#include "mali_kbase_cs_experimental.h"

#ifdef CONFIG_MALI_CINSTR_GWT
#include "mali_kbase_gwt.h"
#endif
#include "backend/gpu/mali_kbase_pm_internal.h"
#include "mali_kbase_dvfs_debugfs.h"
#if IS_ENABLED(CONFIG_DEBUG_FS)
#include "mali_kbase_pbha_debugfs.h"
#endif

#include <linux/module.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/miscdevice.h>
#include <linux/list.h>
#include <linux/semaphore.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mm.h>
#include <linux/compat.h>
#include <linux/mman.h>
#include <linux/version.h>
#include <linux/version_compat_defs.h>
#include <mali_kbase_hw.h>
#if IS_ENABLED(CONFIG_SYNC_FILE)
#include <mali_kbase_sync.h>
#endif
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/log2.h>
#include <linux/mali_hw_access.h>

#include <mali_kbase_config.h>

#include <linux/pm_opp.h>
#include <linux/pm_runtime.h>

#include <tl/mali_kbase_timeline.h>

#include <mali_kbase_as_fault_debugfs.h>
#include <device/mali_kbase_device.h>
#include <context/mali_kbase_context.h>

#include <mali_kbase_caps.h>

#if IS_ENABLED(CONFIG_MALI_MTK_DEBUG_FS) || IS_ENABLED(CONFIG_MALI_MTK_DEBUG_DUMP)
#include <platform/mtk_platform_common.h>
#endif

#if IS_ENABLED(CONFIG_MALI_MTK_LOG_BUFFER)
#include <platform/mtk_platform_common/mtk_platform_logbuffer.h>
#endif

#if IS_ENABLED(CONFIG_MALI_MTK_SLC_ALL_CACHE_MODE)
#include <mtk_heap.h>
#include <slbc_ops.h>
#include <linux/memory_group_manager.h>
#endif

#if IS_ENABLED(CONFIG_MALI_MTK_GPU_IDLE_STRESS_TEST)
#include <platform/mtk_platform_common/mtk_platform_gpu_idle_test.h>
#endif

#if IS_ENABLED(CONFIG_MALI_MTK_PAGE_FAULT_WB_TILER_RECLAIM)
#include "csf/mali_kbase_csf_tiler_heap_reclaim.h"
#endif
#if IS_ENABLED(CONFIG_MALI_MIDGARD_DVFS) && \
	IS_ENABLED(CONFIG_MALI_MTK_DVFS_POLICY) && \
	IS_ENABLED(CONFIG_MALI_MTK_GPU_DVFS_HINT_26M_LOADING)
#include "platform/mtk_platform_common/mtk_platform_dvfs_hint_26m_perf_cnting_ex.h"
#endif

#if IS_ENABLED(CONFIG_MALI_MTK_CROSS_QUEUE_SYNC_RECOVERY)
#include <platform/mtk_platform_common/mtk_platform_qinspect_recovery.h>
#endif

#if IS_ENABLED(CONFIG_MALI_MTK_GPU_BM_2)
#include <gpu_bm.h>
#if IS_ENABLED(CONFIG_MTK_TINYSYS_SSPM_SUPPORT)
#include <sspm_reservedmem_define.h>
static phys_addr_t rec_phys_addr, rec_virt_addr;
static unsigned long long rec_size;
struct v1_data *gpu_info_ref;
#endif
#endif

#if defined(CONFIG_MALI_MTK_GPU_BM_CSF)
#include <ged_gpu_bm.h>
#endif

#if IS_ENABLED(CONFIG_MALI_MTK_MBRAIN_SUPPORT)
#include <ged_mali_event.h>
#include <platform/mtk_platform_common/mtk_platform_mali_event.h>
#endif

#if IS_ENABLED(CONFIG_MALI_MTK_SLC_DYNAMIC_POLICY_V2)
#include <gpu_pdma.h>
#endif

#define KERNEL_SIDE_DDK_VERSION_STRING "K:" MALI_RELEASE_NAME "(GPL)"

#define KBASE_API_VERSION(major, minor) \
	((((major)&0xFFFU) << 20U) | (((minor)&0xFFFU) << 8U) | ((0U & 0xFFU) << 0U))

struct mali_kbase_capability_def {
	u16 required_major;
	u16 required_minor;
};

static const struct mali_kbase_capability_def kbase_caps_table[MALI_KBASE_NUM_CAPS] = {
#if MALI_USE_CSF
	{ 1, 0 },
	{ 1, 0 },
	{ 1, 22 },
	{ 1, 0 },
	{ 1, 0 },
	{ 1, 26 },
	{ 1, 26 },
	{ 1, 28 }
#else
	{ 11, 15 },
	{ 11, 25 },
	{ 11, 40 },
	{ 11, 2 },
	{ 11, 2 },
	{ 11, 43 },
	{ 11, 43 },
	{ 11, 44 }
#endif
};

#if (KERNEL_VERSION(5, 3, 0) <= LINUX_VERSION_CODE)

static struct mutex kbase_probe_mutex;
#endif

#if IS_ENABLED(CONFIG_MALI_MTK_GPU_BM_2)
#if IS_ENABLED(CONFIG_MTK_TINYSYS_SSPM_SUPPORT)
static void get_rec_addr(void)
{
	int i;
	unsigned char *ptr;

	pr_info("%s: [GPU_QOS] start to get sspm reserved memory\n", __func__);

	rec_phys_addr = sspm_reserve_mem_get_phys(GPU_MEM_ID);
	rec_virt_addr = sspm_reserve_mem_get_virt(GPU_MEM_ID);
	rec_size = sspm_reserve_mem_get_size(GPU_MEM_ID);
	pr_info("[GPU_QOS] DRAM physical memory addr= %x\n",
		(unsigned int)rec_phys_addr);
	pr_info("[GPU_QOS] DRAM virtual memory addr= %x size= %llu\n",
		(unsigned int)rec_virt_addr, rec_size);

	if (rec_virt_addr) {

		ptr = (unsigned char *)(uintptr_t)rec_virt_addr;
		for (i = 0; i < rec_size; i++)
			ptr[i] = 0x0;

		gpu_info_ref = (struct v1_data *)(uintptr_t)rec_virt_addr;
	}
#endif
}

static int mtk_bandwith_resource_init(struct kbase_device *kbdev)
{
	int err = 0;

	pr_info("%s: [GPU_QOS] try to get rec addr\n", __func__);

#if IS_ENABLED(CONFIG_MTK_TINYSYS_SSPM_SUPPORT)
	get_rec_addr();

	if(gpu_info_ref == NULL) {
		err = -1;
		pr_info("%s: [GPU_QOS] get sspm reserved memory fail\n", __func__);
		return err;
	}
	kbdev->v1 = gpu_info_ref;
	kbdev->v1->version = 1;
	kbdev->job_status_addr.phyaddr = rec_phys_addr;
	MTKGPUQoS_setup(kbdev->v1, kbdev->job_status_addr.phyaddr, rec_size);
#endif
	return err;
}
#endif

bool mali_kbase_supports_cap(unsigned long api_version, enum mali_kbase_cap cap)
{
	bool supported = false;
	unsigned long required_ver;

	struct mali_kbase_capability_def const *cap_def;

	if (WARN_ON(cap >= MALI_KBASE_NUM_CAPS))
		return false;

	cap_def = &kbase_caps_table[cap];
	required_ver = KBASE_API_VERSION(cap_def->required_major, cap_def->required_minor);
	supported = (api_version >= required_ver);

	return supported;
}

static struct kbase_file *kbase_file_new(struct kbase_device *const kbdev, struct file *const filp)
{
	struct kbase_file *const kfile = kmalloc(sizeof(*kfile), GFP_KERNEL);

	if (kfile) {
		kfile->kbdev = kbdev;
		kfile->filp = filp;
		kfile->kctx = NULL;
		kfile->api_version = 0;
		atomic_set(&kfile->setup_state, KBASE_FILE_NEED_VSN);
	}
	return kfile;
}

static int kbase_file_set_api_version(struct kbase_file *const kfile, u16 const major,
				      u16 const minor)
{
	if (WARN_ON(!kfile))
		return -EINVAL;

	if (atomic_cmpxchg(&kfile->setup_state, KBASE_FILE_NEED_VSN, KBASE_FILE_VSN_IN_PROGRESS) !=
	    KBASE_FILE_NEED_VSN)
		return -EPERM;

	kfile->api_version = KBASE_API_VERSION(major, minor);

	atomic_set(&kfile->setup_state, KBASE_FILE_NEED_CTX);
	return 0;
}

static unsigned long kbase_file_get_api_version(struct kbase_file *const kfile)
{
	if (WARN_ON(!kfile))
		return 0;

	if (atomic_read(&kfile->setup_state) < KBASE_FILE_NEED_CTX)
		return 0;

	return kfile->api_version;
}

static int kbase_file_create_kctx(struct kbase_file *kfile, base_context_create_flags flags);

static struct kbase_context *kbase_file_get_kctx_if_setup_complete(struct kbase_file *const kfile)
{
	if (WARN_ON(!kfile) || atomic_read(&kfile->setup_state) != KBASE_FILE_COMPLETE ||
	    WARN_ON(!kfile->kctx))
		return NULL;

	return kfile->kctx;
}

static void kbase_file_delete(struct kbase_file *const kfile)
{
	struct kbase_device *kbdev = NULL;

	if (WARN_ON(!kfile))
		return;

	kfile->filp->private_data = NULL;
	kbdev = kfile->kbdev;

	if (atomic_read(&kfile->setup_state) == KBASE_FILE_COMPLETE) {
		struct kbase_context *kctx = kfile->kctx;

#if IS_ENABLED(CONFIG_DEBUG_FS)
		kbasep_mem_profile_debugfs_remove(kctx);
#endif
		kbase_context_debugfs_term(kctx);

		kbase_destroy_context(kctx);

		dev_dbg(kbdev->dev, "deleted base context\n");
	}

	kbase_release_device(kbdev);

	kfree(kfile);
}

static int kbase_api_handshake(struct kbase_file *kfile, struct kbase_ioctl_version_check *version)
{
	int err = 0;

	switch (version->major) {
	case BASE_UK_VERSION_MAJOR:

		version->minor = min_t(int, BASE_UK_VERSION_MINOR, (int)version->minor);
		break;
	default:

		version->major = BASE_UK_VERSION_MAJOR;
		version->minor = BASE_UK_VERSION_MINOR;
		break;
	}

	err = kbase_file_set_api_version(kfile, version->major, version->minor);
	if (unlikely(err))
		return err;

	if (!mali_kbase_supports_system_monitor(kbase_file_get_api_version(kfile)))
		err = kbase_file_create_kctx(kfile, BASE_CONTEXT_SYSTEM_MONITOR_SUBMIT_DISABLED);

	return err;
}

static int kbase_api_handshake_dummy(struct kbase_file *kfile,
				     struct kbase_ioctl_version_check *version)
{
	CSTD_UNUSED(kfile);
	CSTD_UNUSED(version);

	return -EPERM;
}

static int
kbase_api_kinstr_prfcnt_enum_info(struct kbase_file *kfile,
				  struct kbase_ioctl_kinstr_prfcnt_enum_info *prfcnt_enum_info)
{
	return kbase_kinstr_prfcnt_enum_info(kfile->kbdev->kinstr_prfcnt_ctx, prfcnt_enum_info);
}

static int kbase_api_kinstr_prfcnt_setup(struct kbase_file *kfile,
					 union kbase_ioctl_kinstr_prfcnt_setup *prfcnt_setup)
{
	return kbase_kinstr_prfcnt_setup(kfile->kbdev->kinstr_prfcnt_ctx, prfcnt_setup);
}

static struct kbase_device *to_kbase_device(struct device *dev)
{
	return dev_get_drvdata(dev);
}

static int get_irqs(struct kbase_device *kbdev, struct platform_device *pdev)
{
	int i;
	static const char *const irq_names_caps[] = { "JOB", "MMU", "GPU" };

	for (i = 0; i < ARRAY_SIZE(irq_names_caps); i++) {
		struct irq_data *irqdata;
		int irq;

		irq = platform_get_irq_byname(pdev, irq_names_caps[i]);
		if (irq < 0) {
			static const char *const irq_names[] = { "job", "mmu", "gpu" };

			irq = platform_get_irq_byname(pdev, irq_names[i]);
		}

		if (irq < 0)
			return irq;

		kbdev->irqs[i].irq = (u32)irq;
		irqdata = irq_get_irq_data((unsigned int)irq);
		if (likely(irqdata))
			kbdev->irqs[i].flags = irqd_get_trigger_type(irqdata);
		else
			return -EINVAL;

		kbdev->nr_irqs++;
	}

	return 0;
}

int kbase_get_irqs(struct kbase_device *kbdev)
{
	int result;
	struct platform_device *pdev = to_platform_device(kbdev->dev);

	kbdev->nr_irqs = 0;
	result = get_irqs(kbdev, pdev);
	if (!result)
		return result;

	if (result)
		dev_err(kbdev->dev, "Invalid or No interrupt resources");

	return result;
}

struct kbase_device *kbase_find_device(int minor)
{
	struct kbase_device *kbdev = NULL;
	struct list_head *entry;
	const struct list_head *dev_list = kbase_device_get_list();

	list_for_each(entry, dev_list) {
		struct kbase_device *tmp;

		tmp = list_entry(entry, struct kbase_device, entry);
		if (tmp->mdev.minor == minor || minor == -1) {
			kbdev = tmp;
			get_device(kbdev->dev);
			break;
		}
	}
	kbase_device_put_list(dev_list);

	return kbdev;
}
EXPORT_SYMBOL(kbase_find_device);

void kbase_release_device(struct kbase_device *kbdev)
{
	put_device(kbdev->dev);
}
EXPORT_SYMBOL(kbase_release_device);

#if IS_ENABLED(CONFIG_DEBUG_FS)
static ssize_t write_ctx_infinite_cache(struct file *f, const char __user *ubuf, size_t size,
					loff_t *off)
{
	struct kbase_context *kctx = f->private_data;
	int err;
	bool value;

	CSTD_UNUSED(off);

	err = kstrtobool_from_user(ubuf, size, &value);
	if (err)
		return err;

	if (value)
		kbase_ctx_flag_set(kctx, KCTX_INFINITE_CACHE);
	else
		kbase_ctx_flag_clear(kctx, KCTX_INFINITE_CACHE);

	return (ssize_t)size;
}

static ssize_t read_ctx_infinite_cache(struct file *f, char __user *ubuf, size_t size, loff_t *off)
{
	struct kbase_context *kctx = f->private_data;
	char buf[32];
	size_t count;
	bool value;

	value = kbase_ctx_flag(kctx, KCTX_INFINITE_CACHE);

	count = (size_t)scnprintf(buf, sizeof(buf), "%s\n", value ? "Y" : "N");

	return simple_read_from_buffer(ubuf, size, off, buf, count);
}

static const struct file_operations kbase_infinite_cache_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = write_ctx_infinite_cache,
	.read = read_ctx_infinite_cache,
};

static ssize_t write_ctx_force_same_va(struct file *f, const char __user *ubuf, size_t size,
				       loff_t *off)
{
	struct kbase_context *kctx = f->private_data;
	int err;
	bool value;

	CSTD_UNUSED(off);

	err = kstrtobool_from_user(ubuf, size, &value);
	if (err)
		return err;

	if (value) {
#if defined(CONFIG_64BIT)

		if (kbase_ctx_flag(kctx, KCTX_COMPAT))
			return -EINVAL;
		kbase_ctx_flag_set(kctx, KCTX_FORCE_SAME_VA);
#else

		return -EINVAL;
#endif
	} else {
		kbase_ctx_flag_clear(kctx, KCTX_FORCE_SAME_VA);
	}

	return (ssize_t)size;
}

static ssize_t read_ctx_force_same_va(struct file *f, char __user *ubuf, size_t size, loff_t *off)
{
	struct kbase_context *kctx = f->private_data;
	char buf[32];
	size_t count;
	bool value;

	value = kbase_ctx_flag(kctx, KCTX_FORCE_SAME_VA);

	count = (size_t)scnprintf(buf, sizeof(buf), "%s\n", value ? "Y" : "N");

	return simple_read_from_buffer(ubuf, size, off, buf, count);
}

static const struct file_operations kbase_force_same_va_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = write_ctx_force_same_va,
	.read = read_ctx_force_same_va,
};
#endif

static int kbase_file_create_kctx(struct kbase_file *const kfile,
				  base_context_create_flags const flags)
{
	struct kbase_device *kbdev = NULL;
	struct kbase_context *kctx = NULL;
#if IS_ENABLED(CONFIG_DEBUG_FS)
	char kctx_name[64];
#endif

	if (WARN_ON(!kfile))
		return -EINVAL;

	if (atomic_cmpxchg(&kfile->setup_state, KBASE_FILE_NEED_CTX, KBASE_FILE_CTX_IN_PROGRESS) !=
	    KBASE_FILE_NEED_CTX)
		return -EPERM;

	kbdev = kfile->kbdev;

	kctx = kbase_create_context(kbdev, in_compat_syscall(), flags, kfile->api_version,
				    kfile->filp);

	if (!kctx)
		return -ENOMEM;

	if (kbdev->infinite_cache_active_default)
		kbase_ctx_flag_set(kctx, KCTX_INFINITE_CACHE);

#if IS_ENABLED(CONFIG_DEBUG_FS)
	if (unlikely(!scnprintf(kctx_name, 64, "%d_%d", kctx->tgid, kctx->id)))
		return -ENOMEM;

	mutex_init(&kctx->mem_profile_lock);

	kctx->kctx_dentry = debugfs_create_dir(kctx_name, kbdev->debugfs_ctx_directory);

	if (IS_ERR_OR_NULL(kctx->kctx_dentry)) {

		dev_warn(kbdev->dev, "couldn't create debugfs dir for kctx\n");
	} else {
		debugfs_create_file("infinite_cache", 0644, kctx->kctx_dentry, kctx,
				    &kbase_infinite_cache_fops);
		debugfs_create_file("force_same_va", 0600, kctx->kctx_dentry, kctx,
				    &kbase_force_same_va_fops);

		kbase_context_debugfs_init(kctx);
	}
#endif

	dev_dbg(kbdev->dev, "created base context\n");

	kfile->kctx = kctx;
	atomic_set(&kfile->setup_state, KBASE_FILE_COMPLETE);

	return 0;
}

static int kbase_open(struct inode *inode, struct file *filp)
{
	struct kbase_device *kbdev = NULL;
	struct kbase_file *kfile;
	int ret = 0;

	kbdev = kbase_find_device((int)iminor(inode));

	if (!kbdev)
		return -ENODEV;

#if (KERNEL_VERSION(6, 0, 0) > LINUX_VERSION_CODE)

	kbase_mem_migrate_set_address_space_ops(kbdev, filp);
#endif

	ret = kbase_device_firmware_init_once(kbdev);
	if (ret)
		goto out;

	kfile = kbase_file_new(kbdev, filp);
	if (!kfile) {
		ret = -ENOMEM;
		goto out;
	}

	filp->private_data = kfile;
#if (KERNEL_VERSION(6, 12, 0) > LINUX_VERSION_CODE)
	filp->f_mode |= FMODE_UNSIGNED_OFFSET;
#endif

	return 0;

out:
	kbase_release_device(kbdev);
	return ret;
}

static int kbase_release(struct inode *inode, struct file *filp)
{
	struct kbase_file *const kfile = filp->private_data;

	CSTD_UNUSED(inode);

	kbase_file_delete(kfile);
	return 0;
}

static int kbase_api_set_flags(struct kbase_file *kfile, struct kbase_ioctl_set_flags *flags)
{
	int err = 0;
	unsigned long const api_version = kbase_file_get_api_version(kfile);
	struct kbase_context *kctx = NULL;

	if (flags->create_flags != (flags->create_flags & BASEP_CONTEXT_CREATE_KERNEL_FLAGS))
		return -EINVAL;

	if (mali_kbase_supports_system_monitor(api_version)) {
		err = kbase_file_create_kctx(kfile, flags->create_flags);
	} else {
#if !MALI_USE_CSF
		struct kbasep_js_kctx_info *js_kctx_info = NULL;
		unsigned long irq_flags = 0;
#endif

		kctx = kbase_file_get_kctx_if_setup_complete(kfile);
		if (unlikely(!kctx))
			return -EPERM;

#if MALI_USE_CSF

#else
		js_kctx_info = &kctx->jctx.sched_info;
		mutex_lock(&js_kctx_info->ctx.jsctx_mutex);
		spin_lock_irqsave(&kctx->kbdev->hwaccess_lock, irq_flags);

		if ((flags->create_flags & BASE_CONTEXT_SYSTEM_MONITOR_SUBMIT_DISABLED) == 0)
			kbase_ctx_flag_clear(kctx, KCTX_SUBMIT_DISABLED);

		spin_unlock_irqrestore(&kctx->kbdev->hwaccess_lock, irq_flags);
		mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);
#endif
	}

	return err;
}

#if !MALI_USE_CSF
static int kbase_api_job_submit(struct kbase_context *kctx, struct kbase_ioctl_job_submit *submit)
{
	return kbase_jd_submit(kctx, u64_to_user_ptr(submit->addr), submit->nr_atoms,
			       submit->stride, false);
}
#endif

static int kbase_api_get_gpuprops(struct kbase_file *kfile,
				  struct kbase_ioctl_get_gpuprops *get_props)
{
	struct kbase_gpu_props *kprops = &kfile->kbdev->gpu_props;
	int err;

	if (get_props->flags != 0) {
		dev_err(kfile->kbdev->dev, "Unsupported flags to get_gpuprops");
		return -EINVAL;
	}

	if (get_props->size == 0)
		return (int)kprops->prop_buffer_size;
	if (get_props->size < kprops->prop_buffer_size)
		return -EINVAL;

	err = copy_to_user(u64_to_user_ptr(get_props->buffer), kprops->prop_buffer,
			   kprops->prop_buffer_size);
	if (err)
		return -EFAULT;
	return (int)kprops->prop_buffer_size;
}

#if !MALI_USE_CSF
static int kbase_api_post_term(struct kbase_context *kctx)
{
	kbase_event_close(kctx);
	return 0;
}
#endif

#if MALI_USE_CSF
static int kbase_api_mem_alloc_ex(struct kbase_context *kctx,
				  union kbase_ioctl_mem_alloc_ex *alloc_ex)
{
	struct kbase_va_region *reg;
	u64 flags = alloc_ex->in.flags;
	u64 gpu_va;

	const enum kbase_caller_mmu_sync_info mmu_sync_info = CALLER_MMU_ASYNC;

	bool gpu_executable = (flags & BASE_MEM_PROT_GPU_EX) && kbase_has_exec_va_zone(kctx);
	bool fixed_or_fixable = (flags & (BASE_MEM_FIXED | BASE_MEM_FIXABLE));

	if (!kbase_mem_allow_alloc(kctx))
		return -EINVAL;

	if ((flags & BASE_MEM_FIXED) && (atomic64_read(&kctx->num_fixable_allocs) > 0))
		return -EINVAL;

	if ((flags & BASE_MEM_FIXABLE) && (atomic64_read(&kctx->num_fixed_allocs) > 0))
		return -EINVAL;

	if (flags & BASEP_MEM_FLAGS_KERNEL_ONLY)
		return -ENOMEM;

	if (flags & BASE_MEM_FIXED) {
		u64 aligned_fixed_address = alloc_ex->in.fixed_address & PAGE_MASK;

		if ((aligned_fixed_address == 0) ||
		    (aligned_fixed_address != alloc_ex->in.fixed_address))
			return -EINVAL;

		gpu_va = aligned_fixed_address;
	} else if (alloc_ex->in.fixed_address != 0) {
		return -EINVAL;
	}

	if ((!kbase_ctx_flag(kctx, KCTX_COMPAT)) && kbase_ctx_flag(kctx, KCTX_FORCE_SAME_VA)) {
		if (!gpu_executable && !fixed_or_fixable)
			flags |= BASE_MEM_SAME_VA;
	}

	if (flags & BASE_MEM_CSF_EVENT) {

		if (gpu_executable || fixed_or_fixable)
			return -ENOMEM;

		flags |= (BASE_MEM_SAME_VA | BASE_MEM_CACHED_CPU | BASE_MEM_COHERENT_SYSTEM);
	}

#if IS_ENABLED(CONFIG_MALI_MTK_SLC_DYNAMIC_POLICY_V2)
	if (alloc_ex->in.have_pbha_hint && !(flags & BASE_MEM_FIXED))
		gpu_va = 0xF0;
#endif

#if IS_ENABLED(CONFIG_MALI_MTK_MEMORY_DEBUG)
	reg = kbase_mem_alloc(kctx, alloc_ex->in.va_pages, alloc_ex->in.commit_pages,
			      alloc_ex->in.extension, &flags, &gpu_va, mmu_sync_info, KBASE_MEM_API);
#else
	reg = kbase_mem_alloc(kctx, alloc_ex->in.va_pages, alloc_ex->in.commit_pages,
			      alloc_ex->in.extension, &flags, &gpu_va, mmu_sync_info);
#endif

	if (!reg)
		return -ENOMEM;

	alloc_ex->out.flags = flags;
	alloc_ex->out.gpu_va = gpu_va;
#if IS_ENABLED(CONFIG_MALI_MTK_SLC_DYNAMIC_POLICY_V2)
	alloc_ex->out.pbha_8bit = reg->pbha_8bit;
#endif

	return 0;
}

static int kbase_api_mem_alloc(struct kbase_context *kctx, union kbase_ioctl_mem_alloc *alloc)
{
	int ret;
	union kbase_ioctl_mem_alloc_ex mem_alloc_ex = { { 0 } };

	mem_alloc_ex.in.va_pages = alloc->in.va_pages;
	mem_alloc_ex.in.commit_pages = alloc->in.commit_pages;
	mem_alloc_ex.in.extension = alloc->in.extension;
	mem_alloc_ex.in.flags = alloc->in.flags;
	mem_alloc_ex.in.fixed_address = 0;

	ret = kbase_api_mem_alloc_ex(kctx, &mem_alloc_ex);

	alloc->out.flags = mem_alloc_ex.out.flags;
	alloc->out.gpu_va = mem_alloc_ex.out.gpu_va;

	return ret;
}
#else
static int kbase_api_mem_alloc(struct kbase_context *kctx, union kbase_ioctl_mem_alloc *alloc)
{
	struct kbase_va_region *reg;
	u64 flags = alloc->in.flags;
	u64 gpu_va;

	const enum kbase_caller_mmu_sync_info mmu_sync_info = CALLER_MMU_ASYNC;

	if (!kbase_mem_allow_alloc(kctx))
		return -EINVAL;

	if (flags & BASEP_MEM_FLAGS_KERNEL_ONLY)
		return -ENOMEM;

	if ((!kbase_ctx_flag(kctx, KCTX_COMPAT)) && kbase_ctx_flag(kctx, KCTX_FORCE_SAME_VA)) {
		if (!(flags & BASE_MEM_PROT_GPU_EX) || !kbase_has_exec_va_zone(kctx))
			flags |= BASE_MEM_SAME_VA;
	}

	reg = kbase_mem_alloc(kctx, alloc->in.va_pages, alloc->in.commit_pages, alloc->in.extension,
			      &flags, &gpu_va, mmu_sync_info);

	if (!reg)
		return -ENOMEM;

	alloc->out.flags = flags;
	alloc->out.gpu_va = gpu_va;

	return 0;
}
#endif

static int kbase_api_mem_query(struct kbase_context *kctx, union kbase_ioctl_mem_query *query)
{
	return kbase_mem_query(kctx, query->in.gpu_addr, query->in.query, &query->out.value);
}

static int kbase_api_mem_free(struct kbase_context *kctx, struct kbase_ioctl_mem_free *free)
{
	return kbase_mem_free(kctx, free->gpu_addr);
}

#if !MALI_USE_CSF
static int kbase_api_kinstr_jm_fd(struct kbase_context *kctx, union kbase_kinstr_jm_fd *arg)
{
	return kbase_kinstr_jm_get_fd(kctx->kinstr_jm, arg);
}
#endif

static int kbase_api_get_cpu_gpu_timeinfo(struct kbase_context *kctx,
					  union kbase_ioctl_get_cpu_gpu_timeinfo *timeinfo)
{
	u32 flags = timeinfo->in.request_flags;
	struct timespec64 ts = { 0 };
	u64 timestamp = 0;
	u64 cycle_cnt = 0;

	kbase_pm_context_active(kctx->kbdev);

	kbase_backend_get_gpu_time(kctx->kbdev,
				   (flags & BASE_TIMEINFO_CYCLE_COUNTER_FLAG) ? &cycle_cnt : NULL,
				   (flags & BASE_TIMEINFO_TIMESTAMP_FLAG) ? &timestamp : NULL,
				   (flags & BASE_TIMEINFO_MONOTONIC_FLAG) ? &ts : NULL);

	if (flags & BASE_TIMEINFO_TIMESTAMP_FLAG)
		timeinfo->out.timestamp = timestamp;

	if (flags & BASE_TIMEINFO_CYCLE_COUNTER_FLAG)
		timeinfo->out.cycle_counter = cycle_cnt;

	if (flags & BASE_TIMEINFO_MONOTONIC_FLAG) {
		timeinfo->out.sec = (u64)ts.tv_sec;
		timeinfo->out.nsec = (u32)ts.tv_nsec;
	}

	kbase_pm_context_idle(kctx->kbdev);

	return 0;
}

#if IS_ENABLED(CONFIG_MALI_NO_MALI)
static int kbase_api_hwcnt_set(struct kbase_context *kctx, struct kbase_ioctl_hwcnt_values *values)
{
	return gpu_model_set_dummy_prfcnt_user_sample(u64_to_user_ptr(values->data), values->size);
}
#endif

static int kbase_api_disjoint_query(struct kbase_context *kctx,
				    struct kbase_ioctl_disjoint_query *query)
{
	query->counter = kbase_disjoint_event_get(kctx->kbdev);

	return 0;
}

static int kbase_api_get_ddk_version(struct kbase_context *kctx,
				     struct kbase_ioctl_get_ddk_version *version)
{
	int ret;
	int len = sizeof(KERNEL_SIDE_DDK_VERSION_STRING);

	CSTD_UNUSED(kctx);

	if (version->version_buffer == 0)
		return len;

	if (version->size < (u32)len)
		return -EOVERFLOW;

	ret = copy_to_user(u64_to_user_ptr(version->version_buffer), KERNEL_SIDE_DDK_VERSION_STRING,
			   sizeof(KERNEL_SIDE_DDK_VERSION_STRING));

	if (ret)
		return -EFAULT;

	return len;
}

static int kbase_api_mem_jit_init(struct kbase_context *kctx,
				  struct kbase_ioctl_mem_jit_init *jit_init)
{
	size_t i;

	for (i = 0; i < sizeof(jit_init->padding); i++) {

		if (jit_init->padding[i])
			return -EINVAL;
	}

	return kbase_region_tracker_init_jit(kctx, jit_init->va_pages, jit_init->max_allocations,
					     jit_init->trim_level, jit_init->group_id,
					     jit_init->phys_pages);
}

static int kbase_api_mem_exec_init(struct kbase_context *kctx,
				   struct kbase_ioctl_mem_exec_init *exec_init)
{
	return kbase_region_tracker_init_exec(kctx, exec_init->va_pages);
}

static int kbase_api_mem_sync(struct kbase_context *kctx, struct kbase_ioctl_mem_sync *sync)
{
	struct basep_syncset sset = { .mem_handle.basep.handle = sync->handle,
				      .user_addr = sync->user_addr,
				      .size = sync->size,
				      .type = sync->type };

	return kbase_sync_now(kctx, &sset);
}

static int kbase_api_mem_find_cpu_offset(struct kbase_context *kctx,
					 union kbase_ioctl_mem_find_cpu_offset *find)
{
	return kbasep_find_enclosing_cpu_mapping_offset(kctx, find->in.cpu_addr, find->in.size,
							&find->out.offset);
}

static int
kbase_api_mem_find_gpu_start_and_offset(struct kbase_context *kctx,
					union kbase_ioctl_mem_find_gpu_start_and_offset *find)
{
	return kbasep_find_enclosing_gpu_mapping_start_and_offset(
		kctx, find->in.gpu_addr, find->in.size, &find->out.start, &find->out.offset);
}

static int kbase_api_get_context_id(struct kbase_context *kctx,
				    struct kbase_ioctl_get_context_id *info)
{
	info->id = kctx->id;

	return 0;
}

static int kbase_api_tlstream_acquire(struct kbase_context *kctx,
				      struct kbase_ioctl_tlstream_acquire *acquire)
{
	return kbase_timeline_io_acquire(kctx->kbdev, acquire->flags);
}

static int kbase_api_tlstream_flush(struct kbase_context *kctx)
{
	kbase_timeline_streams_flush(kctx->kbdev->timeline);

	return 0;
}

static int kbase_api_mem_commit(struct kbase_context *kctx, struct kbase_ioctl_mem_commit *commit)
{
	return kbase_mem_commit(kctx, commit->gpu_addr, commit->pages);
}

static int kbase_api_mem_alias(struct kbase_context *kctx, union kbase_ioctl_mem_alias *alias)
{
	struct base_mem_aliasing_info *ai;
	u64 flags;
	int err;
	size_t copy_size;

	if (alias->in.nents == 0 || alias->in.nents > BASE_MEM_ALIAS_MAX_ENTS) {
		err = -EINVAL;
		goto exit;
	}

	ai = vmalloc(sizeof(*ai) * alias->in.nents);
	if (!ai) {
		err = -ENOMEM;
		goto exit;
	}

	if (check_mul_overflow(sizeof(*ai), (size_t)alias->in.nents, &copy_size)) {
		err = -EINVAL;
		goto free_alloc;
	}

	err = copy_from_user(ai, u64_to_user_ptr(alias->in.aliasing_info), copy_size);
	if (err) {
		err = -EFAULT;
		goto free_alloc;
	}

	flags = alias->in.flags;
	if (flags & BASEP_MEM_FLAGS_KERNEL_ONLY) {
		err = -EINVAL;
		goto free_alloc;
	}

	alias->out.gpu_va = kbase_mem_alias(kctx, &flags, alias->in.stride, alias->in.nents, ai,
					    &alias->out.va_pages);

	alias->out.flags = flags;
	if (alias->out.gpu_va == 0)
		err = -ENOMEM;
free_alloc:
	vfree(ai);
exit:
	return err;
}

static int kbase_api_mem_import(struct kbase_context *kctx, union kbase_ioctl_mem_import *import)
{
	int ret;
	u64 flags = import->in.flags;

	if (flags & BASEP_MEM_FLAGS_KERNEL_ONLY)
		return -ENOMEM;

#if IS_ENABLED(CONFIG_MALI_MTK_SLC_DYNAMIC_POLICY_V2)
		u8 PBHA = import->in.in_pbha_8bit;
		if (import->in.vkAllocateMemoryCallID != 0) {

			bool isFirstDmaBuf = false;
			if (PBHA == 0) {
				PBHA = pdma_request_extended_pbha(kctx->id);
				isFirstDmaBuf = true;
			}

			ret = kbase_mem_import(kctx, import->in.type, u64_to_user_ptr(import->in.phandle),
					   import->in.padding, &import->out.gpu_va, &import->out.va_pages, &flags,
					   PBHA, isFirstDmaBuf);
			import->out.out_pbha_8bit = PBHA;
		}
		else {
			ret = kbase_mem_import(kctx, import->in.type, u64_to_user_ptr(import->in.phandle),
							   import->in.padding, &import->out.gpu_va, &import->out.va_pages, &flags, 0, false);
			import->out.out_pbha_8bit = 0;
		}
#else
	ret = kbase_mem_import(kctx, import->in.type, u64_to_user_ptr(import->in.phandle),
			       import->in.padding, &import->out.gpu_va, &import->out.va_pages,
			       &flags);
#endif

	import->out.flags = flags;

	return ret;
}

static int kbase_api_mem_flags_change(struct kbase_context *kctx,
				      struct kbase_ioctl_mem_flags_change *change)
{
	if (change->flags & BASEP_MEM_FLAGS_KERNEL_ONLY)
		return -ENOMEM;

	return kbase_mem_flags_change(kctx, change->gpu_va, change->flags, change->mask);
}

static int kbase_api_stream_create(struct kbase_context *kctx,
				   struct kbase_ioctl_stream_create *stream)
{
#if IS_ENABLED(CONFIG_SYNC_FILE)
	int fd, ret;

	CSTD_UNUSED(kctx);

	if (stream->name[sizeof(stream->name) - 1] != 0)
		return -EINVAL;

	ret = kbase_sync_fence_stream_create(stream->name, &fd);

	if (ret)
		return ret;
	return fd;
#else
	CSTD_UNUSED(kctx);
	CSTD_UNUSED(stream);

	return -ENOENT;
#endif
}

static int kbase_api_fence_validate(struct kbase_context *kctx,
				    struct kbase_ioctl_fence_validate *validate)
{
	CSTD_UNUSED(kctx);
#if IS_ENABLED(CONFIG_SYNC_FILE)
	return kbase_sync_fence_validate(validate->fd);
#else
	return -ENOENT;
#endif
}

static int kbase_api_mem_profile_add(struct kbase_context *kctx,
				     struct kbase_ioctl_mem_profile_add *data)
{
	char *buf;
	int err;

	if (data->len > KBASE_MEM_PROFILE_MAX_BUF_SIZE) {
		dev_err(kctx->kbdev->dev, "mem_profile_add: buffer too big");
		return -EINVAL;
	}

	if (!data->len) {
		dev_err(kctx->kbdev->dev, "mem_profile_add: buffer size is 0");

		return -ENOMEM;
	}

	buf = kmalloc(data->len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	err = copy_from_user(buf, u64_to_user_ptr(data->buffer), data->len);
	if (err) {
		kfree(buf);
		return -EFAULT;
	}

	return kbasep_mem_profile_debugfs_insert(kctx, buf, data->len);
}

#if !MALI_USE_CSF
static int kbase_api_soft_event_update(struct kbase_context *kctx,
				       struct kbase_ioctl_soft_event_update *update)
{
	if (update->flags != 0)
		return -EINVAL;

	return kbase_soft_event_update(kctx, update->event, update->new_status);
}
#endif

static int kbase_api_sticky_resource_map(struct kbase_context *kctx,
					 struct kbase_ioctl_sticky_resource_map *map)
{
	int ret;
	u64 i;
	u64 gpu_addr[BASE_EXT_RES_COUNT_MAX];
	size_t copy_size;

	if (!map->count || map->count > BASE_EXT_RES_COUNT_MAX)
		return -EOVERFLOW;

	if (check_mul_overflow(sizeof(u64), (size_t)map->count, &copy_size))
		return -EINVAL;

	ret = copy_from_user(gpu_addr, u64_to_user_ptr(map->address), copy_size);

	if (ret != 0)
		return -EFAULT;

	down_read(kbase_mem_get_process_mmap_lock());
	kbase_gpu_vm_lock_with_pmode_sync(kctx);

	for (i = 0; i < map->count; i++) {
		if (!kbase_sticky_resource_acquire(kctx, gpu_addr[i], current->mm)) {

			ret = -EINVAL;
			break;
		}
	}

	if (ret != 0) {
		while (i > 0) {
			i--;
			kbase_sticky_resource_release_force(kctx, NULL, gpu_addr[i]);
		}
	}

	kbase_gpu_vm_unlock_with_pmode_sync(kctx);
	up_read(kbase_mem_get_process_mmap_lock());

	return ret;
}

static int kbase_api_sticky_resource_unmap(struct kbase_context *kctx,
					   struct kbase_ioctl_sticky_resource_unmap *unmap)
{
	int ret;
	u64 i;
	u64 gpu_addr[BASE_EXT_RES_COUNT_MAX];
	size_t copy_size;

	if (!unmap->count || unmap->count > BASE_EXT_RES_COUNT_MAX)
		return -EOVERFLOW;

	if (check_mul_overflow(sizeof(u64), (size_t)unmap->count, &copy_size))
		return -EINVAL;

	ret = copy_from_user(gpu_addr, u64_to_user_ptr(unmap->address), copy_size);

	if (ret != 0)
		return -EFAULT;

	kbase_gpu_vm_lock_with_pmode_sync(kctx);

	for (i = 0; i < unmap->count; i++) {
		if (!kbase_sticky_resource_release_force(kctx, NULL, gpu_addr[i])) {

			ret = -EINVAL;
		}
	}

	kbase_gpu_vm_unlock_with_pmode_sync(kctx);

	return ret;
}

#if MALI_UNIT_TEST

static int kbase_api_tlstream_stats(struct kbase_context *kctx,
				    struct kbase_ioctl_tlstream_stats *stats)
{
	kbase_timeline_stats(kctx->kbdev->timeline, &stats->bytes_collected,
			     &stats->bytes_generated);

	return 0;
}
#endif

#if MALI_USE_CSF
static int kbasep_cs_event_signal(struct kbase_context *kctx)
{
	kbase_csf_event_signal_notify_gpu(kctx);
	return 0;
}

static int kbasep_cs_queue_register(struct kbase_context *kctx,
				    struct kbase_ioctl_cs_queue_register *reg)
{
	kctx->jit_group_id = BASE_MEM_GROUP_DEFAULT;

	return kbase_csf_queue_register(kctx, reg);
}

static int kbasep_cs_queue_register_ex(struct kbase_context *kctx,
				       struct kbase_ioctl_cs_queue_register_ex *reg)
{
	kctx->jit_group_id = BASE_MEM_GROUP_DEFAULT;

	return kbase_csf_queue_register_ex(kctx, reg);
}

static int kbasep_cs_queue_terminate(struct kbase_context *kctx,
				     struct kbase_ioctl_cs_queue_terminate *term)
{
	kbase_csf_queue_terminate(kctx, term);

	return 0;
}

static int kbasep_cs_queue_bind(struct kbase_context *kctx, union kbase_ioctl_cs_queue_bind *bind)
{
	return kbase_csf_queue_bind(kctx, bind);
}

static int kbasep_cs_queue_kick(struct kbase_context *kctx, struct kbase_ioctl_cs_queue_kick *kick)
{
	return kbase_csf_queue_kick(kctx, kick);
}

static int kbasep_queue_group_clear_faults(struct kbase_context *kctx,
					   struct kbase_ioctl_queue_group_clear_faults *faults)
{
	return kbase_csf_queue_group_clear_faults(kctx, faults);
}

static int kbasep_cs_queue_group_create_1_6(struct kbase_context *kctx,
					    union kbase_ioctl_cs_queue_group_create_1_6 *create)
{
	int ret;
	size_t i;
	union kbase_ioctl_cs_queue_group_create
		new_create = { .in = {
				       .tiler_mask = create->in.tiler_mask,
				       .fragment_mask = create->in.fragment_mask,
				       .compute_mask = create->in.compute_mask,
				       .cs_min = create->in.cs_min,
				       .priority = create->in.priority,
				       .tiler_max = create->in.tiler_max,
				       .fragment_max = create->in.fragment_max,
				       .compute_max = create->in.compute_max,
			       } };

	for (i = 0; i < ARRAY_SIZE(create->in.padding); i++) {
		if (create->in.padding[i] != 0)
			return -EINVAL;
	}

	ret = kbase_csf_queue_group_create(kctx, &new_create);

	create->out.group_handle = new_create.out.group_handle;
	create->out.group_uid = new_create.out.group_uid;

	return ret;
}

static int kbasep_cs_queue_group_create_1_18(struct kbase_context *kctx,
					     union kbase_ioctl_cs_queue_group_create_1_18 *create)
{
	int ret;
	size_t i;
	union kbase_ioctl_cs_queue_group_create
		new_create = { .in = {
				       .tiler_mask = create->in.tiler_mask,
				       .fragment_mask = create->in.fragment_mask,
				       .compute_mask = create->in.compute_mask,
				       .cs_min = create->in.cs_min,
				       .priority = create->in.priority,
				       .tiler_max = create->in.tiler_max,
				       .fragment_max = create->in.fragment_max,
				       .compute_max = create->in.compute_max,
				       .csi_handlers = create->in.csi_handlers,
				       .dvs_buf = create->in.dvs_buf,
			       } };

	for (i = 0; i < ARRAY_SIZE(create->in.padding); i++) {
		if (create->in.padding[i] != 0)
			return -EINVAL;
	}

	ret = kbase_csf_queue_group_create(kctx, &new_create);

	create->out.group_handle = new_create.out.group_handle;
	create->out.group_uid = new_create.out.group_uid;

	return ret;
}

static int kbasep_cs_queue_group_create(struct kbase_context *kctx,
					union kbase_ioctl_cs_queue_group_create *create)
{

	if (create->in.reserved != 0) {
		dev_warn(kctx->kbdev->dev, "Invalid reserved field not 0 in queue group create\n");
		return -EINVAL;
	}
	return kbase_csf_queue_group_create(kctx, create);
}

static int kbasep_cs_queue_group_terminate(struct kbase_context *kctx,
					   struct kbase_ioctl_cs_queue_group_term *term)
{
	kbase_csf_queue_group_terminate(kctx, term->group_handle);

	return 0;
}

static int kbasep_kcpu_queue_new(struct kbase_context *kctx, struct kbase_ioctl_kcpu_queue_new *new)
{
	return kbase_csf_kcpu_queue_new(kctx, new);
}

static int kbasep_kcpu_queue_delete(struct kbase_context *kctx,
				    struct kbase_ioctl_kcpu_queue_delete *delete)
{
	return kbase_csf_kcpu_queue_delete(kctx, delete);
}

static int kbasep_kcpu_queue_enqueue(struct kbase_context *kctx,
				     struct kbase_ioctl_kcpu_queue_enqueue *enqueue)
{
	return kbase_csf_kcpu_queue_enqueue(kctx, enqueue);
}

static int kbasep_cs_tiler_heap_init(struct kbase_context *kctx,
				     union kbase_ioctl_cs_tiler_heap_init *heap_init)
{
	if (heap_init->in.group_id >= MEMORY_GROUP_MANAGER_NR_GROUPS)
		return -EINVAL;

	kctx->jit_group_id = heap_init->in.group_id;

	return kbase_csf_tiler_heap_init(kctx, heap_init->in.chunk_size,
					 heap_init->in.initial_chunks, heap_init->in.max_chunks,
					 heap_init->in.target_in_flight, heap_init->in.buf_desc_va,
					 &heap_init->out.gpu_heap_va,
					 &heap_init->out.first_chunk_va);
}

static int kbasep_cs_tiler_heap_init_1_13(struct kbase_context *kctx,
					  union kbase_ioctl_cs_tiler_heap_init_1_13 *heap_init)
{
	if (heap_init->in.group_id >= MEMORY_GROUP_MANAGER_NR_GROUPS)
		return -EINVAL;

	kctx->jit_group_id = heap_init->in.group_id;

	return kbase_csf_tiler_heap_init(kctx, heap_init->in.chunk_size,
					 heap_init->in.initial_chunks, heap_init->in.max_chunks,
					 heap_init->in.target_in_flight, 0,
					 &heap_init->out.gpu_heap_va,
					 &heap_init->out.first_chunk_va);
}

static int kbasep_cs_tiler_heap_term(struct kbase_context *kctx,
				     struct kbase_ioctl_cs_tiler_heap_term *heap_term)
{
	return kbase_csf_tiler_heap_term(kctx, heap_term->gpu_heap_va);
}

static int kbase_ioctl_cs_get_glb_iface(struct kbase_context *kctx,
					union kbase_ioctl_cs_get_glb_iface *param)
{
	struct basep_cs_stream_control *stream_data = NULL;
	struct basep_cs_group_control *group_data = NULL;
	void __user *user_groups, *user_streams;
	int err = 0;
	u32 const max_group_num = param->in.max_group_num;
	u32 const max_total_stream_num = param->in.max_total_stream_num;
	size_t copy_size;

	if (max_group_num > MAX_SUPPORTED_CSGS)
		return -EINVAL;

	if (max_total_stream_num > MAX_SUPPORTED_CSGS * MAX_SUPPORTED_STREAMS_PER_GROUP)
		return -EINVAL;

	user_groups = u64_to_user_ptr(param->in.groups_ptr);
	user_streams = u64_to_user_ptr(param->in.streams_ptr);

	if (max_group_num > 0) {
		if (!user_groups)
			err = -EINVAL;
		else {
			group_data = kcalloc(max_group_num, sizeof(*group_data), GFP_KERNEL);
			if (!group_data)
				err = -ENOMEM;
		}
	}

	if (max_total_stream_num > 0) {
		if (!user_streams)
			err = -EINVAL;
		else {
			stream_data =
				kcalloc(max_total_stream_num, sizeof(*stream_data), GFP_KERNEL);
			if (!stream_data)
				err = -ENOMEM;
		}
	}

	if (!err) {
		param->out.total_stream_num = kbase_csf_firmware_get_glb_iface(
			kctx->kbdev, group_data, max_group_num, stream_data, max_total_stream_num,
			&param->out.glb_version, &param->out.features, &param->out.group_num,
			&param->out.prfcnt_size, &param->out.instr_features);

		if (check_mul_overflow((size_t)(MIN(max_group_num, param->out.group_num)),
				       sizeof(*group_data), &copy_size))
			err = -EINVAL;
	}

	if (!err) {
		if (copy_to_user(user_groups, group_data, copy_size))
			err = -EFAULT;
	}

	if (!err) {
		if (check_mul_overflow(
			    (size_t)(MIN(max_total_stream_num, param->out.total_stream_num)),
			    sizeof(*stream_data), &copy_size))
			err = -EINVAL;
	}

	if (!err) {
		if (copy_to_user(user_streams, stream_data, copy_size))
			err = -EFAULT;
	}

	kfree(group_data);
	kfree(stream_data);
	return err;
}

static int kbasep_ioctl_cs_cpu_queue_dump(struct kbase_context *kctx,
					  struct kbase_ioctl_cs_cpu_queue_info *cpu_queue_info)
{
	return kbase_csf_cpu_queue_dump_buffer(kctx, cpu_queue_info->buffer, cpu_queue_info->size);
}

static int kbase_ioctl_read_user_page(struct kbase_context *kctx,
				      union kbase_ioctl_read_user_page *user_page)
{
	struct kbase_device *kbdev = kctx->kbdev;
	unsigned long flags;

	if (unlikely(user_page->in.offset != LATEST_FLUSH))
		return -EINVAL;

	if (unlikely(user_page->in.padding != 0))
		return -EINVAL;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	if (!kbdev->pm.backend.gpu_powered)
		user_page->out.val_lo = POWER_DOWN_LATEST_FLUSH_VALUE;
	else
		user_page->out.val_lo = kbase_reg_read32(kbdev, USER_ENUM(LATEST_FLUSH));
	user_page->out.val_hi = 0;
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	return 0;
}
#endif

static int
kbasep_ioctl_context_priority_check(struct kbase_context *kctx,
				    struct kbase_ioctl_context_priority_check *priority_check)
{
#if MALI_USE_CSF
	priority_check->priority = kbase_csf_priority_check(kctx->kbdev, priority_check->priority);
#else
	base_jd_prio req_priority = (base_jd_prio)priority_check->priority;

	priority_check->priority = (u8)kbase_js_priority_check(kctx->kbdev, req_priority);
#endif
	return 0;
}

#define KBASE_HANDLE_IOCTL(cmd, function, arg)                                         \
	do {                                                                           \
		int ret;                                                               \
		BUILD_BUG_ON(_IOC_DIR(cmd) != _IOC_NONE);                              \
		dev_dbg(arg->kbdev->dev, "Enter ioctl %s\n", #function);               \
		ret = function(arg);                                                   \
		dev_dbg(arg->kbdev->dev, "Return %d from ioctl %s\n", ret, #function); \
		return ret;                                                            \
	} while (0)

#define KBASE_HANDLE_IOCTL_IN(cmd, function, type, arg)                                \
	do {                                                                           \
		type param;                                                            \
		int ret, err;                                                          \
		dev_dbg(arg->kbdev->dev, "Enter ioctl %s\n", #function);               \
		BUILD_BUG_ON(_IOC_DIR(cmd) != _IOC_WRITE);                             \
		BUILD_BUG_ON(sizeof(param) != _IOC_SIZE(cmd));                         \
		err = copy_from_user(&param, uarg, sizeof(param));                     \
		if (err)                                                               \
			return -EFAULT;                                                \
		ret = function(arg, &param);                                           \
		dev_dbg(arg->kbdev->dev, "Return %d from ioctl %s\n", ret, #function); \
		return ret;                                                            \
	} while (0)

#define KBASE_HANDLE_IOCTL_OUT(cmd, function, type, arg)                               \
	do {                                                                           \
		type param;                                                            \
		int ret, err;                                                          \
		dev_dbg(arg->kbdev->dev, "Enter ioctl %s\n", #function);               \
		BUILD_BUG_ON(_IOC_DIR(cmd) != _IOC_READ);                              \
		BUILD_BUG_ON(sizeof(param) != _IOC_SIZE(cmd));                         \
		memset(&param, 0, sizeof(param));                                      \
		ret = function(arg, &param);                                           \
		err = copy_to_user(uarg, &param, sizeof(param));                       \
		if (err)                                                               \
			return -EFAULT;                                                \
		dev_dbg(arg->kbdev->dev, "Return %d from ioctl %s\n", ret, #function); \
		return ret;                                                            \
	} while (0)

#define KBASE_HANDLE_IOCTL_INOUT(cmd, function, type, arg)                             \
	do {                                                                           \
		type param;                                                            \
		int ret, err;                                                          \
		dev_dbg(arg->kbdev->dev, "Enter ioctl %s\n", #function);               \
		BUILD_BUG_ON(_IOC_DIR(cmd) != (_IOC_WRITE | _IOC_READ));               \
		BUILD_BUG_ON(sizeof(param) != _IOC_SIZE(cmd));                         \
		err = copy_from_user(&param, uarg, sizeof(param));                     \
		if (err)                                                               \
			return -EFAULT;                                                \
		ret = function(arg, &param);                                           \
		err = copy_to_user(uarg, &param, sizeof(param));                       \
		if (err)                                                               \
			return -EFAULT;                                                \
		dev_dbg(arg->kbdev->dev, "Return %d from ioctl %s\n", ret, #function); \
		return ret;                                                            \
	} while (0)

static int kbasep_ioctl_set_limited_core_count(
	struct kbase_context *kctx,
	struct kbase_ioctl_set_limited_core_count *set_limited_core_count)
{
	const u64 shader_core_mask = kbase_pm_get_present_cores(kctx->kbdev, KBASE_PM_CORE_SHADER);
	const u8 max_core_count = set_limited_core_count->max_core_count;
	u64 limited_core_mask = 0;

	if (max_core_count > 64)
		return -EINVAL;
	else if (max_core_count == 64)
		limited_core_mask = UINT64_MAX;
	else
		limited_core_mask = ((u64)1 << max_core_count) - 1;

	if ((shader_core_mask & limited_core_mask) == 0)
		return -EINVAL;

	kctx->limited_core_mask = limited_core_mask;
	return 0;
}

#if IS_ENABLED(CONFIG_MALI_MTK_FENCE_DEBUG)
static int kbasep_ioctl_internal_fence_wait(struct kbase_context *kctx,
			struct kbase_ioctl_internal_fence_wait *fence_wait)
{
#if IS_ENABLED(CONFIG_MALI_MTK_MBRAIN_SUPPORT)
	if (fence_wait->time_in_microseconds == 2000 || fence_wait->time_in_microseconds == 3000 ||
		fence_wait->time_in_microseconds == 4000 || fence_wait->time_in_microseconds == 5000) {
		ged_mali_event_notify_fence_timeout_event(kctx->tgid, FENCE_TYPE_INTERNAL, fence_wait->time_in_microseconds/1000);
	}
#endif
	if (fence_wait->time_in_microseconds == 2000 || fence_wait->time_in_microseconds == 3000) {
#if IS_ENABLED(CONFIG_MALI_MTK_LOG_BUFFER)
		mtk_logbuffer_type_print(kctx->kbdev, MTK_LOGBUFFER_TYPE_CRITICAL | MTK_LOGBUFFER_TYPE_EXCEPTION | MTK_LOGBUFFER_TYPE_DEFERRED,
			"ctx:%d_%d cpu queue:%llx Internal fence wait timeouts(%llu ms)! flags=0x%x pid=%u\n",
			kctx->tgid, kctx->id, fence_wait->queue,
			fence_wait->time_in_microseconds,
			fence_wait->flags,
			fence_wait->pid);

		struct task_struct *task;
		struct pid *pid_struct;
		pid_struct = find_get_pid(kctx->tgid);
		if (pid_struct) {
			rcu_read_lock();
			task = pid_task(pid_struct, PIDTYPE_PID);
			if (task && task->group_leader && task->signal) {
				mtk_logbuffer_type_print(kctx->kbdev, MTK_LOGBUFFER_TYPE_DEFERRED | MTK_LOGBUFFER_TYPE_CRITICAL | MTK_LOGBUFFER_TYPE_EXCEPTION,
					"ctx:%d_%d, process_name:%s, state:0x%llx, exit_state:0x%llx, signal->flags:%lld\n",
					kctx->tgid, kctx->id, task->group_leader->comm, (unsigned long long) task->__state,
					(unsigned long long) task->exit_state, (unsigned long long) task->signal->flags);
			}
			rcu_read_unlock();
			put_pid(pid_struct);
		}
#else
		dev_info(kctx->kbdev->dev, "ctx:%d_%d cpu queue:%llx Internal fence wait timeouts(%llu ms)! flags=0x%x pid=%u",
			kctx->tgid, kctx->id, fence_wait->queue,
			fence_wait->time_in_microseconds,
			fence_wait->flags,
			fence_wait->pid);
#endif
		if (kctx->kbdev->scheduler_kthread_exec_begin_time > kctx->kbdev->scheduler_kthread_exec_end_time) {
			s64 blocked_time = ktime_to_ms(ktime_sub(ktime_get(), kctx->kbdev->scheduler_kthread_exec_begin_time));
			if (blocked_time > 100) {
				struct kbase_csf_scheduler *scheduler = &(kctx->kbdev)->csf.scheduler;
				unsigned int state = scheduler->gpuq_kthread->__state;
				dev_info(kctx->kbdev->dev,
					"ctx_%d_%d scheduler kthread was blocked (%llu ms), state=0x%x", kctx->tgid, kctx->id, blocked_time, state);
#if IS_ENABLED(CONFIG_MALI_MTK_LOG_BUFFER)
				mtk_logbuffer_type_print(kctx->kbdev, MTK_LOGBUFFER_TYPE_CRITICAL | MTK_LOGBUFFER_TYPE_EXCEPTION,
					"ctx_%d_%d scheduler kthread was blocked (%llu ms), state=0x%x\n", kctx->tgid, kctx->id, blocked_time, state);
#endif
#if IS_ENABLED(CONFIG_MALI_MTK_DEBUG_DUMP)

				if (state == 0x2 && scheduler->gpuq_kthread) {
					mtk_common_print_backtrace_for_task(kctx->kbdev, scheduler->gpuq_kthread);
				}
#endif
			}
		}
		if (fence_wait->flags & BASE_INTERNAL_FENCE_WAIT_DUMP_FLAG) {
#if IS_ENABLED(CONFIG_MALI_MTK_DEBUG_DUMP)
			struct kbase_context *kctx_pid;
			mtk_common_debug(MTK_COMMON_DBG_DUMP_INFRA_STATUS, kctx, MTK_DBG_HOOK_FENCE_INTERNAL_TIMEOUT);
			mtk_common_debug(MTK_COMMON_DBG_DUMP_PM_STATUS, kctx, MTK_DBG_HOOK_FENCE_INTERNAL_TIMEOUT);
			mtk_common_debug(MTK_COMMON_DBG_DUMP_DB_BY_SETTING, kctx, MTK_DBG_HOOK_FENCE_INTERNAL_TIMEOUT);
			if (kctx->tgid != (int)fence_wait->pid) {
				list_for_each_entry(kctx_pid, &kctx->kbdev->kctx_list, kctx_list_link) {
					if (kctx_pid->tgid == (int)fence_wait->pid) {
						mtk_common_debug(MTK_COMMON_DBG_CSF_DUMP_GROUPS_QUEUES, kctx_pid, MTK_DBG_HOOK_FENCE_INTERNAL_TIMEOUT);
						break;
					}
				}
			} else
				mtk_common_debug(MTK_COMMON_DBG_CSF_DUMP_GROUPS_QUEUES, kctx, MTK_DBG_HOOK_FENCE_INTERNAL_TIMEOUT);
#endif
		}
	}
#if IS_ENABLED(CONFIG_MALI_MTK_FENCE_TIMEOUT_RESET)
	if (fence_wait->time_in_microseconds == 3000) {
		if (kbase_prepare_to_reset_gpu(kctx->kbdev, RESET_FLAGS_NONE)) {
#if IS_ENABLED(CONFIG_MALI_MTK_LOG_BUFFER)
				mtk_logbuffer_type_print(kctx->kbdev, MTK_LOGBUFFER_TYPE_CRITICAL | MTK_LOGBUFFER_TYPE_EXCEPTION | MTK_LOGBUFFER_TYPE_DEFERRED,
					"ctx:%d_%d cpu queue:%llx Internal fence timeouts(%llu ms)! Trigger GPU reset\n",
					kctx->tgid, kctx->id, fence_wait->queue,
					fence_wait->time_in_microseconds);
#else
			dev_info(kctx->kbdev->dev, "ctx:%d_%d cpu queue:%llx Internal fence timeouts(%llu ms)! Trigger GPU reset",
						kctx->tgid, kctx->id, fence_wait->queue,
						fence_wait->time_in_microseconds);
#endif
#if IS_ENABLED(CONFIG_MALI_MTK_MBRAIN_SUPPORT)
			ged_mali_event_update_gpu_reset_nolock(GPU_RESET_INTERNAL_FENCE_TIMEOUT);
#endif
			kbase_reset_gpu(kctx->kbdev);
		} else {
#if IS_ENABLED(CONFIG_MALI_MTK_LOG_BUFFER)
			mtk_logbuffer_type_print(kctx->kbdev, MTK_LOGBUFFER_TYPE_CRITICAL | MTK_LOGBUFFER_TYPE_EXCEPTION | MTK_LOGBUFFER_TYPE_DEFERRED,
				"ctx:%d_%d cpu queue:%llx Internal fence timeouts(%llu ms)! Other threads are already resetting the GPU\n",
				kctx->tgid, kctx->id, fence_wait->queue,
				fence_wait->time_in_microseconds);
#else
			dev_info(kctx->kbdev->dev, "ctx:%d_%d cpu queue:%llx Internal fence timeouts(%llu ms)! Other threads are already resetting the GPU",
					 kctx->tgid, kctx->id, fence_wait->queue,
					 fence_wait->time_in_microseconds);
#endif
		}
	}

#if IS_ENABLED(CONFIG_MALI_MTK_LOG_BUFFER)
	if (fence_wait->time_in_microseconds == 5000) {

		mtk_logbuffer_dump_to_dev_and_clear(kctx->kbdev, MTK_LOGBUFFER_TYPE_DEFERRED);
	}
#endif

#endif
#if IS_ENABLED(CONFIG_MALI_MTK_CROSS_QUEUE_SYNC_RECOVERY)
	if ((fence_wait->time_in_microseconds == 4000 || fence_wait->time_in_microseconds == 5000 ||
		fence_wait->time_in_microseconds == 6000 || fence_wait->time_in_microseconds == 7000 ||
		fence_wait->time_in_microseconds == 8000 || fence_wait->time_in_microseconds == 9000) &&
		(fence_wait->queue != 0)) {
		mutex_lock(&recovery_lock);
#if IS_ENABLED(CONFIG_MALI_MTK_LOG_BUFFER)
		if (fence_wait->time_in_microseconds == 4000) {
			mtk_logbuffer_type_print(kctx->kbdev, MTK_LOGBUFFER_TYPE_DEFERRED,
						"ctx:%d_%d cpu queue:%llx Internal fence timeouts(%llu ms)! Trigger cross queue sync recovery\n",
						kctx->tgid, kctx->id, fence_wait->queue, fence_wait->time_in_microseconds);
		} else {
			dev_info(kctx->kbdev->dev,
				"ctx:%d_%d cpu queue:%llx Internal fence timeouts(%llu ms)! Trigger cross queue sync recovery",
				kctx->tgid, kctx->id, fence_wait->queue, fence_wait->time_in_microseconds);
		}

		mtk_logbuffer_type_print(kctx->kbdev, MTK_LOGBUFFER_TYPE_CRITICAL | MTK_LOGBUFFER_TYPE_EXCEPTION,
			"ctx:%d_%d cpu queue:%llx Internal fence timeouts(%llu ms)! Trigger cross queue sync recovery\n",
			kctx->tgid, kctx->id, fence_wait->queue, fence_wait->time_in_microseconds);
#else
		dev_info(kctx->kbdev->dev,
			"ctx:%d_%d cpu queue:%llx Internal fence timeouts(%llu ms)! Trigger cross queue sync recovery",
			kctx->tgid, kctx->id, fence_wait->queue, fence_wait->time_in_microseconds);
#endif
		mtk_qinspect_recovery(kctx, QINSPECT_CPU_QUEUE, &fence_wait->queue);
		mutex_unlock(&recovery_lock);
	}
#endif
	return 0;
}
#endif

static long kbase_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct kbase_file *const kfile = filp->private_data;
	struct kbase_context *kctx = NULL;
	struct kbase_device *kbdev = kfile->kbdev;
	void __user *uarg = (void __user *)arg;

	switch (cmd) {
	case KBASE_IOCTL_VERSION_CHECK:
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_VERSION_CHECK, kbase_api_handshake,
					 struct kbase_ioctl_version_check, kfile);
		break;

	case KBASE_IOCTL_VERSION_CHECK_RESERVED:
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_VERSION_CHECK_RESERVED,
					 kbase_api_handshake_dummy,
					 struct kbase_ioctl_version_check, kfile);
		break;

	case KBASE_IOCTL_SET_FLAGS:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_SET_FLAGS, kbase_api_set_flags,
				      struct kbase_ioctl_set_flags, kfile);
		break;

	case KBASE_IOCTL_KINSTR_PRFCNT_ENUM_INFO:
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_KINSTR_PRFCNT_ENUM_INFO,
					 kbase_api_kinstr_prfcnt_enum_info,
					 struct kbase_ioctl_kinstr_prfcnt_enum_info, kfile);
		break;

	case KBASE_IOCTL_KINSTR_PRFCNT_SETUP:
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_KINSTR_PRFCNT_SETUP,
					 kbase_api_kinstr_prfcnt_setup,
					 union kbase_ioctl_kinstr_prfcnt_setup, kfile);
		break;
	case KBASE_IOCTL_GET_GPUPROPS:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_GET_GPUPROPS, kbase_api_get_gpuprops,
				      struct kbase_ioctl_get_gpuprops, kfile);
		break;
	}

	kctx = kbase_file_get_kctx_if_setup_complete(kfile);
	if (unlikely(!kctx))
		return -EPERM;

	switch (cmd) {
#if !MALI_USE_CSF
	case KBASE_IOCTL_JOB_SUBMIT:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_JOB_SUBMIT, kbase_api_job_submit,
				      struct kbase_ioctl_job_submit, kctx);
		break;
#endif
#if !MALI_USE_CSF
	case KBASE_IOCTL_POST_TERM:
		KBASE_HANDLE_IOCTL(KBASE_IOCTL_POST_TERM, kbase_api_post_term, kctx);
		break;
#endif
	case KBASE_IOCTL_MEM_ALLOC:
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_MEM_ALLOC, kbase_api_mem_alloc,
					 union kbase_ioctl_mem_alloc, kctx);
		break;
#if MALI_USE_CSF
	case KBASE_IOCTL_MEM_ALLOC_EX:
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_MEM_ALLOC_EX, kbase_api_mem_alloc_ex,
					 union kbase_ioctl_mem_alloc_ex, kctx);
		break;
#endif
	case KBASE_IOCTL_MEM_QUERY:
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_MEM_QUERY, kbase_api_mem_query,
					 union kbase_ioctl_mem_query, kctx);
		break;
	case KBASE_IOCTL_MEM_FREE:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_MEM_FREE, kbase_api_mem_free,
				      struct kbase_ioctl_mem_free, kctx);
		break;
	case KBASE_IOCTL_DISJOINT_QUERY:
		KBASE_HANDLE_IOCTL_OUT(KBASE_IOCTL_DISJOINT_QUERY, kbase_api_disjoint_query,
				       struct kbase_ioctl_disjoint_query, kctx);
		break;
	case KBASE_IOCTL_GET_DDK_VERSION:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_GET_DDK_VERSION, kbase_api_get_ddk_version,
				      struct kbase_ioctl_get_ddk_version, kctx);
		break;
	case KBASE_IOCTL_MEM_JIT_INIT:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_MEM_JIT_INIT, kbase_api_mem_jit_init,
				      struct kbase_ioctl_mem_jit_init, kctx);
		break;
	case KBASE_IOCTL_MEM_EXEC_INIT:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_MEM_EXEC_INIT, kbase_api_mem_exec_init,
				      struct kbase_ioctl_mem_exec_init, kctx);
		break;
	case KBASE_IOCTL_MEM_SYNC:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_MEM_SYNC, kbase_api_mem_sync,
				      struct kbase_ioctl_mem_sync, kctx);
		break;
	case KBASE_IOCTL_MEM_FIND_CPU_OFFSET:
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_MEM_FIND_CPU_OFFSET,
					 kbase_api_mem_find_cpu_offset,
					 union kbase_ioctl_mem_find_cpu_offset, kctx);
		break;
	case KBASE_IOCTL_MEM_FIND_GPU_START_AND_OFFSET:
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_MEM_FIND_GPU_START_AND_OFFSET,
					 kbase_api_mem_find_gpu_start_and_offset,
					 union kbase_ioctl_mem_find_gpu_start_and_offset, kctx);
		break;
	case KBASE_IOCTL_GET_CONTEXT_ID:
		KBASE_HANDLE_IOCTL_OUT(KBASE_IOCTL_GET_CONTEXT_ID, kbase_api_get_context_id,
				       struct kbase_ioctl_get_context_id, kctx);
		break;
	case KBASE_IOCTL_TLSTREAM_ACQUIRE:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_TLSTREAM_ACQUIRE, kbase_api_tlstream_acquire,
				      struct kbase_ioctl_tlstream_acquire, kctx);
		break;
	case KBASE_IOCTL_TLSTREAM_FLUSH:
		KBASE_HANDLE_IOCTL(KBASE_IOCTL_TLSTREAM_FLUSH, kbase_api_tlstream_flush, kctx);
		break;
	case KBASE_IOCTL_MEM_COMMIT:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_MEM_COMMIT, kbase_api_mem_commit,
				      struct kbase_ioctl_mem_commit, kctx);
		break;
	case KBASE_IOCTL_MEM_ALIAS:
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_MEM_ALIAS, kbase_api_mem_alias,
					 union kbase_ioctl_mem_alias, kctx);
		break;
	case KBASE_IOCTL_MEM_IMPORT:
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_MEM_IMPORT, kbase_api_mem_import,
					 union kbase_ioctl_mem_import, kctx);
		break;
	case KBASE_IOCTL_MEM_FLAGS_CHANGE:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_MEM_FLAGS_CHANGE, kbase_api_mem_flags_change,
				      struct kbase_ioctl_mem_flags_change, kctx);
		break;
	case KBASE_IOCTL_STREAM_CREATE:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_STREAM_CREATE, kbase_api_stream_create,
				      struct kbase_ioctl_stream_create, kctx);
		break;
	case KBASE_IOCTL_FENCE_VALIDATE:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_FENCE_VALIDATE, kbase_api_fence_validate,
				      struct kbase_ioctl_fence_validate, kctx);
		break;
	case KBASE_IOCTL_MEM_PROFILE_ADD:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_MEM_PROFILE_ADD, kbase_api_mem_profile_add,
				      struct kbase_ioctl_mem_profile_add, kctx);
		break;

#if !MALI_USE_CSF
	case KBASE_IOCTL_SOFT_EVENT_UPDATE:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_SOFT_EVENT_UPDATE, kbase_api_soft_event_update,
				      struct kbase_ioctl_soft_event_update, kctx);
		break;
#endif

	case KBASE_IOCTL_STICKY_RESOURCE_MAP:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_STICKY_RESOURCE_MAP,
				      kbase_api_sticky_resource_map,
				      struct kbase_ioctl_sticky_resource_map, kctx);
		break;
	case KBASE_IOCTL_STICKY_RESOURCE_UNMAP:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_STICKY_RESOURCE_UNMAP,
				      kbase_api_sticky_resource_unmap,
				      struct kbase_ioctl_sticky_resource_unmap, kctx);
		break;

#if !MALI_USE_CSF
	case KBASE_IOCTL_KINSTR_JM_FD:
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_KINSTR_JM_FD, kbase_api_kinstr_jm_fd,
					 union kbase_kinstr_jm_fd, kctx);
		break;
#endif
	case KBASE_IOCTL_GET_CPU_GPU_TIMEINFO:
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_GET_CPU_GPU_TIMEINFO,
					 kbase_api_get_cpu_gpu_timeinfo,
					 union kbase_ioctl_get_cpu_gpu_timeinfo, kctx);
		break;
#if IS_ENABLED(CONFIG_MALI_NO_MALI)
	case KBASE_IOCTL_HWCNT_SET:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_HWCNT_SET, kbase_api_hwcnt_set,
				      struct kbase_ioctl_hwcnt_values, kctx);
		break;
#endif
#ifdef CONFIG_MALI_CINSTR_GWT
	case KBASE_IOCTL_CINSTR_GWT_START:
		KBASE_HANDLE_IOCTL(KBASE_IOCTL_CINSTR_GWT_START, kbase_gpu_gwt_start, kctx);
		break;
	case KBASE_IOCTL_CINSTR_GWT_STOP:
		KBASE_HANDLE_IOCTL(KBASE_IOCTL_CINSTR_GWT_STOP, kbase_gpu_gwt_stop, kctx);
		break;
	case KBASE_IOCTL_CINSTR_GWT_DUMP:
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_CINSTR_GWT_DUMP, kbase_gpu_gwt_dump,
					 union kbase_ioctl_cinstr_gwt_dump, kctx);
		break;
#endif
#if MALI_USE_CSF
	case KBASE_IOCTL_CS_EVENT_SIGNAL:
		KBASE_HANDLE_IOCTL(KBASE_IOCTL_CS_EVENT_SIGNAL, kbasep_cs_event_signal, kctx);
		break;
	case KBASE_IOCTL_CS_QUEUE_REGISTER:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_CS_QUEUE_REGISTER, kbasep_cs_queue_register,
				      struct kbase_ioctl_cs_queue_register, kctx);
		break;
	case KBASE_IOCTL_CS_QUEUE_REGISTER_EX:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_CS_QUEUE_REGISTER_EX, kbasep_cs_queue_register_ex,
				      struct kbase_ioctl_cs_queue_register_ex, kctx);
		break;
	case KBASE_IOCTL_CS_QUEUE_TERMINATE:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_CS_QUEUE_TERMINATE, kbasep_cs_queue_terminate,
				      struct kbase_ioctl_cs_queue_terminate, kctx);
		break;
	case KBASE_IOCTL_CS_QUEUE_BIND:
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_CS_QUEUE_BIND, kbasep_cs_queue_bind,
					 union kbase_ioctl_cs_queue_bind, kctx);
		break;
	case KBASE_IOCTL_CS_QUEUE_KICK:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_CS_QUEUE_KICK, kbasep_cs_queue_kick,
				      struct kbase_ioctl_cs_queue_kick, kctx);
		break;
	case KBASE_IOCTL_CS_QUEUE_GROUP_CREATE_1_6:
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_CS_QUEUE_GROUP_CREATE_1_6,
					 kbasep_cs_queue_group_create_1_6,
					 union kbase_ioctl_cs_queue_group_create_1_6, kctx);
		break;
	case KBASE_IOCTL_CS_QUEUE_GROUP_CREATE_1_18:
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_CS_QUEUE_GROUP_CREATE_1_18,
					 kbasep_cs_queue_group_create_1_18,
					 union kbase_ioctl_cs_queue_group_create_1_18, kctx);
		break;
	case KBASE_IOCTL_CS_QUEUE_GROUP_CREATE:
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_CS_QUEUE_GROUP_CREATE,
					 kbasep_cs_queue_group_create,
					 union kbase_ioctl_cs_queue_group_create, kctx);
		break;
	case KBASE_IOCTL_CS_QUEUE_GROUP_TERMINATE:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_CS_QUEUE_GROUP_TERMINATE,
				      kbasep_cs_queue_group_terminate,
				      struct kbase_ioctl_cs_queue_group_term, kctx);
		break;
	case KBASE_IOCTL_KCPU_QUEUE_CREATE:
		KBASE_HANDLE_IOCTL_OUT(KBASE_IOCTL_KCPU_QUEUE_CREATE, kbasep_kcpu_queue_new,
				       struct kbase_ioctl_kcpu_queue_new, kctx);
		break;
	case KBASE_IOCTL_KCPU_QUEUE_DELETE:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_KCPU_QUEUE_DELETE, kbasep_kcpu_queue_delete,
				      struct kbase_ioctl_kcpu_queue_delete, kctx);
		break;
	case KBASE_IOCTL_KCPU_QUEUE_ENQUEUE:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_KCPU_QUEUE_ENQUEUE, kbasep_kcpu_queue_enqueue,
				      struct kbase_ioctl_kcpu_queue_enqueue, kctx);
		break;
	case KBASE_IOCTL_QUEUE_GROUP_CLEAR_FAULTS:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_QUEUE_GROUP_CLEAR_FAULTS,
				      kbasep_queue_group_clear_faults,
				      struct kbase_ioctl_queue_group_clear_faults, kctx);
		break;
	case KBASE_IOCTL_CS_TILER_HEAP_INIT:
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_CS_TILER_HEAP_INIT, kbasep_cs_tiler_heap_init,
					 union kbase_ioctl_cs_tiler_heap_init, kctx);
		break;
	case KBASE_IOCTL_CS_TILER_HEAP_INIT_1_13:
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_CS_TILER_HEAP_INIT_1_13,
					 kbasep_cs_tiler_heap_init_1_13,
					 union kbase_ioctl_cs_tiler_heap_init_1_13, kctx);
		break;
	case KBASE_IOCTL_CS_TILER_HEAP_TERM:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_CS_TILER_HEAP_TERM, kbasep_cs_tiler_heap_term,
				      struct kbase_ioctl_cs_tiler_heap_term, kctx);
		break;
	case KBASE_IOCTL_CS_GET_GLB_IFACE:
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_CS_GET_GLB_IFACE, kbase_ioctl_cs_get_glb_iface,
					 union kbase_ioctl_cs_get_glb_iface, kctx);
		break;
	case KBASE_IOCTL_CS_CPU_QUEUE_DUMP:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_CS_CPU_QUEUE_DUMP, kbasep_ioctl_cs_cpu_queue_dump,
				      struct kbase_ioctl_cs_cpu_queue_info, kctx);
		break;

	case KBASE_IOCTL_READ_USER_PAGE:
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_READ_USER_PAGE, kbase_ioctl_read_user_page,
					 union kbase_ioctl_read_user_page, kctx);
		break;
#endif
#if MALI_UNIT_TEST
	case KBASE_IOCTL_TLSTREAM_STATS:
		KBASE_HANDLE_IOCTL_OUT(KBASE_IOCTL_TLSTREAM_STATS, kbase_api_tlstream_stats,
				       struct kbase_ioctl_tlstream_stats, kctx);
		break;
#endif
	case KBASE_IOCTL_CONTEXT_PRIORITY_CHECK:
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_CONTEXT_PRIORITY_CHECK,
					 kbasep_ioctl_context_priority_check,
					 struct kbase_ioctl_context_priority_check, kctx);
		break;
	case KBASE_IOCTL_SET_LIMITED_CORE_COUNT:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_SET_LIMITED_CORE_COUNT,
				      kbasep_ioctl_set_limited_core_count,
				      struct kbase_ioctl_set_limited_core_count, kctx);
		break;
#if IS_ENABLED(CONFIG_MALI_MTK_FENCE_DEBUG)
	case KBASE_IOCTL_INTERNAL_FENCE_WAIT:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_INTERNAL_FENCE_WAIT,
					kbasep_ioctl_internal_fence_wait,
					struct kbase_ioctl_internal_fence_wait, kctx);
		break;
#endif
	}

	dev_warn(kbdev->dev, "Unknown ioctl 0x%x nr:%d", cmd, _IOC_NR(cmd));

	return -ENOIOCTLCMD;
}

#if MALI_USE_CSF
static ssize_t kbase_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	struct kbase_file *const kfile = filp->private_data;
	struct kbase_context *const kctx = kbase_file_get_kctx_if_setup_complete(kfile);
	struct base_csf_notification event_data = { .type = BASE_CSF_NOTIFICATION_EVENT };
	const size_t data_size = sizeof(event_data);
	bool read_event = false, read_error = false;

	CSTD_UNUSED(f_pos);

	if (unlikely(!kctx))
		return -EPERM;

	if (count < data_size)
		return -ENOBUFS;

	if (atomic_read(&kctx->event_count))
		read_event = true;
	else
		read_error = kbase_csf_event_read_error(kctx, &event_data);

	if (!read_event && !read_error) {
		bool dump = kbase_csf_cpu_queue_read_dump_req(kctx, &event_data);

		if (!dump)
			dev_dbg(kctx->kbdev->dev, "Neither event nor error signaled");
	}

	if (copy_to_user(buf, &event_data, data_size) != 0) {
		dev_warn(kctx->kbdev->dev, "Failed to copy data\n");
		return -EFAULT;
	}

	if (read_event)
		atomic_set(&kctx->event_count, 0);

#if IS_ENABLED(CONFIG_MALI_MTK_FENCE_DEBUG)
	kctx->notification_data_read_time = ktime_get_ns();
#endif

	return data_size;
}
#else
static ssize_t kbase_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	struct kbase_file *const kfile = filp->private_data;
	struct kbase_context *const kctx = kbase_file_get_kctx_if_setup_complete(kfile);
	struct base_jd_event_v2 uevent;
	int out_count = 0;

	CSTD_UNUSED(f_pos);

	if (unlikely(!kctx))
		return -EPERM;

	if (count < sizeof(uevent))
		return -ENOBUFS;

	memset(&uevent, 0, sizeof(uevent));

	do {
		while (kbase_event_dequeue(kctx, &uevent)) {
			if (out_count > 0)
				goto out;

			if (filp->f_flags & O_NONBLOCK)
				return -EAGAIN;

			if (wait_event_interruptible(kctx->event_queue,
						     kbase_event_pending(kctx)) != 0)
				return -ERESTARTSYS;
		}
		if (uevent.event_code == BASE_JD_EVENT_DRV_TERMINATED) {
			if (out_count == 0)
				return -EPIPE;
			goto out;
		}

		if (copy_to_user(buf, &uevent, sizeof(uevent)) != 0)
			return -EFAULT;

		buf += sizeof(uevent);
		out_count++;
		count -= sizeof(uevent);
	} while (count >= sizeof(uevent));

out:
	return out_count * sizeof(uevent);
}
#endif

static __poll_t kbase_poll(struct file *filp, poll_table *wait)
{
	struct kbase_file *const kfile = filp->private_data;
	struct kbase_context *const kctx = kbase_file_get_kctx_if_setup_complete(kfile);

	if (unlikely(!kctx)) {
#if (KERNEL_VERSION(4, 19, 0) > LINUX_VERSION_CODE)
		return POLLERR;
#else
		return EPOLLERR;
#endif
	}
#if IS_ENABLED(CONFIG_MALI_MTK_FENCE_DEBUG)
	kctx->notification_fd_signal_time = 0;
	kctx->notification_data_read_time = 0;
	kctx->notification_polling_start_time = ktime_get_ns();
#endif

	poll_wait(filp, &kctx->event_queue, wait);
	if (kbase_event_pending(kctx)) {
#if IS_ENABLED(CONFIG_MALI_MTK_FENCE_DEBUG)
		kctx->notification_fd_signal_time = ktime_get_ns();
#endif
#if (KERNEL_VERSION(4, 19, 0) > LINUX_VERSION_CODE)
		return POLLIN | POLLRDNORM;
#else
		return EPOLLIN | EPOLLRDNORM;
#endif
	}

	return 0;
}

void kbase_event_wakeup(struct kbase_context *kctx)
{
	KBASE_DEBUG_ASSERT(kctx);
	dev_dbg(kctx->kbdev->dev, "Waking event queue for context %pK\n", (void *)kctx);
	wake_up_interruptible(&kctx->event_queue);
}

KBASE_EXPORT_TEST_API(kbase_event_wakeup);

#if MALI_USE_CSF
int kbase_event_pending(struct kbase_context *kctx)
{
	KBASE_DEBUG_ASSERT(kctx);

	if (unlikely(!kctx))
		return -EPERM;

	return (atomic_read(&kctx->event_count) != 0) || kbase_csf_event_error_pending(kctx) ||
	       kbase_csf_cpu_queue_dump_needed(kctx);
}
#else
int kbase_event_pending(struct kbase_context *kctx)
{
	KBASE_DEBUG_ASSERT(kctx);

	if (unlikely(!kctx))
		return -EPERM;

	return (atomic_read(&kctx->event_count) != 0) || (atomic_read(&kctx->event_closed) != 0);
}
#endif

KBASE_EXPORT_TEST_API(kbase_event_pending);

static int kbase_mmap(struct file *const filp, struct vm_area_struct *const vma)
{
	struct kbase_file *const kfile = filp->private_data;
	struct kbase_context *const kctx = kbase_file_get_kctx_if_setup_complete(kfile);

	if (unlikely(!kctx))
		return -EPERM;

	return kbase_context_mmap(kctx, vma);
}

static int kbase_check_flags(int flags)
{

	if (0 == (flags & O_CLOEXEC))
		return -EINVAL;

	return 0;
}

static unsigned long kbase_get_unmapped_area(struct file *const filp, const unsigned long addr,
					     const unsigned long len, const unsigned long pgoff,
					     const unsigned long flags)
{
	struct kbase_file *const kfile = filp->private_data;
	struct kbase_context *const kctx = kbase_file_get_kctx_if_setup_complete(kfile);

	if (unlikely(!kctx))
		return -EPERM;

	return kbase_context_get_unmapped_area(kctx, addr, len, pgoff, flags);
}

static const struct file_operations kbase_fops = {
	.owner = THIS_MODULE,
	.open = kbase_open,
	.release = kbase_release,
	.read = kbase_read,
	.poll = kbase_poll,
	.unlocked_ioctl = kbase_ioctl,
	.compat_ioctl = kbase_ioctl,
	.mmap = kbase_mmap,
	.check_flags = kbase_check_flags,
	.get_unmapped_area = kbase_get_unmapped_area,
#if (KERNEL_VERSION(6, 12, 0) <= LINUX_VERSION_CODE)
	.fop_flags = FOP_UNSIGNED_OFFSET,
#endif
};

#if IS_ENABLED(CONFIG_MALI_MIDGARD_DVFS) && \
	IS_ENABLED(CONFIG_MALI_MTK_DVFS_POLICY) && \
	IS_ENABLED(CONFIG_MALI_MTK_GPU_DVFS_HINT_26M_LOADING)
static ssize_t dvfs_hint_26m_perf_cnting_show(struct device *dev, struct device_attribute *attr, char *const buf)
{
	struct kbase_device *kbdev;
	ssize_t ret = 0;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	if (dvfs_hint_26m_perf_cnting_enable)
		ret += scnprintf(buf + ret, PAGE_SIZE - ret, "dvfs_hint_26m_perf_cnting is enabled\n");
	else
		ret += scnprintf(buf + ret, PAGE_SIZE - ret, "dvfs_hint_26m_perf_cnting is disabled\n");

	return ret;
}
static ssize_t dvfs_hint_26m_perf_cnting_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	int ret;
	int dvfs_hint_26m_perf_cnting = 0;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	ret = kstrtoint(buf, 0, &dvfs_hint_26m_perf_cnting);

	dvfs_hint_26m_perf_cnting_enable = dvfs_hint_26m_perf_cnting;

	return count;
}
static DEVICE_ATTR_RW(dvfs_hint_26m_perf_cnting);

static ssize_t ipa_enable_show(struct device *dev, struct device_attribute *attr, char *const buf)
{
	struct kbase_device *kbdev;
	ssize_t ret = 0;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	if (Enable_IPA)
		ret += scnprintf(buf + ret, PAGE_SIZE - ret, "ipa is enabled\n");
	else
		ret += scnprintf(buf + ret, PAGE_SIZE - ret, "ipa is disabled\n");

	return ret;
}
static ssize_t ipa_enable_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	int ret;
	int ipa_enable = 0;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	ret = kstrtoint(buf, 0, &ipa_enable);

	Enable_IPA = ipa_enable;

	return count;
}
static DEVICE_ATTR_RW(ipa_enable);

#endif

static ssize_t power_policy_show(struct device *dev, struct device_attribute *attr, char *const buf)
{
	struct kbase_device *kbdev;
	const struct kbase_pm_policy *current_policy;
	const struct kbase_pm_policy *const *policy_list;
	uint policy_count;
	uint i;
	ssize_t ret = 0;

	CSTD_UNUSED(attr);

	kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

	current_policy = kbase_pm_get_policy(kbdev);

	policy_count = (uint)kbase_pm_list_policies(kbdev, &policy_list);

	for (i = 0; i < policy_count && ret < (ssize_t)PAGE_SIZE; i++) {
		if (policy_list[i] == current_policy)
			ret += scnprintf(buf + ret, (size_t)(PAGE_SIZE - ret), "[%s] ",
					 policy_list[i]->name);
		else
			ret += scnprintf(buf + ret, (size_t)(PAGE_SIZE - ret), "%s ",
					 policy_list[i]->name);
	}

	if (ret < (ssize_t)PAGE_SIZE - 1) {
		ret += scnprintf(buf + ret, (size_t)(PAGE_SIZE - ret), "\n");
	} else {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

	return ret;
}

static ssize_t power_policy_store(struct device *dev, struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	const struct kbase_pm_policy *new_policy = NULL;
	const struct kbase_pm_policy *const *policy_list;
	uint policy_count;
	uint i;

	CSTD_UNUSED(attr);

	kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

	policy_count = (uint)kbase_pm_list_policies(kbdev, &policy_list);

	for (i = 0; i < policy_count; i++) {
		if (sysfs_streq(policy_list[i]->name, buf)) {
			new_policy = policy_list[i];
			break;
		}
	}

	if (!new_policy) {
		dev_err(dev, "power_policy: policy not found\n");
		return -EINVAL;
	}

	kbase_pm_set_policy(kbdev, new_policy);

	return (ssize_t)count;
}

static DEVICE_ATTR_RW(power_policy);

#if IS_ENABLED(CONFIG_MALI_MTK_ACP_FORCE_SYNC_DEBUG)
static ssize_t force_cache_sync_show(struct device *dev, struct device_attribute *attr, char * const buf)
{
	struct kbase_device *kbdev;

	CSTD_UNUSED(attr);

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	return scnprintf(buf, PAGE_SIZE, "System cohrency: %u, Force cache: %d\n", kbdev->system_coherency, kbdev->acp_dbg_force_sync);
}

static ssize_t force_cache_sync_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	unsigned int force_sync;

	kbdev = to_kbase_device(dev);
	if (!kbdev) {
		pr_info("[KBASE] Bad kbdev!\n");
		return -ENODEV;
	}

	if ((kstrtoint(buf, 0, &force_sync)) || kbdev->acp_dbg_force_sync == FORCE_SYNC_DTS ||
		(force_sync != FORCE_SYNC_NONE && force_sync != FORCE_SYNC_CMD))
		return -EINVAL;

	kbdev->acp_dbg_force_sync = FORCE_SYNC_CMD;

	return count;
}
static DEVICE_ATTR_RW(force_cache_sync);
#endif

#if IS_ENABLED(CONFIG_MALI_MTK_KBASE_MMU_DBG_LOG)

static ssize_t mmu_dbg_config_show(struct device *dev, struct device_attribute *attr, char * const buf)
{
	struct kbase_device *kbdev = dev_get_drvdata(dev);
	u32 mmu_dbg_config_value;

	if (!kbdev) {
		pr_info("[KBASE] Bad kbdev!\n");
		return -ENODEV;
	}

	mmu_dbg_config_value = kbdev->mmu_dbg_config_value;
	return scnprintf(buf, PAGE_SIZE, "%u\n", mmu_dbg_config_value);
}

static ssize_t mmu_dbg_config_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev = dev_get_drvdata(dev);
	u32 mmu_dbg_config_value;

	if (!kbdev) {
		pr_info("[KBASE] Bad kbdev!\n");
		return -ENODEV;
	}

	if (kstrtouint(buf, 0, &mmu_dbg_config_value))
		return -EINVAL;

	kbdev->mmu_dbg_config_value = mmu_dbg_config_value;
	pr_info("[KBASE] mmu_dbg_config_value=%d\n", mmu_dbg_config_value);

	return count;
}

static DEVICE_ATTR_RW(mmu_dbg_config);
#endif

#if IS_ENABLED(CONFIG_MALI_MTK_PAGE_FAULT_WB_TILER_RECLAIM)
static ssize_t force_reclaim_show(struct device *dev, struct device_attribute *attr, char * const buf)
{
	return 0;
}

static ssize_t force_reclaim_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;

	kbdev = to_kbase_device(dev);
	if (!kbdev) {
		pr_info("[KBASE] Bad kbdev!\n");
		return -ENODEV;
	}
	mtk_force_reclaim(kbdev);
	return count;
}

static DEVICE_ATTR_RW(force_reclaim);
#endif

#if IS_ENABLED(CONFIG_MALI_MTK_MMU_DUMP)
extern void *kbase_mmu_dump_mtk(struct kbase_device *kbdev, struct kbase_context *kctx, size_t nr_pages, size_t *ret_size);
#define DUMP_LIST_D1_NUM   (KBASE_MMU_PAGE_ENTRIES + 1)
#define DUMP_LIST_STR_SIZE (DUMP_LIST_D1_NUM*17)

static void dump_mmu_table_to_km_log(struct kbase_device *kbdev, u64 *table, unsigned long dump_entry_nr)
{
	size_t entry_list_str_i = 0;
	unsigned long i = 0;
	char* entry_list_str = (char *) kmalloc(DUMP_LIST_STR_SIZE+1, GFP_KERNEL);
	entry_list_str[DUMP_LIST_STR_SIZE] = 0;

	dev_info(kbdev->dev, "[GPUMMU] as_setup: transtab 0x%llx, memattr 0x%llx, transcfg 0x%llx,\n", table[0], table[1], table[2]);
	dev_info(kbdev->dev, "[GPUMMU] show i = %lu, range (3, %lu)\n", i, dump_entry_nr);
	for(i = 3; i < dump_entry_nr; i++) {
		if ( (i-3) % DUMP_LIST_D1_NUM == 0 ) {
			dev_info(kbdev->dev, "[GPUMMU] [%lu] %llx \n", i, table[i]);

		}

		if(0xFFULL == table[i])
			break;
		entry_list_str_i += scnprintf(entry_list_str + entry_list_str_i, DUMP_LIST_D1_NUM*16 - entry_list_str_i, "%llx ", table[i]);
		if ( ((i-3) % DUMP_LIST_D1_NUM) == (DUMP_LIST_D1_NUM - 1) ) {

			entry_list_str_i += scnprintf(entry_list_str + entry_list_str_i, DUMP_LIST_D1_NUM*16 - entry_list_str_i, " \n");
			entry_list_str[entry_list_str_i] = 0;
			dev_info(kbdev->dev, "[GPUMMU] %s,\n", entry_list_str);
			entry_list_str_i = 0;
		}
	}
	kfree(entry_list_str);
}

static size_t dump_mmu_table_to_str_buf(struct kbase_device *kbdev, u64 *table, unsigned long dump_entry_nr, char* buf_str, size_t buf_size)
{
	size_t count = 0;
	unsigned long i = 0;

	count += scnprintf(buf_str + count, buf_size - count,
					"[GPUMMU] as_setup: transtab 0x%llx, memattr 0x%llx, transcfg 0x%llx,\n", table[0], table[1], table[2]);
	count += scnprintf(buf_str + count, buf_size - count, "[GPUMMU] Table:\n");
	for(i = 3; i < dump_entry_nr; i++) {
		if(0xFFULL == table[i])
			break;
		count += scnprintf(buf_str + count, buf_size - count, "%llx ", table[i]);
		if ( ((i-3) % DUMP_LIST_D1_NUM) == (DUMP_LIST_D1_NUM - 1) ) {
			count += scnprintf(buf_str + count, buf_size - count, " \n");
		}
	}
	return count;
}

#define DUMP_MMU_TABLE_MAX_NUM 256
struct mmu_user_info {
	void *mmu_table_addr;
	size_t table_size;
	unsigned int as_no;
	int as_nr;
	u32 id;
	pid_t tgid;
	pid_t pid;
	char group_leader_comm[TASK_COMM_LEN];
	char comm[TASK_COMM_LEN];
};

static char* mmu_table_user_arr_in_str_buf = NULL;
static struct mutex mmu_table_str_buf_mutex;
static ssize_t mmu_table_str_buf_size = 0;

static atomic_t dump_mmu_mode;
static struct mutex dump_mmu_mode_mutex;
#define DUMP_MMU_MODE_INIT   1
#define DUMP_MMU_MODE_LOCK   2
#define DUMP_MMU_MODE_UNLOCK 3
#define DUMP_MMU_MODE_FINISH 0
#define DUMP_MMU_MODE_KM_LOG 4

static void save_mmu_table_user_info(struct kbase_device *kbdev,
									struct mmu_user_info * dst,
									void *mmu_table_addr,
									size_t table_size,
									unsigned int as_no,
									int as_nr,
									u32 id,
									pid_t tgid,
									pid_t pid,
									char *group_leader_comm,
									char *comm)
{
	u64 *table = NULL;
	dev_info(kbdev->dev,
		"[GPUMMU] Dump MMU table, as_no %u, as_nr %d, dump address %p, ctx_id %d_%d, pid %d, group_leader %s, comm %s, table_size %lu \n",
		as_no, as_nr, mmu_table_addr,
		tgid, id, pid,
		group_leader_comm, comm, table_size);
	dst->mmu_table_addr = mmu_table_addr;
	dst->table_size = table_size;
	dst->as_no = as_no;
	dst->as_nr = as_nr;
	dst->id = id;
	dst->tgid = tgid;
	dst->pid = pid;
	memcpy(dst->group_leader_comm, group_leader_comm, TASK_COMM_LEN);
	memcpy(dst->comm, comm, TASK_COMM_LEN);

	if(mmu_table_addr) {
		table = (u64 *) mmu_table_addr;
		dev_info(kbdev->dev, "[GPUMMU] as_setup: transtab 0x%llx, memattr 0x%llx, transcfg 0x%llx,\n", table[0], table[1], table[2]);
	}
}

static void free_all_dump_mmu_table(struct kbase_device *kbdev)
{
	unsigned int i = 0;

	if(mmu_table_str_buf_size == 0)
		return;

	mutex_lock(&mmu_table_str_buf_mutex);
	mmu_table_str_buf_size = 0;
	if(mmu_table_user_arr_in_str_buf)
		kfree(mmu_table_user_arr_in_str_buf);
	mmu_table_user_arr_in_str_buf = NULL;
	mutex_unlock(&mmu_table_str_buf_mutex);
}

static void dump_mmu_table_all_to_str_buf(struct kbase_device *kbdev,
										  struct mmu_user_info *mmu_table_user_arr, unsigned int mmu_table_num)
{
	unsigned int i = 0;
	size_t out_size = PAGE_SIZE + 1, used_size = 0;
	unsigned long entry_nr = 0;

	if(mmu_table_num == 0)
		return;

	for(i=0; i< mmu_table_num; i++)
		out_size += (mmu_table_user_arr[i].table_size * 17) / sizeof(u64);

	mmu_table_user_arr_in_str_buf = (char *) kmalloc(out_size, GFP_KERNEL);
	char* buf_str = mmu_table_user_arr_in_str_buf;
	if(buf_str == NULL)
		return;

	out_size -= 1;
	buf_str[out_size] = 0;
	dev_info(kbdev->dev, "[GPUMMU] out_size = %lu \n", out_size);

	for(i=0; i< mmu_table_num && used_size <= out_size; i++){
		used_size += (size_t)scnprintf(buf_str + used_size, out_size - used_size,
							"[GPUMMU] Dump MMU table, as_no %u, as_nr %d, dump address %p, ctx_id %d_%d, pid %d, size %lu, group_leader %s, comm %s\n",
							mmu_table_user_arr[i].as_no, mmu_table_user_arr[i].as_nr, mmu_table_user_arr[i].mmu_table_addr,
							mmu_table_user_arr[i].tgid,  mmu_table_user_arr[i].id,  mmu_table_user_arr[i].pid,
							mmu_table_user_arr[i].table_size, mmu_table_user_arr[i].group_leader_comm,  mmu_table_user_arr[i].comm);
		entry_nr = mmu_table_user_arr[i].table_size / sizeof(u64);
		used_size += dump_mmu_table_to_str_buf(kbdev, (u64 *)mmu_table_user_arr[i].mmu_table_addr, entry_nr, buf_str + used_size, out_size - used_size);
		used_size += (size_t)scnprintf(buf_str + used_size, out_size - used_size, "\n[GPUMMU] -----------------------\n");
	}
	buf_str[used_size] = '\0';
	used_size++;
	dev_info(kbdev->dev, "[GPUMMU] used_size = %lu \n", used_size);
	if(used_size == out_size){
		dev_err(kbdev->dev, "[GPUMMU] warning (used_size = out_size) \n");
	}
	mmu_table_str_buf_size = used_size;
}

static void dump_mmu_table_all(struct kbase_device *kbdev)
{
	struct kbase_context *kctx;
	size_t nr_pages = 0, copy_size = 0;
	unsigned int i = 0;
	struct mmu_user_info *mmu_table_user_arr = NULL;
	unsigned int mmu_table_num = 0;

	if (!kbdev) {
		pr_info("[KBASE] Bad kbdev!\n");
		return;
	}

	mutex_lock(&mmu_table_str_buf_mutex);
	mutex_lock(&kbdev->kctx_list_lock);
	size_t kctx_num = list_count_nodes(&kbdev->kctx_list);
#if MALI_USE_CSF
	mmu_table_num = kctx_num + 1;
#else
	mmu_table_num = kctx_num;
#endif
	mmu_table_user_arr = (struct mmu_user_info *) kmalloc(sizeof(struct mmu_user_info)*mmu_table_num, GFP_KERNEL);
	if (!mmu_table_user_arr) {
		pr_info("[KBASE] Bad alloc of mmu_table_user_arr!\n");
		return;
	}
	list_for_each_entry(kctx, &kbdev->kctx_list, kctx_list_link) {
		if(kctx != NULL) {

			nr_pages = 2;
			nr_pages += kbasep_mmu_dump_table_size(kbdev, MIDGARD_MMU_TOPLEVEL, &kctx->mmu) >> PAGE_SHIFT;

			void *kaddr = (void *) kbase_mmu_dump_mtk(kbdev, kctx, nr_pages, &copy_size);

			unsigned int as_no = (kctx->as_nr != KBASEP_AS_NR_INVALID) ? kbdev->as[kctx->as_nr].number : 0xFF;
			dev_info(kbdev->dev, "[GPUMMU] i = %u ", i);
#if IS_ENABLED(CONFIG_MALI_MTK_UNHANDLED_PAGE_FAULT_DEBUG)
			save_mmu_table_user_info(kbdev, mmu_table_user_arr + i, kaddr, copy_size, as_no, kctx->as_nr,
								kctx->id, kctx->tgid, kctx->pid, kctx->group_leader_comm, kctx->comm);
#else
			save_mmu_table_user_info(kbdev, mmu_table_user_arr + i, kaddr, copy_size, as_no, kctx->as_nr,
								kctx->id, kctx->tgid, kctx->pid, kctx->comm, kctx->comm);
#endif
			i++;
                }
	}
	mutex_unlock(&kbdev->kctx_list_lock);
#if MALI_USE_CSF
	nr_pages = 2;
	nr_pages += kbasep_mmu_dump_table_size(kbdev, MIDGARD_MMU_TOPLEVEL, &kbdev->csf.mcu_mmu) >> PAGE_SHIFT;

	dev_info(kbdev->dev, "[GPUMMU] i = %u ", mmu_table_num-1);
	void *csf_dump_kaddr = (void *) kbase_mmu_dump_mtk(kbdev, NULL, nr_pages, &copy_size);
	char csf_name[TASK_COMM_LEN] = "CSF (GPU FW)";
	save_mmu_table_user_info(kbdev, mmu_table_user_arr + (mmu_table_num-1), csf_dump_kaddr, copy_size, 0, 0, 0, 0, 0, csf_name, csf_name);
#endif

	dump_mmu_table_all_to_str_buf(kbdev, mmu_table_user_arr, mmu_table_num);

	for(i=0; i< mmu_table_num; i++){

		if( (mmu_table_user_arr + i)->mmu_table_addr )
			vfree( (mmu_table_user_arr + i)->mmu_table_addr );
	}

	kfree(mmu_table_user_arr);
	mmu_table_user_arr = NULL;
	mmu_table_num = 0;
	mutex_unlock(&mmu_table_str_buf_mutex);
}

static ssize_t force_dump_mmu_read(struct file *file, char __user *buf, size_t len,
						 loff_t *ppos)
{
	mutex_lock(&dump_mmu_mode_mutex);
	if(atomic_read(&dump_mmu_mode) != DUMP_MMU_MODE_LOCK && mmu_table_str_buf_size <= 0) {
		mutex_unlock(&dump_mmu_mode_mutex);
		pr_err("[GPUMMU] User Guide (Please init and lock buffer before reading): \n"
			"[GPUMMU]  Write Option 1: alloc buffer and snapshot (dump csf mmu and all kctx mmu to buffer) \n"
			"[GPUMMU]  Write Option 2: lock buffer \n"
			"[GPUMMU]  Write Option 3: unlock buffer \n"
			"[GPUMMU]  Write Option 0: free buffer \n"
			"[GPUMMU]  Read: dump buffer \n"
			"[GPUMMU]  Example: echo 1 (init) -> echo 2 (lock) -> cat (dump) -> echo 3 (unlock) -> echo 0 (free) \n");
		return 0;
	}
	mutex_unlock(&dump_mmu_mode_mutex);
	return simple_read_from_buffer(buf, len, ppos, mmu_table_user_arr_in_str_buf, mmu_table_str_buf_size);
}

static ssize_t force_dump_mmu_write(struct file *file,
		const char __user *ubuf, size_t count, loff_t *ppos)
{
	unsigned long val = 0;
	int err = 0;

	CSTD_UNUSED(ppos);
	err = kstrtoul_from_user(ubuf, count, 0, &val);
	if (err)
		return err;

	mutex_lock(&dump_mmu_mode_mutex);
	unsigned int pre_mode = atomic_read(&dump_mmu_mode);
	switch(val)
	{
		case DUMP_MMU_MODE_INIT:
		{
			if(pre_mode == DUMP_MMU_MODE_FINISH) {
				atomic_set(&dump_mmu_mode, DUMP_MMU_MODE_INIT);
				pr_info("[GPUMMU] DUMP_MMU_MODE_INIT \n");
				free_all_dump_mmu_table((struct kbase_device *)file->private_data);
				dump_mmu_table_all((struct kbase_device *)file->private_data);
			} else {
				goto fail_print_guide;
			}
			break;
		}
		case DUMP_MMU_MODE_LOCK:
		{
			if(pre_mode == DUMP_MMU_MODE_INIT) {
				atomic_set(&dump_mmu_mode, DUMP_MMU_MODE_LOCK);
				mutex_lock(&mmu_table_str_buf_mutex);
				pr_info("[GPUMMU] DUMP_MMU_MODE_LOCK \n");
			} else {
				goto fail_print_guide;
			}
			break;
		}
		case DUMP_MMU_MODE_UNLOCK:
		{
			if(pre_mode == DUMP_MMU_MODE_LOCK) {
				atomic_set(&dump_mmu_mode, DUMP_MMU_MODE_UNLOCK);
				mutex_unlock(&mmu_table_str_buf_mutex);
				pr_info("[GPUMMU] DUMP_MMU_MODE_UNLOCK \n");
			} else {
				goto fail_print_guide;
			}
			break;
		}
		case DUMP_MMU_MODE_FINISH:
		{
			if(pre_mode == DUMP_MMU_MODE_UNLOCK || pre_mode == DUMP_MMU_MODE_INIT) {
				atomic_set(&dump_mmu_mode, DUMP_MMU_MODE_FINISH);
				pr_info("[GPUMMU] DUMP_MMU_MODE_FINISH \n");
				free_all_dump_mmu_table((struct kbase_device *)file->private_data);
			} else {
				goto fail_print_guide;
			}
			break;
		}
		case DUMP_MMU_MODE_KM_LOG:
		default:
			goto fail_print_guide;
			break;
	}

	pr_info("[GPUMMU] set dump_mmu_mode=%u done\n", atomic_read(&dump_mmu_mode));
	mutex_unlock(&dump_mmu_mode_mutex);

	return count;

fail_print_guide:
	pr_err("[GPUMMU] Write Option is not expected, target %lu, current dump_mmu_mode %u\n", val, atomic_read(&dump_mmu_mode));
	pr_err("[GPUMMU] User Guide : \n"
			"[GPUMMU]  Write Option 1: alloc buffer and snapshot (dump csf mmu and all kctx mmu to buffer) \n"
			"[GPUMMU]  Write Option 2: lock buffer \n"
			"[GPUMMU]  Write Option 3: unlock buffer \n"
			"[GPUMMU]  Write Option 0: free buffer \n"
			"[GPUMMU]  Read: dump buffer \n"
			"[GPUMMU]  Example: echo 1 (init) -> echo 2 (lock) -> cat (dump) -> echo 3 (unlock) -> echo 0 (free) \n");
	return count;
}

static const struct file_operations fops_force_dump_mmu = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = force_dump_mmu_read,
	.write = force_dump_mmu_write,
#if (KERNEL_VERSION(6, 12, 0) > LINUX_VERSION_CODE)
	.llseek = no_llseek,
#endif
};
#endif

static ssize_t core_mask_show(struct device *dev, struct device_attribute *attr, char *const buf)
{
	struct kbase_device *kbdev;
	unsigned long flags;
	ssize_t ret = 0;
#if !MALI_USE_CSF
	size_t i;
#endif

	CSTD_UNUSED(attr);

	kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

#if MALI_USE_CSF
	ret += scnprintf(buf + ret, (size_t)(PAGE_SIZE - ret), "Current debug core mask : 0x%llX\n",
			 kbdev->pm.debug_core_mask);
	ret += scnprintf(buf + ret, (size_t)(PAGE_SIZE - ret),
			 "Current desired core mask : 0x%llX\n", kbase_pm_ca_get_core_mask(kbdev));
	ret += scnprintf(buf + ret, (size_t)(PAGE_SIZE - ret),
			 "Current in use core mask : 0x%llX\n", kbdev->pm.backend.shaders_avail);
#else
	for (i = 0; i < BASE_JM_MAX_NR_SLOTS; i++) {
		if (PAGE_SIZE < ret)
			goto out_unlock;

		ret += scnprintf(buf + ret, (size_t)(PAGE_SIZE - ret),
				 "Current core mask (JS%zu) : 0x%llX\n", i,
				 kbdev->pm.debug_core_mask[i]);
	}
#endif

	ret += scnprintf(buf + ret, (size_t)(PAGE_SIZE - ret), "Available core mask : 0x%llX\n",
			 kbdev->gpu_props.shader_present);
#if !MALI_USE_CSF
out_unlock:
#endif
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	return ret;
}

#if MALI_USE_CSF
struct kbase_core_mask {
	u64 new_core_mask;
};

static int core_mask_parse(struct kbase_device *const kbdev, const char *const buf,
			   struct kbase_core_mask *const mask)
{
	int err = kstrtou64(buf, 0, &mask->new_core_mask);

	if (err) {
#if IS_ENABLED(CONFIG_MALI_MTK_PREVENT_PRINTK_TOO_MUCH)
		dev_dbg(kbdev->dev, "Couldn't process core mask write operation.\n");
#else
		dev_err(kbdev->dev, "Couldn't process core mask write operation.\n");
#endif
        }
	return err;
}

static int core_mask_set(struct kbase_device *kbdev, struct kbase_core_mask *const new_mask)
{
	u64 new_core_mask = new_mask->new_core_mask;
	u64 shader_present = kbdev->gpu_props.shader_present;

	lockdep_assert_held(&kbdev->pm.lock);
	lockdep_assert_held(&kbdev->hwaccess_lock);

	if ((new_core_mask & shader_present) != new_core_mask) {
#if IS_ENABLED(CONFIG_MALI_MTK_PREVENT_PRINTK_TOO_MUCH)
		dev_dbg(kbdev->dev,
#else
		dev_err(kbdev->dev,
#endif
			"Invalid core mask 0x%llX: Includes non-existent cores (present = 0x%llX)",
			new_core_mask, shader_present);
		return -EINVAL;

	} else if (!(new_core_mask & shader_present & kbdev->pm.backend.ca_cores_enabled)) {
		dev_err(kbdev->dev,
			"Invalid core mask 0x%llX: No intersection with currently available cores (present = 0x%llX, CA enabled = 0x%llX)",
			new_core_mask, kbdev->gpu_props.shader_present,
			kbdev->pm.backend.ca_cores_enabled);
		return -EINVAL;
	}

	if (kbdev->pm.debug_core_mask != new_core_mask)
		kbase_pm_set_debug_core_mask(kbdev, new_core_mask);

	return 0;
}
#else
struct kbase_core_mask {
	u64 new_core_mask[BASE_JM_MAX_NR_SLOTS];
};

static int core_mask_parse(struct kbase_device *const kbdev, const char *const buf,
			   struct kbase_core_mask *const mask)
{
	int items;
	char *buf_tmp;
	char *token;

	buf_tmp = kstrdup(buf, GFP_KERNEL);
	if (buf_tmp == NULL)
		goto error;

	for (items = 0; items < BASE_JM_MAX_NR_SLOTS; items++) {
		token = strsep(&buf_tmp, " ");
		if (token == NULL || kstrtoull(token, 16, &mask->new_core_mask[items]) < 0)
			break;
	}

	kfree(buf_tmp);

	if (items != 1 && items != BASE_JM_MAX_NR_SLOTS) {
error:
		dev_err(kbdev->dev, "Couldn't process core mask write operation.\n"
				    "Use format <core_mask>\n"
				    "or <core_mask_js0> <core_mask_js1> <core_mask_js2>\n");
		return -EINVAL;
	}

	if (items == 1) {
		size_t i;

		for (i = 1; i < BASE_JM_MAX_NR_SLOTS; i++)
			mask->new_core_mask[i] = mask->new_core_mask[0];
	}

	return 0;
}

static int core_mask_set(struct kbase_device *kbdev, struct kbase_core_mask *const new_mask)
{
	u64 shader_present = kbdev->gpu_props.shader_present;
	u64 group_core_mask = kbdev->gpu_props.coherency_info.group.core_mask;
	u64 *new_core_mask = &new_mask->new_core_mask[0];
	size_t i;

	for (i = 0; i < BASE_JM_MAX_NR_SLOTS; ++i) {
		if ((new_core_mask[i] & shader_present) != new_core_mask[i]) {
			dev_err(kbdev->dev,
				"Invalid core mask 0x%llX for JS %zu: Includes non-existent cores (present = 0x%llX)",
				new_core_mask[i], i, shader_present);
			return -EINVAL;

		} else if (!(new_core_mask[i] & shader_present &
			     kbdev->pm.backend.ca_cores_enabled)) {
			dev_err(kbdev->dev,
				"Invalid core mask 0x%llX for JS %zu: No intersection with currently available cores (present = 0x%llX, CA enabled = 0x%llX)",
				new_core_mask[i], i, kbdev->gpu_props.shader_present,
				kbdev->pm.backend.ca_cores_enabled);
			return -EINVAL;
		} else if (!(new_core_mask[i] & group_core_mask)) {
			dev_err(kbdev->dev,
				"Invalid core mask 0x%llX for JS %zu: No intersection with group 0 core mask 0x%llX",
				new_core_mask[i], i, group_core_mask);
			return -EINVAL;
		} else if (!(new_core_mask[i] & kbdev->gpu_props.curr_config.shader_present)) {
			dev_err(kbdev->dev,
				"Invalid core mask 0x%llX for JS %zu: No intersection with current core mask 0x%llX",
				new_core_mask[i], i, kbdev->gpu_props.curr_config.shader_present);
			return -EINVAL;
		}
	}

	for (i = 0; i < BASE_JM_MAX_NR_SLOTS; i++) {
		if (kbdev->pm.debug_core_mask[i] != new_core_mask[i]) {
			kbase_pm_set_debug_core_mask(kbdev, new_core_mask, BASE_JM_MAX_NR_SLOTS);
			break;
		}
	}

	return 0;
}

#endif

static ssize_t core_mask_store(struct device *dev, struct device_attribute *attr, const char *buf,
			       size_t count)
{
	struct kbase_device *kbdev;
	struct kbase_core_mask core_mask = {};

	int err;
	unsigned long flags;

	CSTD_UNUSED(attr);

	kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

	err = core_mask_parse(kbdev, buf, &core_mask);
	if (err)
		return err;

	mutex_lock(&kbdev->pm.lock);
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	err = core_mask_set(kbdev, &core_mask);

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	mutex_unlock(&kbdev->pm.lock);

	if (err)
		return err;

	return count;
}

static DEVICE_ATTR_RW(core_mask);

#if !MALI_USE_CSF

static ssize_t soft_job_timeout_store(struct device *dev, struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	int soft_job_timeout_ms;

	CSTD_UNUSED(attr);

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	if ((kstrtoint(buf, 0, &soft_job_timeout_ms) != 0) || (soft_job_timeout_ms <= 0))
		return -EINVAL;

	atomic_set(&kbdev->js_data.soft_job_timeout_ms, soft_job_timeout_ms);

	return (ssize_t)count;
}

static ssize_t soft_job_timeout_show(struct device *dev, struct device_attribute *attr,
				     char *const buf)
{
	struct kbase_device *kbdev;

	CSTD_UNUSED(attr);

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	return scnprintf(buf, PAGE_SIZE, "%i\n", atomic_read(&kbdev->js_data.soft_job_timeout_ms));
}

static DEVICE_ATTR_RW(soft_job_timeout);

static u32 timeout_ms_to_ticks(struct kbase_device *kbdev, long timeout_ms, int default_ticks,
			       u32 old_ticks)
{
	if (timeout_ms > 0) {
		u64 ticks = (u64)timeout_ms * 1000000ULL;

		do_div(ticks, kbdev->js_data.scheduling_period_ns);
		if (!ticks)
			return 1;
		return ticks;
	} else if (timeout_ms < 0) {
		return (u32)default_ticks;
	} else {
		return old_ticks;
	}
}

static ssize_t js_timeouts_store(struct device *dev, struct device_attribute *attr, const char *buf,
				 size_t count)
{
	struct kbase_device *kbdev;
	long js_soft_stop_ms;
	long js_soft_stop_ms_cl;
	long js_hard_stop_ms_ss;
	long js_hard_stop_ms_cl;
	long js_hard_stop_ms_dumping;
	long js_reset_ms_ss;
	long js_reset_ms_cl;
	long js_reset_ms_dumping;
	char *buf_tmp = NULL;
	char *token;
	unsigned long flags;
	struct kbasep_js_device_data *js_data;

	CSTD_UNUSED(attr);

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;
	buf_tmp = kstrdup(buf, GFP_KERNEL);
	if (buf_tmp == NULL)
		goto error;

	token = strsep(&buf_tmp, " ");
	if (token == NULL || kstrtol(token, 10, &js_soft_stop_ms) < 0)
		goto error;

	token = strsep(&buf_tmp, " ");
	if (token == NULL || kstrtol(token, 10, &js_soft_stop_ms_cl) < 0)
		goto error;

	token = strsep(&buf_tmp, " ");
	if (token == NULL || kstrtol(token, 10, &js_hard_stop_ms_ss) < 0)
		goto error;

	token = strsep(&buf_tmp, " ");
	if (token == NULL || kstrtol(token, 10, &js_hard_stop_ms_cl) < 0)
		goto error;

	token = strsep(&buf_tmp, " ");
	if (token == NULL || kstrtol(token, 10, &js_hard_stop_ms_dumping) < 0)
		goto error;

	token = strsep(&buf_tmp, " ");
	if (token == NULL || kstrtol(token, 10, &js_reset_ms_ss) < 0)
		goto error;

	token = strsep(&buf_tmp, " ");
	if (token == NULL || kstrtol(token, 10, &js_reset_ms_cl) < 0)
		goto error;

	token = strsep(&buf_tmp, " ");
	if (token == NULL || kstrtol(token, 10, &js_reset_ms_dumping) < 0)
		goto error;

	kfree(buf_tmp);
	js_data = &kbdev->js_data;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

#define UPDATE_TIMEOUT(ticks_name, ms_name, default)                                        \
	do {                                                                                \
		js_data->ticks_name =                                                       \
			timeout_ms_to_ticks(kbdev, ms_name, default, js_data->ticks_name);  \
		dev_dbg(kbdev->dev, "Overriding " #ticks_name " with %lu ticks (%lu ms)\n", \
			(unsigned long)js_data->ticks_name, ms_name);                       \
	} while (0)

	UPDATE_TIMEOUT(soft_stop_ticks, js_soft_stop_ms, DEFAULT_JS_SOFT_STOP_TICKS);
	UPDATE_TIMEOUT(soft_stop_ticks_cl, js_soft_stop_ms_cl, DEFAULT_JS_SOFT_STOP_TICKS_CL);
	UPDATE_TIMEOUT(hard_stop_ticks_ss, js_hard_stop_ms_ss, DEFAULT_JS_HARD_STOP_TICKS_SS);
	UPDATE_TIMEOUT(hard_stop_ticks_cl, js_hard_stop_ms_cl, DEFAULT_JS_HARD_STOP_TICKS_CL);
	UPDATE_TIMEOUT(hard_stop_ticks_dumping, js_hard_stop_ms_dumping,
		       DEFAULT_JS_HARD_STOP_TICKS_DUMPING);
	UPDATE_TIMEOUT(gpu_reset_ticks_ss, js_reset_ms_ss, DEFAULT_JS_RESET_TICKS_SS);
	UPDATE_TIMEOUT(gpu_reset_ticks_cl, js_reset_ms_cl, DEFAULT_JS_RESET_TICKS_CL);
	UPDATE_TIMEOUT(gpu_reset_ticks_dumping, js_reset_ms_dumping,
		       DEFAULT_JS_RESET_TICKS_DUMPING);

	kbase_js_set_timeouts(kbdev);

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	return (ssize_t)count;
error:
	kfree(buf_tmp);
	dev_err(kbdev->dev,
		"Couldn't process js_timeouts write operation.\n"
		"Use format <soft_stop_ms> <soft_stop_ms_cl> <hard_stop_ms_ss> <hard_stop_ms_cl> <hard_stop_ms_dumping> <reset_ms_ss> <reset_ms_cl> <reset_ms_dumping>\n"
		"Write 0 for no change, -1 to restore default timeout\n");
	return -EINVAL;
}

static unsigned long get_js_timeout_in_ms(u32 scheduling_period_ns, u32 ticks)
{
	u64 ms = (u64)ticks * scheduling_period_ns;

	do_div(ms, 1000000UL);
	return ms;
}

static ssize_t js_timeouts_show(struct device *dev, struct device_attribute *attr, char *const buf)
{
	struct kbase_device *kbdev;
	ssize_t ret;
	unsigned long js_soft_stop_ms;
	unsigned long js_soft_stop_ms_cl;
	unsigned long js_hard_stop_ms_ss;
	unsigned long js_hard_stop_ms_cl;
	unsigned long js_hard_stop_ms_dumping;
	unsigned long js_reset_ms_ss;
	unsigned long js_reset_ms_cl;
	unsigned long js_reset_ms_dumping;
	u32 scheduling_period_ns;

	CSTD_UNUSED(attr);

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	scheduling_period_ns = kbdev->js_data.scheduling_period_ns;

#define GET_TIMEOUT(name) get_js_timeout_in_ms(scheduling_period_ns, kbdev->js_data.name)

	js_soft_stop_ms = GET_TIMEOUT(soft_stop_ticks);
	js_soft_stop_ms_cl = GET_TIMEOUT(soft_stop_ticks_cl);
	js_hard_stop_ms_ss = GET_TIMEOUT(hard_stop_ticks_ss);
	js_hard_stop_ms_cl = GET_TIMEOUT(hard_stop_ticks_cl);
	js_hard_stop_ms_dumping = GET_TIMEOUT(hard_stop_ticks_dumping);
	js_reset_ms_ss = GET_TIMEOUT(gpu_reset_ticks_ss);
	js_reset_ms_cl = GET_TIMEOUT(gpu_reset_ticks_cl);
	js_reset_ms_dumping = GET_TIMEOUT(gpu_reset_ticks_dumping);

#undef GET_TIMEOUT

	ret = scnprintf(buf, PAGE_SIZE, "%lu %lu %lu %lu %lu %lu %lu %lu\n", js_soft_stop_ms,
			js_soft_stop_ms_cl, js_hard_stop_ms_ss, js_hard_stop_ms_cl,
			js_hard_stop_ms_dumping, js_reset_ms_ss, js_reset_ms_cl,
			js_reset_ms_dumping);

	if (ret >= (ssize_t)PAGE_SIZE) {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

	return ret;
}

static DEVICE_ATTR_RW(js_timeouts);

static u32 get_new_js_timeout(u32 old_period, u32 old_ticks, u32 new_scheduling_period_ns)
{
	u64 ticks = (u64)old_period * (u64)old_ticks;

	do_div(ticks, new_scheduling_period_ns);
	return ticks ? ticks : 1;
}

static ssize_t js_scheduling_period_store(struct device *dev, struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	int ret;
	unsigned int js_scheduling_period;
	u32 new_scheduling_period_ns;
	u32 old_period;
	struct kbasep_js_device_data *js_data;
	unsigned long flags;

	CSTD_UNUSED(attr);

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	js_data = &kbdev->js_data;

	ret = kstrtouint(buf, 0, &js_scheduling_period);
	if (ret || !js_scheduling_period) {
		dev_err(kbdev->dev, "Couldn't process js_scheduling_period write operation.\n"
				    "Use format <js_scheduling_period_ms>\n");
		return -EINVAL;
	}

	new_scheduling_period_ns = js_scheduling_period * 1000000;

	mutex_lock(&js_data->runpool_mutex);
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	old_period = js_data->scheduling_period_ns;

#define SET_TIMEOUT(name) \
	(js_data->name =  \
		 get_new_js_timeout(old_period, kbdev->js_data.name, new_scheduling_period_ns))

	SET_TIMEOUT(soft_stop_ticks);
	SET_TIMEOUT(soft_stop_ticks_cl);
	SET_TIMEOUT(hard_stop_ticks_ss);
	SET_TIMEOUT(hard_stop_ticks_cl);
	SET_TIMEOUT(hard_stop_ticks_dumping);
	SET_TIMEOUT(gpu_reset_ticks_ss);
	SET_TIMEOUT(gpu_reset_ticks_cl);
	SET_TIMEOUT(gpu_reset_ticks_dumping);

#undef SET_TIMEOUT

	js_data->scheduling_period_ns = new_scheduling_period_ns;

	kbase_js_set_timeouts(kbdev);

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	mutex_unlock(&js_data->runpool_mutex);

	dev_dbg(kbdev->dev, "JS scheduling period: %dms\n", js_scheduling_period);

	return (ssize_t)count;
}

static ssize_t js_scheduling_period_show(struct device *dev, struct device_attribute *attr,
					 char *const buf)
{
	struct kbase_device *kbdev;
	u32 period;
	ssize_t ret;

	CSTD_UNUSED(attr);

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	period = kbdev->js_data.scheduling_period_ns;

	ret = scnprintf(buf, PAGE_SIZE, "%d\n", period / 1000000);

	return ret;
}

static DEVICE_ATTR_RW(js_scheduling_period);

#ifdef CONFIG_MALI_DEBUG
static ssize_t js_softstop_always_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	int ret;
	int softstop_always;

	CSTD_UNUSED(attr);

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	ret = kstrtoint(buf, 0, &softstop_always);
	if (ret || ((softstop_always != 0) && (softstop_always != 1))) {
		dev_err(kbdev->dev, "Couldn't process js_softstop_always write operation.\n"
				    "Use format <soft_stop_always>\n");
		return -EINVAL;
	}

	kbdev->js_data.softstop_always = (bool)softstop_always;
	dev_dbg(kbdev->dev, "Support for softstop on a single context: %s\n",
		(kbdev->js_data.softstop_always) ? "Enabled" : "Disabled");
	return (ssize_t)count;
}

static ssize_t js_softstop_always_show(struct device *dev, struct device_attribute *attr,
				       char *const buf)
{
	struct kbase_device *kbdev;
	ssize_t ret;

	CSTD_UNUSED(attr);

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	ret = scnprintf(buf, PAGE_SIZE, "%d\n", kbdev->js_data.softstop_always);

	if (ret >= (ssize_t)PAGE_SIZE) {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

	return ret;
}

static DEVICE_ATTR_RW(js_softstop_always);
#endif
#endif

#ifdef CONFIG_MALI_DEBUG
typedef void kbasep_debug_command_func(struct kbase_device *);

enum kbasep_debug_command_code {
	KBASEP_DEBUG_COMMAND_DUMPTRACE,

	KBASEP_DEBUG_COMMAND_COUNT
};

struct kbasep_debug_command {
	char *str;
	kbasep_debug_command_func *func;
};

static void kbasep_ktrace_dump_wrapper(struct kbase_device *kbdev)
{
	KBASE_KTRACE_DUMP(kbdev);
}

static const struct kbasep_debug_command debug_commands[] = { {
	.str = "dumptrace",
	.func = &kbasep_ktrace_dump_wrapper,
} };

static ssize_t debug_command_show(struct device *dev, struct device_attribute *attr,
				  char *const buf)
{
	struct kbase_device *kbdev;
	size_t i;
	ssize_t ret = 0;

	CSTD_UNUSED(attr);

	kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

	for (i = 0; i < KBASEP_DEBUG_COMMAND_COUNT && ret < (ssize_t)PAGE_SIZE; i++)
		ret += scnprintf(buf + ret, (size_t)(PAGE_SIZE - ret), "%s\n",
				 debug_commands[i].str);

	if (ret >= (ssize_t)PAGE_SIZE) {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

	return ret;
}

static ssize_t debug_command_store(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	int i;

	CSTD_UNUSED(attr);

	kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

	for (i = 0; i < KBASEP_DEBUG_COMMAND_COUNT; i++) {
		if (sysfs_streq(debug_commands[i].str, buf)) {
			debug_commands[i].func(kbdev);
			return (ssize_t)count;
		}
	}

	dev_err(dev, "debug_command: command not known\n");
	return -EINVAL;
}

static DEVICE_ATTR_RW(debug_command);
#endif

static ssize_t gpuinfo_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	static const struct gpu_product_id_name {
		unsigned int id;
		char *name;
	} gpu_product_id_names[] = {
		{ .id = GPU_ID_PRODUCT_TMIX, .name = "Mali-G71" },
		{ .id = GPU_ID_PRODUCT_THEX, .name = "Mali-G72" },
		{ .id = GPU_ID_PRODUCT_TSIX, .name = "Mali-G51" },
		{ .id = GPU_ID_PRODUCT_TNOX, .name = "Mali-G76" },
		{ .id = GPU_ID_PRODUCT_TDVX, .name = "Mali-G31" },
		{ .id = GPU_ID_PRODUCT_TGOX, .name = "Mali-G52" },
		{ .id = GPU_ID_PRODUCT_TTRX, .name = "Mali-G77" },
		{ .id = GPU_ID_PRODUCT_TBEX, .name = "Mali-G78" },
		{ .id = GPU_ID_PRODUCT_TBAX, .name = "Mali-G78AE" },
		{ .id = GPU_ID_PRODUCT_LBEX, .name = "Mali-G68" },
		{ .id = GPU_ID_PRODUCT_TNAX, .name = "Mali-G57" },
		{ .id = GPU_ID_PRODUCT_TODX, .name = "Mali-G710" },
		{ .id = GPU_ID_PRODUCT_LODX, .name = "Mali-G610" },
		{ .id = GPU_ID_PRODUCT_TGRX, .name = "Mali-G510" },
		{ .id = GPU_ID_PRODUCT_TVAX, .name = "Mali-G310" },
		{ .id = GPU_ID_PRODUCT_LTUX, .name = "Mali-G615" },
		{ .id = GPU_ID_PRODUCT_LTIX, .name = "Mali-G620" },
		{ .id = GPU_ID_PRODUCT_TKRX, .name = "Mali-G725" },
		{ .id = GPU_ID_PRODUCT_LKRX, .name = "Mali-G625" },
	};
	const char *product_name = "(Unknown Mali GPU)";
	struct kbase_device *kbdev;
	u32 product_id;
	u32 product_model;
	unsigned int i;
	struct kbase_gpu_props *gpu_props;

	CSTD_UNUSED(attr);

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	gpu_props = &kbdev->gpu_props;
	product_id = gpu_props->gpu_id.product_id;
	product_model = gpu_props->gpu_id.product_model;

	for (i = 0; i < ARRAY_SIZE(gpu_product_id_names); ++i) {
		const struct gpu_product_id_name *p = &gpu_product_id_names[i];

		if (p->id == product_model) {
			product_name = p->name;
			break;
		}
	}

#if MALI_USE_CSF
	if (product_model == GPU_ID_PRODUCT_TTUX) {
		const bool rt_supported = gpu_props->gpu_features.ray_intersection;
		const u8 nr_cores = gpu_props->num_cores;

		if ((nr_cores > 10) && rt_supported)
			product_name = "Mali-G715-Immortalis";
		else if (nr_cores >= 7)
			product_name = "Mali-G715";

		if (nr_cores < 7) {
			dev_warn(kbdev->dev, "nr_cores(%u) GPU ID must be G615", nr_cores);
			product_name = "Mali-G615";
		} else
			dev_dbg(kbdev->dev, "GPU ID_Name: %s, nr_cores(%u)\n", product_name,
				nr_cores);
	}

	if (product_model == GPU_ID_PRODUCT_TTIX) {
		const bool rt_supported = gpu_props->gpu_features.ray_intersection;
		const u8 nr_cores = gpu_props->num_cores;

		if ((nr_cores >= 10) && rt_supported)
			product_name = "Mali-G720-Immortalis";
		else
			product_name = (nr_cores >= 6) ? "Mali-G720" : "Mali-G620";

		dev_dbg(kbdev->dev, "GPU ID_Name: %s (ID: 0x%x), nr_cores(%u)\n", product_name,
			product_id, nr_cores);
	}

	if (product_model == GPU_ID_PRODUCT_TKRX) {
		const bool rt_supported = gpu_props->gpu_features.ray_intersection;
		const u8 nr_cores = gpu_props->num_cores;

		if ((nr_cores >= 10) && rt_supported)
			product_name = "Mali-G925-Immortalis";
		else
			product_name = (nr_cores >= 6) ? "Mali-G725" : "Mali-G625";

		dev_dbg(kbdev->dev, "GPU ID_Name: %s (ID: 0x%x), nr_cores(%u)\n", product_name,
			product_id, nr_cores);
	}

#endif

	return scnprintf(buf, PAGE_SIZE, "%s %d cores r%dp%d 0x%08X\n", product_name,
			 kbdev->gpu_props.num_cores, gpu_props->gpu_id.version_major,
			 gpu_props->gpu_id.version_minor, product_id);
}
static DEVICE_ATTR_RO(gpuinfo);

#if IS_ENABLED(CONFIG_MALI_MTK_UNHANDLED_PAGE_FAULT_DEBUG)
static ssize_t upf_counter_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kbase_device *kbdev;

	CSTD_UNUSED(attr);

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	return scnprintf(buf, PAGE_SIZE, "upf_counter = %lld\n", mtk_common_upf_counter_get());
}
static ssize_t upf_counter_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev = dev_get_drvdata(dev);
	u32 reset_value;

	CSTD_UNUSED(attr);

	if (!kbdev) {
		pr_info("[KBASE] Bad kbdev!\n");
		return -ENODEV;
	}

	if (kstrtouint(buf, 0, &reset_value))
		return -EINVAL;

	if (reset_value == 0) {
		mtk_common_upf_counter_reset();
	}

	return count;
}
static DEVICE_ATTR_RW(upf_counter);
#endif

static ssize_t dvfs_period_store(struct device *dev, struct device_attribute *attr, const char *buf,
				 size_t count)
{
	struct kbase_device *kbdev;
	int ret;
	int dvfs_period;

	CSTD_UNUSED(attr);

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	ret = kstrtoint(buf, 0, &dvfs_period);
	if (ret || dvfs_period <= 0) {
		dev_err(kbdev->dev, "Couldn't process dvfs_period write operation.\n"
				    "Use format <dvfs_period_ms>\n");
		return -EINVAL;
	}

	kbdev->pm.dvfs_period = (u32)dvfs_period;
	dev_dbg(kbdev->dev, "DVFS period: %dms\n", dvfs_period);

	return (ssize_t)count;
}

static ssize_t dvfs_period_show(struct device *dev, struct device_attribute *attr, char *const buf)
{
	struct kbase_device *kbdev;
	ssize_t ret;

	CSTD_UNUSED(attr);

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	ret = scnprintf(buf, PAGE_SIZE, "%d\n", kbdev->pm.dvfs_period);

	return ret;
}

static DEVICE_ATTR_RW(dvfs_period);

int kbase_pm_gpu_freq_init(struct kbase_device *kbdev)
{
	int err;

	u64 lowest_freq_khz = DEFAULT_REF_TIMEOUT_FREQ_KHZ;

#ifdef CONFIG_PM_OPP
	struct dev_pm_opp *opp_ptr;
	unsigned long found_freq = 0;

	opp_ptr = dev_pm_opp_find_freq_ceil(kbdev->dev, &found_freq);
	if (IS_ERR(opp_ptr)) {
		dev_err(kbdev->dev, "No OPPs found in device tree! Scaling timeouts using %llu kHz",
			(unsigned long long)lowest_freq_khz);
	} else {
#if KERNEL_VERSION(4, 11, 0) <= LINUX_VERSION_CODE
		dev_pm_opp_put(opp_ptr);
#endif

		found_freq /= 1000;

		lowest_freq_khz = found_freq;
	}
#else
	dev_err(kbdev->dev, "No operating-points-v2 node or operating-points property in DT");
#endif

	kbdev->lowest_gpu_freq_khz = lowest_freq_khz;

	err = kbase_device_populate_max_freq(kbdev);
	if (unlikely(err < 0))
		return -1;

	dev_dbg(kbdev->dev, "Lowest frequency identified is %llu kHz", kbdev->lowest_gpu_freq_khz);
	dev_dbg(kbdev->dev,
		"Setting default highest frequency to %u kHz (pending devfreq initialization",
		kbdev->gpu_props.gpu_freq_khz_max);

	return 0;
}

static ssize_t pm_poweroff_store(struct device *dev, struct device_attribute *attr, const char *buf,
				 size_t count)
{
	struct kbase_device *kbdev;
	struct kbasep_pm_tick_timer_state *stt;
	u64 gpu_poweroff_time;
	unsigned int poweroff_shader_ticks, poweroff_gpu_ticks;
	unsigned long flags;
	char *buf_tmp;
	char *token;

	CSTD_UNUSED(attr);

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	buf_tmp = kstrdup(buf, GFP_KERNEL);
	if (buf_tmp == NULL)
		goto error;

	token = strsep(&buf_tmp, " ");
	if (token == NULL || kstrtoull(token, 10, &gpu_poweroff_time) < 0)
		goto error;

	token = strsep(&buf_tmp, " ");
	if (token == NULL || kstrtou32(token, 10, &poweroff_shader_ticks) < 0)
		goto error;

	token = strsep(&buf_tmp, " ");
	if (token == NULL || kstrtou32(token, 10, &poweroff_gpu_ticks) < 0)
		goto error;
	kfree(buf_tmp);
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	stt = &kbdev->pm.backend.shader_tick_timer;
	stt->configured_interval = HR_TIMER_DELAY_NSEC(gpu_poweroff_time);
	stt->default_ticks = poweroff_shader_ticks;
	stt->configured_ticks = stt->default_ticks;

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	if (poweroff_gpu_ticks != 0)
		dev_warn(kbdev->dev, "Separate GPU poweroff delay no longer supported.\n");

	return (ssize_t)count;
error:
	kfree(buf_tmp);
	dev_err(kbdev->dev,
		"Couldn't process pm_poweroff write operation.\n"
		"Use format <gpu_poweroff_time_ns> <poweroff_shader_ticks> <poweroff_gpu_ticks>\n");
	return -EINVAL;
}

static ssize_t pm_poweroff_show(struct device *dev, struct device_attribute *attr, char *const buf)
{
	struct kbase_device *kbdev;
	struct kbasep_pm_tick_timer_state *stt;
	ssize_t ret;
	unsigned long flags;

	CSTD_UNUSED(attr);

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	stt = &kbdev->pm.backend.shader_tick_timer;
	ret = scnprintf(buf, PAGE_SIZE, "%llu %u 0\n", ktime_to_ns(stt->configured_interval),
			stt->default_ticks);

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	return ret;
}

static DEVICE_ATTR_RW(pm_poweroff);

static ssize_t reset_timeout_store(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	int ret;
	u32 reset_timeout;
	u32 default_reset_timeout;

	CSTD_UNUSED(attr);

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	ret = kstrtou32(buf, 0, &reset_timeout);
	if (ret || reset_timeout == 0) {
		dev_err(kbdev->dev, "Couldn't process reset_timeout write operation.\n"
				    "Use format <reset_timeout_ms>\n");
		return -EINVAL;
	}

#if MALI_USE_CSF
	default_reset_timeout = kbase_get_timeout_ms(kbdev, CSF_GPU_RESET_TIMEOUT);
#else
	default_reset_timeout = JM_DEFAULT_RESET_TIMEOUT_MS;
#endif

	if (reset_timeout < default_reset_timeout)
		dev_warn(kbdev->dev, "requested reset_timeout(%u) is smaller than default(%u)",
			 reset_timeout, default_reset_timeout);

	kbdev->reset_timeout_ms = reset_timeout;
	dev_dbg(kbdev->dev, "Reset timeout: %ums\n", reset_timeout);

	return (ssize_t)count;
}

static ssize_t reset_timeout_show(struct device *dev, struct device_attribute *attr,
				  char *const buf)
{
	struct kbase_device *kbdev;
	ssize_t ret;

	CSTD_UNUSED(attr);

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	ret = scnprintf(buf, PAGE_SIZE, "%d\n", kbdev->reset_timeout_ms);

	return ret;
}

static DEVICE_ATTR_RW(reset_timeout);

static ssize_t mem_pool_size_show(struct device *dev, struct device_attribute *attr,
				  char *const buf)
{
	struct kbase_device *const kbdev = to_kbase_device(dev);

	CSTD_UNUSED(attr);

	if (!kbdev)
		return -ENODEV;

	return kbase_debugfs_helper_get_attr_to_string(buf, PAGE_SIZE, kbdev->mem_pools.small,
						       MEMORY_GROUP_MANAGER_NR_GROUPS,
						       kbase_mem_pool_debugfs_size);
}

static ssize_t mem_pool_size_store(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct kbase_device *const kbdev = to_kbase_device(dev);
	ssize_t err;

	CSTD_UNUSED(attr);

	if (!kbdev)
		return -ENODEV;

	err = kbase_debugfs_helper_set_attr_from_string(buf, kbdev->mem_pools.small,
							MEMORY_GROUP_MANAGER_NR_GROUPS,
							kbase_mem_pool_debugfs_trim);

	return err ? err : (ssize_t)count;
}

static DEVICE_ATTR_RW(mem_pool_size);

static ssize_t mem_pool_max_size_show(struct device *dev, struct device_attribute *attr,
				      char *const buf)
{
	struct kbase_device *const kbdev = to_kbase_device(dev);

	CSTD_UNUSED(attr);

	if (!kbdev)
		return -ENODEV;

	return kbase_debugfs_helper_get_attr_to_string(buf, PAGE_SIZE, kbdev->mem_pools.small,
						       MEMORY_GROUP_MANAGER_NR_GROUPS,
						       kbase_mem_pool_debugfs_max_size);
}

static ssize_t mem_pool_max_size_store(struct device *dev, struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct kbase_device *const kbdev = to_kbase_device(dev);
	ssize_t err;

	CSTD_UNUSED(attr);

	if (!kbdev)
		return -ENODEV;

	err = kbase_debugfs_helper_set_attr_from_string(buf, kbdev->mem_pools.small,
							MEMORY_GROUP_MANAGER_NR_GROUPS,
							kbase_mem_pool_debugfs_set_max_size);

	return err ? err : (ssize_t)count;
}

static DEVICE_ATTR_RW(mem_pool_max_size);

static ssize_t lp_mem_pool_size_show(struct device *dev, struct device_attribute *attr,
				     char *const buf)
{
	struct kbase_device *const kbdev = to_kbase_device(dev);

	CSTD_UNUSED(attr);

	if (!kbdev)
		return -ENODEV;

	return kbase_debugfs_helper_get_attr_to_string(buf, PAGE_SIZE, kbdev->mem_pools.large,
						       MEMORY_GROUP_MANAGER_NR_GROUPS,
						       kbase_mem_pool_debugfs_size);
}

static ssize_t lp_mem_pool_size_store(struct device *dev, struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct kbase_device *const kbdev = to_kbase_device(dev);
	ssize_t err;

	CSTD_UNUSED(attr);

	if (!kbdev)
		return -ENODEV;

	err = kbase_debugfs_helper_set_attr_from_string(buf, kbdev->mem_pools.large,
							MEMORY_GROUP_MANAGER_NR_GROUPS,
							kbase_mem_pool_debugfs_trim);

	return err ? err : (ssize_t)count;
}

static DEVICE_ATTR_RW(lp_mem_pool_size);

static ssize_t lp_mem_pool_max_size_show(struct device *dev, struct device_attribute *attr,
					 char *const buf)
{
	struct kbase_device *const kbdev = to_kbase_device(dev);

	CSTD_UNUSED(attr);

	if (!kbdev)
		return -ENODEV;

	return kbase_debugfs_helper_get_attr_to_string(buf, PAGE_SIZE, kbdev->mem_pools.large,
						       MEMORY_GROUP_MANAGER_NR_GROUPS,
						       kbase_mem_pool_debugfs_max_size);
}

static ssize_t lp_mem_pool_max_size_store(struct device *dev, struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct kbase_device *const kbdev = to_kbase_device(dev);
	ssize_t err;

	CSTD_UNUSED(attr);

	if (!kbdev)
		return -ENODEV;

	err = kbase_debugfs_helper_set_attr_from_string(buf, kbdev->mem_pools.large,
							MEMORY_GROUP_MANAGER_NR_GROUPS,
							kbase_mem_pool_debugfs_set_max_size);

	return err ? err : (ssize_t)count;
}

static DEVICE_ATTR_RW(lp_mem_pool_max_size);

static ssize_t show_simplified_mem_pool_max_size(struct device *dev, struct device_attribute *attr,
						 char *const buf)
{
	struct kbase_device *const kbdev = to_kbase_device(dev);

	CSTD_UNUSED(attr);

	if (!kbdev)
		return -ENODEV;

	return kbase_debugfs_helper_get_attr_to_string(buf, PAGE_SIZE, kbdev->mem_pools.small, 1,
						       kbase_mem_pool_debugfs_max_size);
}

static ssize_t set_simplified_mem_pool_max_size(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t count)
{
	struct kbase_device *const kbdev = to_kbase_device(dev);
	unsigned long new_size;
	size_t gid;
	int err;

	CSTD_UNUSED(attr);

	if (!kbdev)
		return -ENODEV;

	err = kstrtoul(buf, 0, &new_size);
	if (err)
		return -EINVAL;

	for (gid = 0; gid < MEMORY_GROUP_MANAGER_NR_GROUPS; ++gid)
		kbase_mem_pool_debugfs_set_max_size(kbdev->mem_pools.small, gid, (size_t)new_size);

	return (ssize_t)count;
}

static DEVICE_ATTR(max_size, 0600, show_simplified_mem_pool_max_size,
		   set_simplified_mem_pool_max_size);

static ssize_t show_simplified_lp_mem_pool_max_size(struct device *dev,
						    struct device_attribute *attr, char *const buf)
{
	struct kbase_device *const kbdev = to_kbase_device(dev);

	CSTD_UNUSED(attr);

	if (!kbdev)
		return -ENODEV;

	return kbase_debugfs_helper_get_attr_to_string(buf, PAGE_SIZE, kbdev->mem_pools.large, 1,
						       kbase_mem_pool_debugfs_max_size);
}

static ssize_t set_simplified_lp_mem_pool_max_size(struct device *dev,
						   struct device_attribute *attr, const char *buf,
						   size_t count)
{
	struct kbase_device *const kbdev = to_kbase_device(dev);
	unsigned long new_size;
	size_t gid;
	int err;

	CSTD_UNUSED(attr);

	if (!kbdev)
		return -ENODEV;

	err = kstrtoul(buf, 0, &new_size);
	if (err)
		return -EINVAL;

	for (gid = 0; gid < MEMORY_GROUP_MANAGER_NR_GROUPS; ++gid)
		kbase_mem_pool_debugfs_set_max_size(kbdev->mem_pools.large, gid, (size_t)new_size);

	return (ssize_t)count;
}

static DEVICE_ATTR(lp_max_size, 0600, show_simplified_lp_mem_pool_max_size,
		   set_simplified_lp_mem_pool_max_size);

static ssize_t show_simplified_ctx_default_max_size(struct device *dev,
						    struct device_attribute *attr, char *const buf)
{
	struct kbase_device *kbdev = to_kbase_device(dev);
	size_t max_size;

	CSTD_UNUSED(attr);

	if (!kbdev)
		return -ENODEV;

	max_size = kbase_mem_pool_config_debugfs_max_size(kbdev->mem_pool_defaults.small, 0);

	return scnprintf(buf, PAGE_SIZE, "%zu\n", max_size);
}

static ssize_t set_simplified_ctx_default_max_size(struct device *dev,
						   struct device_attribute *attr, const char *buf,
						   size_t count)
{
	struct kbase_device *kbdev;
	unsigned long new_size;
	int err;

	CSTD_UNUSED(attr);

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	err = kstrtoul(buf, 0, &new_size);
	if (err)
		return -EINVAL;

	kbase_mem_pool_group_config_set_max_size(&kbdev->mem_pool_defaults, (size_t)new_size);

	return (ssize_t)count;
}

static DEVICE_ATTR(ctx_default_max_size, 0600, show_simplified_ctx_default_max_size,
		   set_simplified_ctx_default_max_size);

#if !MALI_USE_CSF

static ssize_t js_ctx_scheduling_mode_show(struct device *dev, struct device_attribute *attr,
					   char *const buf)
{
	struct kbase_device *kbdev;

	CSTD_UNUSED(attr);

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	return scnprintf(buf, PAGE_SIZE, "%u\n", kbdev->js_ctx_scheduling_mode);
}

static ssize_t js_ctx_scheduling_mode_store(struct device *dev, struct device_attribute *attr,
					    const char *buf, size_t count)
{
	struct kbase_context *kctx;
	u32 new_js_ctx_scheduling_mode;
	struct kbase_device *kbdev;
	unsigned long flags;
	int ret;

	CSTD_UNUSED(attr);

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	ret = kstrtouint(buf, 0, &new_js_ctx_scheduling_mode);
	if (ret || new_js_ctx_scheduling_mode >= KBASE_JS_PRIORITY_MODE_COUNT) {
		dev_err(kbdev->dev, "Couldn't process js_ctx_scheduling_mode"
				    " write operation.\n"
				    "Use format <js_ctx_scheduling_mode>\n");
		return -EINVAL;
	}

	if (new_js_ctx_scheduling_mode == kbdev->js_ctx_scheduling_mode)
		return (ssize_t)count;

	mutex_lock(&kbdev->kctx_list_lock);
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	kbdev->js_ctx_scheduling_mode = new_js_ctx_scheduling_mode;

	list_for_each_entry(kctx, &kbdev->kctx_list, kctx_list_link)
		kbase_js_update_ctx_priority(kctx);

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	mutex_unlock(&kbdev->kctx_list_lock);

	dev_dbg(kbdev->dev, "JS ctx scheduling mode: %u\n", new_js_ctx_scheduling_mode);

	return (ssize_t)count;
}

static DEVICE_ATTR_RW(js_ctx_scheduling_mode);

#define NR_SERIALIZE_JOBS_SETTINGS 5

#define MAX_SERIALIZE_JOBS_NAME_LEN 16

static struct {
	char *name;
	u8 setting;
} serialize_jobs_settings[NR_SERIALIZE_JOBS_SETTINGS] = {
	{ "none", 0 },
	{ "intra-slot", KBASE_SERIALIZE_INTRA_SLOT },
	{ "inter-slot", KBASE_SERIALIZE_INTER_SLOT },
	{ "full", KBASE_SERIALIZE_INTRA_SLOT | KBASE_SERIALIZE_INTER_SLOT },
	{ "full-reset",
	  KBASE_SERIALIZE_INTRA_SLOT | KBASE_SERIALIZE_INTER_SLOT | KBASE_SERIALIZE_RESET }
};

static ssize_t update_serialize_jobs_setting(struct kbase_device *kbdev, const char *buf,
					     size_t count)
{
	int i;
	bool valid = false;

	for (i = 0; i < NR_SERIALIZE_JOBS_SETTINGS; i++) {
		if (sysfs_streq(serialize_jobs_settings[i].name, buf)) {
			kbdev->serialize_jobs = serialize_jobs_settings[i].setting;
			valid = true;
			break;
		}
	}

	if (!valid) {
		dev_err(kbdev->dev, "serialize_jobs: invalid setting");
		return -EINVAL;
	}

	return (ssize_t)count;
}

#if IS_ENABLED(CONFIG_DEBUG_FS)

static int kbasep_serialize_jobs_seq_debugfs_show(struct seq_file *sfile, void *data)
{
	struct kbase_device *kbdev = sfile->private;
	int i;

	CSTD_UNUSED(data);

	for (i = 0; i < NR_SERIALIZE_JOBS_SETTINGS; i++) {
		if (kbdev->serialize_jobs == serialize_jobs_settings[i].setting)
			seq_printf(sfile, "[%s] ", serialize_jobs_settings[i].name);
		else
			seq_printf(sfile, "%s ", serialize_jobs_settings[i].name);
	}

	seq_puts(sfile, "\n");

	return 0;
}

static ssize_t kbasep_serialize_jobs_debugfs_write(struct file *file, const char __user *ubuf,
						   size_t count, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct kbase_device *kbdev = s->private;
	char buf[MAX_SERIALIZE_JOBS_NAME_LEN];

	CSTD_UNUSED(ppos);

	count = min_t(size_t, sizeof(buf) - 1, count);
	if (copy_from_user(buf, ubuf, count))
		return -EFAULT;

	buf[count] = 0;

	return update_serialize_jobs_setting(kbdev, buf, count);
}

static int kbasep_serialize_jobs_debugfs_open(struct inode *in, struct file *file)
{
	return single_open(file, kbasep_serialize_jobs_seq_debugfs_show, in->i_private);
}

static const struct file_operations kbasep_serialize_jobs_debugfs_fops = {
	.owner = THIS_MODULE,
	.open = kbasep_serialize_jobs_debugfs_open,
	.read = seq_read,
	.write = kbasep_serialize_jobs_debugfs_write,
	.llseek = seq_lseek,
	.release = single_release,
};

#endif

static ssize_t show_serialize_jobs_sysfs(struct device *dev, struct device_attribute *attr,
					 char *buf)
{
	struct kbase_device *kbdev = to_kbase_device(dev);
	ssize_t ret = 0;
	int i;

	CSTD_UNUSED(attr);

	for (i = 0; i < NR_SERIALIZE_JOBS_SETTINGS; i++) {
		if (kbdev->serialize_jobs == serialize_jobs_settings[i].setting)
			ret += scnprintf(buf + ret, (size_t)(PAGE_SIZE - ret), "[%s]",
					 serialize_jobs_settings[i].name);
		else
			ret += scnprintf(buf + ret, (size_t)(PAGE_SIZE - ret), "%s ",
					 serialize_jobs_settings[i].name);
	}

	if (ret < (ssize_t)(PAGE_SIZE - 1)) {
		ret += scnprintf(buf + ret, (size_t)(PAGE_SIZE - ret), "\n");
	} else {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

	return ret;
}

static ssize_t store_serialize_jobs_sysfs(struct device *dev, struct device_attribute *attr,
					  const char *buf, size_t count)
{
	CSTD_UNUSED(attr);
	return update_serialize_jobs_setting(to_kbase_device(dev), buf, count);
}

static DEVICE_ATTR(serialize_jobs, 0600, show_serialize_jobs_sysfs, store_serialize_jobs_sysfs);
#endif

static void kbasep_protected_mode_hwcnt_disable_worker(struct work_struct *data)
{
	struct kbase_device *kbdev =
		container_of(data, struct kbase_device, protected_mode_hwcnt_disable_work);
	spinlock_t *backend_lock;
	unsigned long flags;

	bool do_disable;

#if MALI_USE_CSF
	backend_lock = &kbdev->csf.scheduler.interrupt_lock;
#else
	backend_lock = &kbdev->hwaccess_lock;
#endif

	spin_lock_irqsave(backend_lock, flags);
	do_disable = !kbdev->protected_mode_hwcnt_desired && !kbdev->protected_mode_hwcnt_disabled;
	spin_unlock_irqrestore(backend_lock, flags);

	if (!do_disable)
		return;

	kbase_hwcnt_context_disable(kbdev->hwcnt_gpu_ctx);

	spin_lock_irqsave(backend_lock, flags);
	do_disable = !kbdev->protected_mode_hwcnt_desired && !kbdev->protected_mode_hwcnt_disabled;

	if (do_disable) {

		kbdev->protected_mode_hwcnt_disabled = true;
#if !MALI_USE_CSF
		kbase_backend_slot_update(kbdev);
#endif
	} else {

		kbase_hwcnt_context_enable(kbdev->hwcnt_gpu_ctx);
	}

	spin_unlock_irqrestore(backend_lock, flags);
}

#ifndef PLATFORM_PROTECTED_CALLBACKS
static int kbasep_protected_mode_enable(struct protected_mode_device *pdev)
{
	struct kbase_device *kbdev = pdev->data;

	return kbase_pm_protected_mode_enable(kbdev);
}

static int kbasep_protected_mode_disable(struct protected_mode_device *pdev)
{
	struct kbase_device *kbdev = pdev->data;

	return kbase_pm_protected_mode_disable(kbdev);
}

static const struct protected_mode_ops kbasep_native_protected_ops = {
	.protected_mode_enable = kbasep_protected_mode_enable,
	.protected_mode_disable = kbasep_protected_mode_disable
};

#define PLATFORM_PROTECTED_CALLBACKS (&kbasep_native_protected_ops)
#endif

int kbase_protected_mode_init(struct kbase_device *kbdev)
{

	kbdev->protected_dev = kzalloc(sizeof(*kbdev->protected_dev), GFP_KERNEL);
	if (!kbdev->protected_dev)
		return -ENOMEM;
	kbdev->protected_dev->data = kbdev;
	kbdev->protected_ops = PLATFORM_PROTECTED_CALLBACKS;
	INIT_WORK(&kbdev->protected_mode_hwcnt_disable_work,
		  kbasep_protected_mode_hwcnt_disable_worker);
	kbdev->protected_mode_hwcnt_desired = true;
	return 0;
}

void kbase_protected_mode_term(struct kbase_device *kbdev)
{
	cancel_work_sync(&kbdev->protected_mode_hwcnt_disable_work);
	kfree(kbdev->protected_dev);
}

#if IS_ENABLED(CONFIG_MALI_NO_MALI)
static int kbase_common_reg_map(struct kbase_device *kbdev)
{
	return 0;
}
static void kbase_common_reg_unmap(struct kbase_device *const kbdev)
{
}
#else
static int kbase_common_reg_map(struct kbase_device *kbdev)
{
	int err = 0;

	if (!request_mem_region(kbdev->reg_start, kbdev->reg_size, dev_name(kbdev->dev))) {
		dev_err(kbdev->dev, "Register window unavailable\n");
		err = -EIO;
		goto out_region;
	}

	kbdev->reg = mali_ioremap(kbdev->reg_start, kbdev->reg_size);
	if (!kbdev->reg) {
		dev_err(kbdev->dev, "Can't remap register window\n");
		err = -EINVAL;
		goto out_ioremap;
	}

	return err;

out_ioremap:
	release_mem_region(kbdev->reg_start, kbdev->reg_size);
out_region:
	return err;
}

static void kbase_common_reg_unmap(struct kbase_device *const kbdev)
{
	if (kbdev->reg) {
		mali_iounmap(kbdev->reg);
		release_mem_region(kbdev->reg_start, kbdev->reg_size);
		kbdev->reg = NULL;
		kbdev->reg_start = 0;
		kbdev->reg_size = 0;
	}
}
#endif

int registers_map(struct kbase_device *const kbdev)
{

	struct platform_device *pdev = to_platform_device(kbdev->dev);
	struct resource *reg_res;
	int err;

	reg_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!reg_res) {
		dev_err(kbdev->dev, "Invalid register resource\n");
		return -ENOENT;
	}

	kbdev->reg_start = reg_res->start;
	kbdev->reg_size = resource_size(reg_res);

#if MALI_USE_CSF
	if (kbdev->reg_size <
	    (CSF_HW_DOORBELL_PAGE_OFFSET + CSF_NUM_DOORBELL * CSF_HW_DOORBELL_PAGE_SIZE)) {
		dev_err(kbdev->dev,
			"Insufficient register space, will override to the required size\n");
		kbdev->reg_size =
			CSF_HW_DOORBELL_PAGE_OFFSET + CSF_NUM_DOORBELL * CSF_HW_DOORBELL_PAGE_SIZE;
	}
#endif

	err = kbase_common_reg_map(kbdev);
	if (err) {
		dev_err(kbdev->dev, "Failed to map registers\n");
		return err;
	}

	return 0;
}

void registers_unmap(struct kbase_device *kbdev)
{
	kbase_common_reg_unmap(kbdev);
}

#if defined(CONFIG_MALI_ARBITER_SUPPORT) && defined(CONFIG_OF)
static bool kbase_is_pm_enabled(const struct device_node *gpu_node)
{
	const struct device_node *power_model_node;
	const void *cooling_cells_node;
	const void *operating_point_node;
	bool is_pm_enable = false;

	power_model_node = of_get_child_by_name(gpu_node, "power-model");
	if (!power_model_node)
		power_model_node = of_get_child_by_name(gpu_node, "power_model");

	if (power_model_node)
		is_pm_enable = true;

	cooling_cells_node = of_get_property(gpu_node, "#cooling-cells", NULL);
	if (cooling_cells_node)
		is_pm_enable = true;

	operating_point_node = of_get_property(gpu_node, "operating-points", NULL);
	if (operating_point_node)
		is_pm_enable = true;

	return is_pm_enable;
}

static bool kbase_is_full_coherency_enabled(const struct device_node *gpu_node)
{
	const void *coherency_dts;
	u32 coherency;

	coherency_dts = of_get_property(gpu_node, "system-coherency", NULL);
	if (coherency_dts) {
		coherency = be32_to_cpup(coherency_dts);
		if (coherency == COHERENCY_ACE)
			return true;
	}
	return false;
}
#endif

int kbase_device_backend_init(struct kbase_device *kbdev)
{
	int err = 0;

#if defined(CONFIG_MALI_ARBITER_SUPPORT) && defined(CONFIG_OF)

	if (kbase_is_pm_enabled(kbdev->dev->of_node)) {

		dev_dbg(kbdev->dev, "Arbitration not supported with power management");
		return -EPERM;
	}

	if (kbase_is_full_coherency_enabled(kbdev->dev->of_node)) {

		dev_dbg(kbdev->dev, "Arbitration not supported with full coherency");
		return -EPERM;
	}

	err = kbase_arbiter_pm_early_init(kbdev);
	if (err == 0) {
#if !MALI_USE_CSF
		u32 product_model;

		if (kbdev->gpu_props.gpu_id.arch_major == 0)
			kbase_gpuprops_parse_gpu_id(&kbdev->gpu_props.gpu_id,
						    kbase_reg_get_gpu_id(kbdev));

		product_model = kbdev->gpu_props.gpu_id.product_model;
		if (product_model != GPU_ID_PRODUCT_TGOX && product_model != GPU_ID_PRODUCT_TNOX &&
		    product_model != GPU_ID_PRODUCT_TBAX) {
			kbase_arbiter_pm_early_term(kbdev);
			dev_dbg(kbdev->dev, "GPU platform not suitable for arbitration");
			return -EPERM;
		}
#endif
		dev_info(kbdev->dev, "Arbitration interface enabled");
	}
#endif
	return err;
}

void kbase_device_backend_term(struct kbase_device *kbdev)
{
#ifdef CONFIG_MALI_ARBITER_SUPPORT
	kbase_arbiter_pm_early_term(kbdev);
#endif
}

int power_control_init(struct kbase_device *kbdev)
{
#ifndef CONFIG_OF

	return 0;
#else
	struct platform_device *pdev;
	int err = 0;
	unsigned int i, k;
#if defined(CONFIG_REGULATOR)

	static const char *const regulator_names[] = { "mali", "shadercores", NULL };
	static const char *avail_regulator_names[BASE_MAX_NR_CLOCKS_REGULATORS + 1];

	BUILD_BUG_ON(ARRAY_SIZE(regulator_names) - 1 > BASE_MAX_NR_CLOCKS_REGULATORS);
#endif

	if (!kbdev)
		return -ENODEV;

	pdev = to_platform_device(kbdev->dev);

#if defined(CONFIG_REGULATOR)

	k = 0;
	for (i = 0; i < BASE_MAX_NR_CLOCKS_REGULATORS; i++) {
		const char *id = (i < ARRAY_SIZE(regulator_names)) ? regulator_names[i] : NULL;

		kbdev->regulators[k] = regulator_get_optional(kbdev->dev, id);
		if (IS_ERR(kbdev->regulators[k])) {
			if (err != -EPROBE_DEFER)
				err = PTR_ERR(kbdev->regulators[k]);
			kbdev->regulators[k] = NULL;
		} else {
			avail_regulator_names[k] = regulator_names[i];

			avail_regulator_names[++k] = NULL;
		}
	}
	if (err == -EPROBE_DEFER) {
		while (k > 0)
			regulator_put(kbdev->regulators[--k]);
		return err;
	}

	kbdev->nr_regulators = k;
	dev_dbg(&pdev->dev, "Regulators probed: %u\n", kbdev->nr_regulators);
#endif

	for (i = 0; i < BASE_MAX_NR_CLOCKS_REGULATORS; i++) {
		kbdev->clocks[i] = of_clk_get(kbdev->dev->of_node, (int)i);
		if (IS_ERR(kbdev->clocks[i])) {
			err = PTR_ERR(kbdev->clocks[i]);
			kbdev->clocks[i] = NULL;
			break;
		}

		err = clk_prepare_enable(kbdev->clocks[i]);
		if (err) {
			dev_err(kbdev->dev, "Failed to prepare and enable clock (%d)\n", err);
			clk_put(kbdev->clocks[i]);
			break;
		}
	}

	if (err == -EPROBE_DEFER) {
		while (i > 0) {
			clk_disable_unprepare(kbdev->clocks[--i]);
			clk_put(kbdev->clocks[i]);
		}
		goto clocks_probe_defer;
	}

	kbdev->nr_clocks = i;
	dev_dbg(&pdev->dev, "Clocks probed: %u\n", kbdev->nr_clocks);

#if defined(CONFIG_PM_OPP)
#if defined(CONFIG_REGULATOR)
#if (KERNEL_VERSION(6, 0, 0) <= LINUX_VERSION_CODE)
	if (kbdev->nr_regulators > 0) {
		kbdev->token = dev_pm_opp_set_regulators(kbdev->dev, avail_regulator_names);

		if (kbdev->token < 0) {
			err = kbdev->token;
			goto regulators_probe_defer;
		}
	}
#elif (KERNEL_VERSION(4, 10, 0) <= LINUX_VERSION_CODE)
	if (kbdev->nr_regulators > 0) {
		kbdev->opp_table = dev_pm_opp_set_regulators(kbdev->dev, avail_regulator_names,
							     BASE_MAX_NR_CLOCKS_REGULATORS);

		if (IS_ERR(kbdev->opp_table)) {
			err = PTR_ERR(kbdev->opp_table);
			goto regulators_probe_defer;
		}
	}
#endif
#endif
	err = dev_pm_opp_of_add_table(kbdev->dev);
	CSTD_UNUSED(err);
#endif
	return 0;

#if defined(CONFIG_PM_OPP) && \
	((KERNEL_VERSION(4, 10, 0) <= LINUX_VERSION_CODE) && defined(CONFIG_REGULATOR))
regulators_probe_defer:
	for (i = 0; i < BASE_MAX_NR_CLOCKS_REGULATORS; i++) {
		if (kbdev->clocks[i]) {
			if (__clk_is_enabled(kbdev->clocks[i]))
				clk_disable_unprepare(kbdev->clocks[i]);
			clk_put(kbdev->clocks[i]);
			kbdev->clocks[i] = NULL;
		} else
			break;
	}
#endif

clocks_probe_defer:
#if defined(CONFIG_REGULATOR)
	for (i = 0; i < BASE_MAX_NR_CLOCKS_REGULATORS; i++)
		regulator_put(kbdev->regulators[i]);
#endif
	return err;
#endif
}

void power_control_term(struct kbase_device *kbdev)
{
	unsigned int i;

#if defined(CONFIG_PM_OPP)
	dev_pm_opp_of_remove_table(kbdev->dev);
#if defined(CONFIG_REGULATOR)
#if (KERNEL_VERSION(6, 0, 0) <= LINUX_VERSION_CODE)
	if (kbdev->token > -EPERM)
		dev_pm_opp_put_regulators(kbdev->token);
#elif (KERNEL_VERSION(4, 10, 0) <= LINUX_VERSION_CODE)
	if (!IS_ERR_OR_NULL(kbdev->opp_table))
		dev_pm_opp_put_regulators(kbdev->opp_table);
#endif
#endif
#endif

	for (i = 0; i < BASE_MAX_NR_CLOCKS_REGULATORS; i++) {
		if (kbdev->clocks[i]) {
			if (__clk_is_enabled(kbdev->clocks[i]))
				clk_disable_unprepare(kbdev->clocks[i]);
			clk_put(kbdev->clocks[i]);
			kbdev->clocks[i] = NULL;
		} else
			break;
	}

#if defined(CONFIG_OF) && defined(CONFIG_REGULATOR)
	for (i = 0; i < BASE_MAX_NR_CLOCKS_REGULATORS; i++) {
		if (kbdev->regulators[i]) {
			regulator_put(kbdev->regulators[i]);
			kbdev->regulators[i] = NULL;
		}
	}
#endif
}

#if IS_ENABLED(CONFIG_DEBUG_FS)

static void trigger_reset(struct kbase_device *kbdev)
{
	kbase_pm_context_active(kbdev);
	if (kbase_prepare_to_reset_gpu(kbdev, RESET_FLAGS_NONE))
		kbase_reset_gpu(kbdev);
	kbase_pm_context_idle(kbdev);
}

#define MAKE_QUIRK_ACCESSORS(type)                                                           \
	static int type##_quirks_set(void *data, u64 val)                                    \
	{                                                                                    \
		struct kbase_device *kbdev;                                                  \
		kbdev = (struct kbase_device *)data;                                         \
		kbdev->hw_quirks_##type = (u32)val;                                          \
		trigger_reset(kbdev);                                                        \
		return 0;                                                                    \
	}                                                                                    \
                                                                                             \
	static int type##_quirks_get(void *data, u64 *val)                                   \
	{                                                                                    \
		struct kbase_device *kbdev;                                                  \
		kbdev = (struct kbase_device *)data;                                         \
		*val = kbdev->hw_quirks_##type;                                              \
		return 0;                                                                    \
	}                                                                                    \
	DEFINE_DEBUGFS_ATTRIBUTE(fops_##type##_quirks, type##_quirks_get, type##_quirks_set, \
				 "%llu\n")

MAKE_QUIRK_ACCESSORS(sc);
MAKE_QUIRK_ACCESSORS(tiler);
MAKE_QUIRK_ACCESSORS(mmu);
MAKE_QUIRK_ACCESSORS(gpu);

static int kbase_device_debugfs_reset_write(void *data, u64 wait_for_reset)
{
	struct kbase_device *kbdev = data;

	trigger_reset(kbdev);

	if (wait_for_reset == 1)
		return kbase_reset_gpu_wait(kbdev);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_trigger_reset, NULL, &kbase_device_debugfs_reset_write, "%llu\n");

static ssize_t debugfs_protected_debug_mode_read(struct file *file, char __user *buf, size_t len,
						 loff_t *ppos)
{
	struct kbase_device *kbdev = (struct kbase_device *)file->private_data;
	u32 gpu_status;
	ssize_t ret_val;

	kbase_pm_context_active(kbdev);
	gpu_status = kbase_reg_read32(kbdev, GPU_CONTROL_ENUM(GPU_STATUS));
	kbase_pm_context_idle(kbdev);

	if (gpu_status & GPU_STATUS_GPU_DBG_ENABLED)
		ret_val = simple_read_from_buffer(buf, len, ppos, "1\n", 2);
	else
		ret_val = simple_read_from_buffer(buf, len, ppos, "0\n", 2);

	return ret_val;
}

static const struct file_operations fops_protected_debug_mode = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = debugfs_protected_debug_mode_read,
	.llseek = default_llseek,
};

static int kbase_device_debugfs_mem_pool_max_size_show(struct seq_file *sfile, void *data)
{
	CSTD_UNUSED(data);
	return kbase_debugfs_helper_seq_read(sfile, MEMORY_GROUP_MANAGER_NR_GROUPS,
					     kbase_mem_pool_config_debugfs_max_size);
}

static ssize_t kbase_device_debugfs_mem_pool_max_size_write(struct file *file,
							    const char __user *ubuf, size_t count,
							    loff_t *ppos)
{
	ssize_t err = 0;

	CSTD_UNUSED(ppos);
	err = kbase_debugfs_helper_seq_write(file, ubuf, count, MEMORY_GROUP_MANAGER_NR_GROUPS,
					     kbase_mem_pool_config_debugfs_set_max_size);

	return err ? err : (ssize_t)count;
}

static int kbase_device_debugfs_mem_pool_max_size_open(struct inode *in, struct file *file)
{
	return single_open(file, kbase_device_debugfs_mem_pool_max_size_show, in->i_private);
}

static const struct file_operations kbase_device_debugfs_mem_pool_max_size_fops = {
	.owner = THIS_MODULE,
	.open = kbase_device_debugfs_mem_pool_max_size_open,
	.read = seq_read,
	.write = kbase_device_debugfs_mem_pool_max_size_write,
	.llseek = seq_lseek,
	.release = single_release,
};

#if IS_ENABLED(CONFIG_MALI_MTK_JIT_RECLAIM_ANTITHRASHING)
static int kbase_device_debugfs_jit_reclaim_timeout_ms_show(struct seq_file *sfile,
	void *data)
{
	void *const jit_reclaim_timeout_ms = sfile->private;

	CSTD_UNUSED(data);

	seq_printf(sfile, "%u\n", *(u32 *) jit_reclaim_timeout_ms);

	return 0;
}

static ssize_t kbase_device_debugfs_jit_reclaim_timeout_ms_write(struct file *file,
		const char __user *ubuf, size_t count, loff_t *ppos)
{
	const struct seq_file *const sfile = (struct seq_file *) file->private_data;
	void *const jit_reclaim_timeout_ms = sfile->private;
	unsigned long val = 0;
	int err = 0;

	CSTD_UNUSED(ppos);
	err = kstrtoul_from_user(ubuf, count, 0, &val);
	if (err)
		return err;

	*((u32 *) jit_reclaim_timeout_ms) = val;

	return count;
}

static int kbase_device_debugfs_jit_reclaim_timeout_ms_open(struct inode *in,
	struct file *file)
{
	return single_open(file, kbase_device_debugfs_jit_reclaim_timeout_ms_show,
		in->i_private);
}

static const struct file_operations
	kbase_device_debugfs_jit_reclaim_timeout_ms_fops = {
	.owner = THIS_MODULE,
	.open = kbase_device_debugfs_jit_reclaim_timeout_ms_open,
	.read = seq_read,
	.write = kbase_device_debugfs_jit_reclaim_timeout_ms_write,
	.llseek = seq_lseek,
	.release = single_release,
};
#endif

static struct dentry *debugfs_ctx_defaults_init(struct kbase_device *const kbdev)
{

	const mode_t mode = 0644;
	struct dentry *dentry = debugfs_create_dir("defaults", kbdev->debugfs_ctx_directory);
	struct dentry *debugfs_ctx_defaults_directory = dentry;

	if (IS_ERR_OR_NULL(dentry)) {
		dev_err(kbdev->dev, "Couldn't create mali debugfs ctx defaults directory\n");
		return dentry;
	}

	debugfs_create_bool("infinite_cache", mode, debugfs_ctx_defaults_directory,
			    &kbdev->infinite_cache_active_default);

	dentry = debugfs_create_file("mem_pool_max_size", mode, debugfs_ctx_defaults_directory,
				     &kbdev->mem_pool_defaults.small,
				     &kbase_device_debugfs_mem_pool_max_size_fops);
	if (IS_ERR_OR_NULL(dentry)) {
		dev_err(kbdev->dev, "Unable to create mem_pool_max_size debugfs entry\n");
		return dentry;
	}

	dentry = debugfs_create_file("lp_mem_pool_max_size", mode, debugfs_ctx_defaults_directory,
				     &kbdev->mem_pool_defaults.large,
				     &kbase_device_debugfs_mem_pool_max_size_fops);
	if (IS_ERR_OR_NULL(dentry))
		dev_err(kbdev->dev, "Unable to create lp_mem_pool_max_size debugfs entry\n");

	return dentry;
}

#if IS_ENABLED(CONFIG_MALI_MTK_WHITEBOX_MISSING_DOORBELL)
void kbase_csf_db_valid_test_debugfs_init(struct kbase_device *kbdev);
#endif

static struct dentry *init_debugfs(struct kbase_device *kbdev)
{
	struct dentry *dentry = debugfs_create_dir(kbdev->devname, NULL);

	kbdev->mali_debugfs_directory = dentry;
	if (IS_ERR_OR_NULL(dentry)) {
		dev_err(kbdev->dev, "Couldn't create mali debugfs directory: %s\n", kbdev->devname);
		return dentry;
	}

	dentry = debugfs_create_dir("ctx", kbdev->mali_debugfs_directory);
	kbdev->debugfs_ctx_directory = dentry;
	if (IS_ERR_OR_NULL(dentry)) {
		dev_err(kbdev->dev, "Couldn't create mali debugfs ctx directory\n");
		return dentry;
	}

	dentry = debugfs_create_dir("instrumentation", kbdev->mali_debugfs_directory);
	kbdev->debugfs_instr_directory = dentry;
	if (IS_ERR_OR_NULL(dentry)) {
		dev_err(kbdev->dev, "Couldn't create mali debugfs instrumentation directory\n");
		return dentry;
	}

	kbasep_regs_history_debugfs_init(kbdev);

#if MALI_USE_CSF
	kbase_debug_csf_fault_debugfs_init(kbdev);
#else
	kbase_debug_job_fault_debugfs_init(kbdev);
#endif

	kbasep_gpu_memory_debugfs_init(kbdev);
	kbase_as_fault_debugfs_init(kbdev);
#ifdef CONFIG_MALI_PRFCNT_SET_SELECT_VIA_DEBUG_FS
	kbase_instr_backend_debugfs_init(kbdev);
#endif
	kbase_pbha_debugfs_init(kbdev);

	dentry = debugfs_create_file("quirks_sc", 0644, kbdev->mali_debugfs_directory, kbdev,
				     &fops_sc_quirks);
	if (IS_ERR_OR_NULL(dentry)) {
		dev_err(kbdev->dev, "Unable to create quirks_sc debugfs entry\n");
		return dentry;
	}

	dentry = debugfs_create_file("quirks_tiler", 0644, kbdev->mali_debugfs_directory, kbdev,
				     &fops_tiler_quirks);
	if (IS_ERR_OR_NULL(dentry)) {
		dev_err(kbdev->dev, "Unable to create quirks_tiler debugfs entry\n");
		return dentry;
	}

	dentry = debugfs_create_file("quirks_mmu", 0644, kbdev->mali_debugfs_directory, kbdev,
				     &fops_mmu_quirks);
	if (IS_ERR_OR_NULL(dentry)) {
		dev_err(kbdev->dev, "Unable to create quirks_mmu debugfs entry\n");
		return dentry;
	}

	dentry = debugfs_create_file("quirks_gpu", 0644, kbdev->mali_debugfs_directory, kbdev,
				     &fops_gpu_quirks);
	if (IS_ERR_OR_NULL(dentry)) {
		dev_err(kbdev->dev, "Unable to create quirks_gpu debugfs entry\n");
		return dentry;
	}

	dentry = debugfs_ctx_defaults_init(kbdev);
	if (IS_ERR_OR_NULL(dentry))
		return dentry;

	if (kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_PROTECTED_DEBUG_MODE)) {
		dentry = debugfs_create_file("protected_debug_mode", 0444,
					     kbdev->mali_debugfs_directory, kbdev,
					     &fops_protected_debug_mode);
		if (IS_ERR_OR_NULL(dentry)) {
			dev_err(kbdev->dev,
				"Unable to create protected_debug_mode debugfs entry\n");
			return dentry;
		}
	}

	dentry = debugfs_create_file("reset", 0644, kbdev->mali_debugfs_directory, kbdev,
				     &fops_trigger_reset);
	if (IS_ERR_OR_NULL(dentry)) {
		dev_err(kbdev->dev, "Unable to create reset debugfs entry\n");
		return dentry;
	}

#if IS_ENABLED(CONFIG_MALI_MTK_JIT_RECLAIM_ANTITHRASHING)
	debugfs_create_file("jit_reclaim_timeout_ms", 0644,
			kbdev->mali_debugfs_directory,
			&kbdev->jit_reclaim_timeout_ms,
			&kbase_device_debugfs_jit_reclaim_timeout_ms_fops);
#endif

#if IS_ENABLED(CONFIG_MALI_MTK_MMU_DUMP)
	dentry = debugfs_create_file("force_dump_mmu", 0644, kbdev->mali_debugfs_directory, kbdev,
				     &fops_force_dump_mmu);
	if (IS_ERR_OR_NULL(dentry)) {
		dev_err(kbdev->dev, "Unable to create reset debugfs entry (force_dump_mmu) \n");
		return dentry;
	}
#endif

	kbase_ktrace_debugfs_init(kbdev);

#ifdef CONFIG_MALI_DEVFREQ
#if IS_ENABLED(CONFIG_DEVFREQ_THERMAL)
#if !IS_ENABLED(CONFIG_MALI_MTK_DEVFREQ_THERMAL)
	if (kbdev->devfreq)
		kbase_ipa_debugfs_init(kbdev);
#endif
#endif
#endif

#if IS_ENABLED(CONFIG_MALI_MTK_GPU_IDLE_STRESS_TEST)
	mtk_debug_gpu_idle_test_debugfs_init(kbdev);
#endif

#if !MALI_USE_CSF
	dentry = debugfs_create_file("serialize_jobs", 0644, kbdev->mali_debugfs_directory, kbdev,
				     &kbasep_serialize_jobs_debugfs_fops);
	if (IS_ERR_OR_NULL(dentry)) {
		dev_err(kbdev->dev, "Unable to create serialize_jobs debugfs entry\n");
		return dentry;
	}
	kbase_timeline_io_debugfs_init(kbdev);
#endif
	kbase_dvfs_status_debugfs_init(kbdev);

#if IS_ENABLED(CONFIG_MALI_MTK_DEBUG_FS)
	mtk_common_debugfs_init(kbdev);
#endif

	return dentry;
}

int kbase_device_debugfs_init(struct kbase_device *kbdev)
{
	struct dentry *dentry = init_debugfs(kbdev);

	if (IS_ERR_OR_NULL(dentry)) {
		debugfs_remove_recursive(kbdev->mali_debugfs_directory);
		return IS_ERR(dentry) ? PTR_ERR(dentry) : -ENOMEM;
	}
	return 0;
}

void kbase_device_debugfs_term(struct kbase_device *kbdev)
{
	debugfs_remove_recursive(kbdev->mali_debugfs_directory);
}
#endif

static u32 kbase_device_normalize_coherency_bitmap(struct kbase_device *kbdev)
{
	u32 supported_coherency_bitmap = kbdev->gpu_props.coherency_mode;

	if ((kbdev->gpu_props.gpu_id.product_model == GPU_ID_PRODUCT_TMIX) &&
	    (supported_coherency_bitmap == COHERENCY_FEATURE_BIT(COHERENCY_ACE)))
		supported_coherency_bitmap |= COHERENCY_FEATURE_BIT(COHERENCY_ACE_LITE);

	return supported_coherency_bitmap;
}

static bool kbase_device_supports_coherency_mode(struct kbase_device *kbdev, u32 coherency_mode)
{
	u32 supported_coherency_bitmap = kbase_device_normalize_coherency_bitmap(kbdev);

	if (coherency_mode > COHERENCY_NONE) {
		dev_warn(kbdev->dev, "Ignoring unsupported coherency mode %u set from dtb",
			 coherency_mode);
		return false;
	}

	if (coherency_mode == COHERENCY_ACE) {
		if (IS_ENABLED(MALI_USE_CSF) && !IS_ENABLED(CONFIG_MALI_NO_MALI)) {
			dev_err(kbdev->dev,
				"ACE coherency not supported on CSF, wrong DT configuration");
			return false;
		}
	}

	if (!(supported_coherency_bitmap & COHERENCY_FEATURE_BIT(coherency_mode))) {
		dev_warn(kbdev->dev, "Device does not support coherency mode %u set from dtb",
			 coherency_mode);
		return false;
	}

	return true;
}

int kbase_device_coherency_init(struct kbase_device *kbdev)
{
	int err = 0;

#if IS_ENABLED(CONFIG_MALI_MTK_ACP_FORCE_SYNC_DEBUG)
	kbdev->acp_dbg_force_sync = FORCE_SYNC_NONE;
	if (IS_ENABLED(CONFIG_OF)) {
		u32 override_dbg_force_sync;
		const void *dbg_force_sync_override_dts;
		dbg_force_sync_override_dts =
			of_get_property(kbdev->dev->of_node, "dbg-force-sync", NULL);

		if (dbg_force_sync_override_dts)
			override_dbg_force_sync = be32_to_cpup(dbg_force_sync_override_dts);
		else
			override_dbg_force_sync = FORCE_SYNC_NONE;

		if (override_dbg_force_sync == FORCE_SYNC_DTS)
			kbdev->acp_dbg_force_sync = override_dbg_force_sync;
	}
#endif

	kbdev->system_coherency = COHERENCY_NONE;

	if (IS_ENABLED(CONFIG_OF)) {
		u32 override_coherency;
		const void *coherency_override_dts;
		bool dma_coherent;

		dma_coherent = of_dma_is_coherent(kbdev->dev->of_node);
		coherency_override_dts =
			of_get_property(kbdev->dev->of_node, "system-coherency", NULL);

		if (!coherency_override_dts && !dma_coherent)
			goto early_exit;

		if (coherency_override_dts)
			override_coherency = be32_to_cpup(coherency_override_dts);
		else
			override_coherency = COHERENCY_ACE_LITE;

		if (dma_coherent && override_coherency != COHERENCY_ACE_LITE) {
			dev_err(kbdev->dev,
				"system-coherency needs to be 0 when dma-coherent is set!");
			err = -EINVAL;
			goto early_exit;
		}

		if (!kbase_device_supports_coherency_mode(kbdev, override_coherency)) {
			err = -EINVAL;
			goto early_exit;
		}

		kbdev->system_coherency = override_coherency;

		dev_info(kbdev->dev, "Using coherency mode %u set from dtb", override_coherency);
	}
early_exit:
	kbdev->gpu_props.coherency_mode = kbdev->system_coherency;

	return err;
}

#if MALI_USE_CSF

bool kbasep_adjust_prioritized_process(struct kbase_device *kbdev, bool add, uint32_t tgid)
{
	struct kbase_context *kctx;
	bool found_contexts = false;

	mutex_lock(&kbdev->kctx_list_lock);
	list_for_each_entry(kctx, &kbdev->kctx_list, kctx_list_link) {
		if (kctx->tgid == tgid) {
			if (add)
				dev_dbg(kbdev->dev,
					"Adding context %pK of process %u to prioritized list\n",
					(void *)kctx, tgid);
			else
				dev_dbg(kbdev->dev,
					"Removing context %pK of process %u from prioritized list\n",
					(void *)kctx, tgid);
			atomic_set(&kctx->prioritized, add);
			found_contexts = true;
		}
	}
	mutex_unlock(&kbdev->kctx_list_lock);

	if (found_contexts)
		kbase_csf_scheduler_kick(kbdev);

	return found_contexts;
}

static ssize_t add_prioritized_process_store(struct device *dev, struct device_attribute *attr,
					     const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	int ret;
	unsigned int tgid;

	CSTD_UNUSED(attr);

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	ret = kstrtouint(buf, 0, &tgid);
	if (ret || tgid == 0) {
		dev_err(kbdev->dev, "Invalid PID specified\n");
		return -EINVAL;
	}

	if (unlikely(!kbasep_adjust_prioritized_process(kbdev, true, tgid))) {
		dev_err(kbdev->dev, "Non-existent PID specified\n");
		return -EINVAL;
	}

	return count;
}

static DEVICE_ATTR_WO(add_prioritized_process);

static ssize_t remove_prioritized_process_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	int ret;
	unsigned int tgid;

	CSTD_UNUSED(attr);

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	ret = kstrtouint(buf, 0, &tgid);
	if (ret || tgid == 0) {
		dev_err(kbdev->dev, "Invalid PID specified\n");
		return -EINVAL;
	}

	if (unlikely(!kbasep_adjust_prioritized_process(kbdev, false, tgid))) {
		dev_err(kbdev->dev, "Non-existent PID specified\n");
		return -EINVAL;
	}

	return count;
}

static DEVICE_ATTR_WO(remove_prioritized_process);

static ssize_t csg_scheduling_period_store(struct device *dev, struct device_attribute *attr,
					   const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	int ret;
	unsigned int csg_scheduling_period;

	CSTD_UNUSED(attr);

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	ret = kstrtouint(buf, 0, &csg_scheduling_period);
	if (ret || csg_scheduling_period == 0) {
		dev_err(kbdev->dev,
			"Couldn't process csg_scheduling_period write operation.\n"
			"Use format 'csg_scheduling_period_ms', and csg_scheduling_period_ms > 0\n");
		return -EINVAL;
	}

	kbase_csf_scheduler_lock(kbdev);
	kbdev->csf.scheduler.csg_scheduling_period_ms = csg_scheduling_period;
	dev_dbg(kbdev->dev, "CSG scheduling period: %ums\n", csg_scheduling_period);
	kbase_csf_scheduler_unlock(kbdev);

	return (ssize_t)count;
}

static ssize_t csg_scheduling_period_show(struct device *dev, struct device_attribute *attr,
					  char *const buf)
{
	struct kbase_device *kbdev;
	ssize_t ret;

	CSTD_UNUSED(attr);

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	ret = scnprintf(buf, PAGE_SIZE, "%u\n", kbdev->csf.scheduler.csg_scheduling_period_ms);

	return ret;
}

static DEVICE_ATTR_RW(csg_scheduling_period);

static ssize_t fw_timeout_store(struct device *dev, struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct kbase_device *kbdev;
	int ret;
	unsigned int fw_timeout;

	CSTD_UNUSED(attr);

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	ret = kstrtouint(buf, 0, &fw_timeout);
	if (ret || fw_timeout == 0) {
		dev_err(kbdev->dev,
			"Couldn't process fw_timeout write operation.\n"
			"Use format 'fw_timeout_ms', and fw_timeout_ms > 0\n"
			"Default fw_timeout: %u",
			kbase_get_timeout_ms(kbdev, CSF_FIRMWARE_TIMEOUT));
		return -EINVAL;
	}

	kbase_csf_scheduler_lock(kbdev);
	kbase_device_set_timeout_ms(kbdev, CSF_FIRMWARE_TIMEOUT, fw_timeout);
	kbase_csf_scheduler_unlock(kbdev);
	dev_dbg(kbdev->dev, "Firmware timeout: %ums\n", fw_timeout);

	return (ssize_t)count;
}

static ssize_t fw_timeout_show(struct device *dev, struct device_attribute *attr, char *const buf)
{
	struct kbase_device *kbdev;
	ssize_t ret;

	CSTD_UNUSED(attr);

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	ret = scnprintf(buf, PAGE_SIZE, "%u\n", kbase_get_timeout_ms(kbdev, CSF_FIRMWARE_TIMEOUT));

	return ret;
}

static DEVICE_ATTR_RW(fw_timeout);

static ssize_t idle_hysteresis_time_store(struct device *dev, struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	u32 dur_us = 0;

	CSTD_UNUSED(attr);

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	if (kstrtou32(buf, 0, &dur_us)) {
		dev_err(kbdev->dev, "Couldn't process idle_hysteresis_time write operation.\n"
				    "Use format <idle_hysteresis_time>\n");
		return -EINVAL;
	}

	kbase_csf_firmware_set_gpu_idle_hysteresis_time(kbdev, (u64)dur_us * NSEC_PER_USEC);

	return (ssize_t)count;
}

static ssize_t idle_hysteresis_time_show(struct device *dev, struct device_attribute *attr,
					 char *const buf)
{
	struct kbase_device *kbdev;
	ssize_t ret;
	u64 dur_us;

	CSTD_UNUSED(attr);

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	dur_us = div_u64(kbase_csf_firmware_get_gpu_idle_hysteresis_time(kbdev), NSEC_PER_USEC);
	ret = scnprintf(buf, PAGE_SIZE, "%u\n", (u32)dur_us);

	return ret;
}

static DEVICE_ATTR_RW(idle_hysteresis_time);

#if IS_ENABLED(CONFIG_MALI_MTK_SOI_DEBUG)

static ssize_t sleep_on_idle_enable_store(struct device *dev, struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	u32 enabled = 0;

	CSTD_UNUSED(attr);

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	if (kstrtou32(buf, 0, &enabled)) {
		dev_err(kbdev->dev, "Couldn't process sleep_on_idle_enable write operation.\n"
				    "Use format <sleep_on_idle_enable>\n");
		return -EINVAL;
	}

	kbase_csf_firmware_set_sleep_on_idle(kbdev,enabled);

	return (ssize_t)count;
}

static ssize_t sleep_on_idle_enable_show(struct device *dev, struct device_attribute *attr,
					 char *const buf)
{
	struct kbase_device *kbdev;
	ssize_t ret;
	u64 enabled;

	CSTD_UNUSED(attr);

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	enabled = kbase_csf_firmware_get_sleep_on_idle(kbdev);
	ret = scnprintf(buf, PAGE_SIZE, "sleep on idle enable = %u\n", (u32)enabled);

	return ret;
}

static DEVICE_ATTR_RW(sleep_on_idle_enable);

#endif

static ssize_t idle_hysteresis_time_ns_store(struct device *dev, struct device_attribute *attr,
					     const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	u64 dur_ns = 0;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	if (kstrtou64(buf, 0, &dur_ns)) {
		dev_err(kbdev->dev, "Couldn't process idle_hysteresis_time_ns write operation.\n"
				    "Use format <idle_hysteresis_time_ns>\n");
		return -EINVAL;
	}

	kbase_csf_firmware_set_gpu_idle_hysteresis_time(kbdev, dur_ns);

	return count;
}

static ssize_t idle_hysteresis_time_ns_show(struct device *dev, struct device_attribute *attr,
					    char *const buf)
{
	struct kbase_device *kbdev;
	ssize_t ret;
	u64 dur_ns;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	dur_ns = kbase_csf_firmware_get_gpu_idle_hysteresis_time(kbdev);
	ret = scnprintf(buf, PAGE_SIZE, "%llu\n", dur_ns);

	return ret;
}

static DEVICE_ATTR_RW(idle_hysteresis_time_ns);

#if IS_ENABLED(CONFIG_MALI_MTK_GOV_CORE_MASK_DEBUG)

static ssize_t gov_core_mask_disable_store(struct device *dev, struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	u32 disabled = 0;

	CSTD_UNUSED(attr);

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	if (kstrtou32(buf, 0, &disabled)) {
		dev_err(kbdev->dev, "Couldn't process gov_core_mask_enable write operation.\n"
				    "Use format <gov_core_mask_enable>\n");
		return -EINVAL;
	}

	kbdev->gov_core_mask_disable = disabled;
	dev_dbg(kbdev->dev, "gov_core_mask_disable: %d\n",disabled);

	return (ssize_t)count;
}

static ssize_t gov_core_mask_disable_show(struct device *dev, struct device_attribute *attr,
					 char *const buf)
{
	struct kbase_device *kbdev;
	ssize_t ret;
	u32 disabled;

	CSTD_UNUSED(attr);

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	disabled = kbdev->gov_core_mask_disable;
	ret = scnprintf(buf, PAGE_SIZE, "gov_core_mask disable = %u\n", (u32)disabled);

	return ret;
}
static DEVICE_ATTR_RW(gov_core_mask_disable);

#endif

static ssize_t mcu_shader_pwroff_timeout_show(struct device *dev, struct device_attribute *attr,
					      char *const buf)
{
	struct kbase_device *kbdev = dev_get_drvdata(dev);
	u64 pwroff_us;

	CSTD_UNUSED(attr);

	if (!kbdev)
		return -ENODEV;

	pwroff_us = div_u64(kbase_csf_firmware_get_mcu_core_pwroff_time(kbdev), NSEC_PER_USEC);
	return scnprintf(buf, PAGE_SIZE, "%u\n", (u32)pwroff_us);
}

static ssize_t mcu_shader_pwroff_timeout_store(struct device *dev, struct device_attribute *attr,
					       const char *buf, size_t count)
{
	struct kbase_device *kbdev = dev_get_drvdata(dev);
	u32 dur_us;

	const struct kbase_pm_policy *current_policy;
	bool always_on;

	CSTD_UNUSED(attr);

	if (!kbdev)
		return -ENODEV;

	if (kstrtou32(buf, 0, &dur_us))
		return -EINVAL;

	current_policy = kbase_pm_get_policy(kbdev);
	always_on = current_policy == &kbase_pm_always_on_policy_ops;
	if (dur_us == 0 && !always_on)
		return -EINVAL;

	kbase_csf_firmware_set_mcu_core_pwroff_time(kbdev, (u64)dur_us * NSEC_PER_USEC);

	return (ssize_t)count;
}

static DEVICE_ATTR_RW(mcu_shader_pwroff_timeout);

static ssize_t mcu_shader_pwroff_timeout_ns_show(struct device *dev, struct device_attribute *attr,
						 char *const buf)
{
	struct kbase_device *kbdev = dev_get_drvdata(dev);
	u64 pwroff_ns;

	if (!kbdev)
		return -ENODEV;

	pwroff_ns = kbase_csf_firmware_get_mcu_core_pwroff_time(kbdev);
	return scnprintf(buf, PAGE_SIZE, "%llu\n", pwroff_ns);
}

static ssize_t mcu_shader_pwroff_timeout_ns_store(struct device *dev, struct device_attribute *attr,
						  const char *buf, size_t count)
{
	struct kbase_device *kbdev = dev_get_drvdata(dev);
	u64 dur_ns;

	const struct kbase_pm_policy *current_policy;
	bool always_on;

	if (!kbdev)
		return -ENODEV;

	if (kstrtou64(buf, 0, &dur_ns))
		return -EINVAL;

	current_policy = kbase_pm_get_policy(kbdev);
	always_on = current_policy == &kbase_pm_always_on_policy_ops;
	if (dur_ns == 0 && !always_on)
		return -EINVAL;

	kbase_csf_firmware_set_mcu_core_pwroff_time(kbdev, dur_ns);

	return count;
}

static DEVICE_ATTR_RW(mcu_shader_pwroff_timeout_ns);

#endif

static struct attribute *kbase_scheduling_attrs[] = {
#if !MALI_USE_CSF
	&dev_attr_serialize_jobs.attr,
#endif
	NULL
};

static struct attribute *kbase_attrs[] = {
#ifdef CONFIG_MALI_DEBUG
	&dev_attr_debug_command.attr,
#if !MALI_USE_CSF
	&dev_attr_js_softstop_always.attr,
#endif
#endif
#if !MALI_USE_CSF
	&dev_attr_js_timeouts.attr,
	&dev_attr_soft_job_timeout.attr,
#endif
	&dev_attr_gpuinfo.attr,
	&dev_attr_dvfs_period.attr,
	&dev_attr_pm_poweroff.attr,
	&dev_attr_reset_timeout.attr,
#if !MALI_USE_CSF
	&dev_attr_js_scheduling_period.attr,
#else
	&dev_attr_csg_scheduling_period.attr,
	&dev_attr_add_prioritized_process.attr,
	&dev_attr_remove_prioritized_process.attr,
	&dev_attr_fw_timeout.attr,
	&dev_attr_idle_hysteresis_time.attr,
	&dev_attr_idle_hysteresis_time_ns.attr,
#if IS_ENABLED(CONFIG_MALI_MTK_SOI_DEBUG)
	&dev_attr_sleep_on_idle_enable.attr,
#endif
#if IS_ENABLED(CONFIG_MALI_MTK_GOV_CORE_MASK_DEBUG)
	&dev_attr_gov_core_mask_disable.attr,
#endif
	&dev_attr_mcu_shader_pwroff_timeout.attr,
	&dev_attr_mcu_shader_pwroff_timeout_ns.attr,
#endif
	&dev_attr_power_policy.attr,
	&dev_attr_core_mask.attr,
#if IS_ENABLED(CONFIG_MALI_MTK_PAGE_FAULT_WB_TILER_RECLAIM)
	&dev_attr_force_reclaim.attr,
#endif
#if IS_ENABLED(CONFIG_MALI_MTK_KBASE_MMU_DBG_LOG)
	&dev_attr_mmu_dbg_config.attr,
#endif
	&dev_attr_mem_pool_size.attr,
	&dev_attr_mem_pool_max_size.attr,
	&dev_attr_lp_mem_pool_size.attr,
	&dev_attr_lp_mem_pool_max_size.attr,
#if !MALI_USE_CSF
	&dev_attr_js_ctx_scheduling_mode.attr,
#endif
#if IS_ENABLED(CONFIG_MALI_MIDGARD_DVFS) && \
	IS_ENABLED(CONFIG_MALI_MTK_DVFS_POLICY) && \
	IS_ENABLED(CONFIG_MALI_MTK_GPU_DVFS_HINT_26M_LOADING)
	&dev_attr_dvfs_hint_26m_perf_cnting.attr,
	&dev_attr_ipa_enable.attr,
#endif
#if IS_ENABLED(CONFIG_MALI_MTK_UNHANDLED_PAGE_FAULT_DEBUG)
	&dev_attr_upf_counter.attr,
#endif
#if IS_ENABLED(CONFIG_MALI_MTK_ACP_FORCE_SYNC_DEBUG)
	&dev_attr_force_cache_sync.attr,
#endif
	NULL
};

static struct attribute *kbase_mempool_attrs[] = { &dev_attr_max_size.attr,
						   &dev_attr_lp_max_size.attr,
						   &dev_attr_ctx_default_max_size.attr, NULL };

#define SYSFS_SCHEDULING_GROUP "scheduling"
static const struct attribute_group kbase_scheduling_attr_group = {
	.name = SYSFS_SCHEDULING_GROUP,
	.attrs = kbase_scheduling_attrs,
};

#define SYSFS_MEMPOOL_GROUP "mempool"
static const struct attribute_group kbase_mempool_attr_group = {
	.name = SYSFS_MEMPOOL_GROUP,
	.attrs = kbase_mempool_attrs,
};

static const struct attribute_group kbase_attr_group = {
	.attrs = kbase_attrs,
};

int kbase_sysfs_init(struct kbase_device *kbdev)
{
	int err = 0;

	kbdev->mdev.minor = MISC_DYNAMIC_MINOR;
	kbdev->mdev.name = kbdev->devname;
	kbdev->mdev.fops = &kbase_fops;
	kbdev->mdev.parent = get_device(kbdev->dev);
	kbdev->mdev.mode = 0666;

	err = sysfs_create_group(&kbdev->dev->kobj, &kbase_attr_group);
	if (err)
		return err;

	err = sysfs_create_group(&kbdev->dev->kobj, &kbase_scheduling_attr_group);
	if (err) {
		dev_err(kbdev->dev, "Creation of %s sysfs group failed", SYSFS_SCHEDULING_GROUP);
		sysfs_remove_group(&kbdev->dev->kobj, &kbase_attr_group);
		return err;
	}

	err = sysfs_create_group(&kbdev->dev->kobj, &kbase_mempool_attr_group);
	if (err) {
		dev_err(kbdev->dev, "Creation of %s sysfs group failed", SYSFS_MEMPOOL_GROUP);
		sysfs_remove_group(&kbdev->dev->kobj, &kbase_scheduling_attr_group);
		sysfs_remove_group(&kbdev->dev->kobj, &kbase_attr_group);
	}

#if IS_ENABLED(CONFIG_MALI_MTK_SYSFS)
	mtk_common_sysfs_init(kbdev);
#endif

	return err;
}

void kbase_sysfs_term(struct kbase_device *kbdev)
{
#if IS_ENABLED(CONFIG_MALI_MTK_SYSFS)
	mtk_common_sysfs_term(kbdev);
#endif

	sysfs_remove_group(&kbdev->dev->kobj, &kbase_mempool_attr_group);
	sysfs_remove_group(&kbdev->dev->kobj, &kbase_scheduling_attr_group);
	sysfs_remove_group(&kbdev->dev->kobj, &kbase_attr_group);
	put_device(kbdev->dev);
}

#if (KERNEL_VERSION(6, 11, 0) > LINUX_VERSION_CODE)
static int kbase_platform_device_remove(struct platform_device *pdev)
#else
static void kbase_platform_device_remove(struct platform_device *pdev)
#endif
{
	struct kbase_device *kbdev = to_kbase_device(&pdev->dev);

	if (!kbdev)
#if (KERNEL_VERSION(6, 11, 0) > LINUX_VERSION_CODE)
		return -ENODEV;
#else
		return;
#endif

	kbase_device_term(kbdev);
	dev_set_drvdata(kbdev->dev, NULL);
	kbase_device_free(kbdev);

#if (KERNEL_VERSION(6, 11, 0) > LINUX_VERSION_CODE)
	return 0;
#endif
}

void kbase_backend_devfreq_term(struct kbase_device *kbdev)
{
#ifdef CONFIG_MALI_DEVFREQ
	if (kbdev->devfreq)
		kbase_devfreq_term(kbdev);
#endif
}

int kbase_backend_devfreq_init(struct kbase_device *kbdev)
{
#ifdef CONFIG_MALI_DEVFREQ

	int err = kbase_devfreq_init(kbdev);

	if (err)
		dev_err(kbdev->dev, "Continuing without devfreq\n");
#endif
	return 0;
}

static int kbase_platform_device_probe(struct platform_device *pdev)
{
	struct kbase_device *kbdev;
	int err = 0;
	dev_err(&pdev->dev, "kbase_platform_device_probe [start] \n");

	mali_kbase_print_cs_experimental();

	kbdev = kbase_device_alloc();
	if (!kbdev) {
		dev_err(&pdev->dev, "Allocate device failed\n");
		return -ENOMEM;
	}

	kbdev->dev = &pdev->dev;

#if IS_ENABLED(CONFIG_REGULATOR)
#if (KERNEL_VERSION(6, 0, 0) <= LINUX_VERSION_CODE)
	kbdev->token = -EPERM;
#endif
#endif

	dev_set_drvdata(kbdev->dev, kbdev);
#if (KERNEL_VERSION(5, 3, 0) <= LINUX_VERSION_CODE)
	mutex_lock(&kbase_probe_mutex);
#endif
	err = kbase_device_init(kbdev);

	if (err) {
		if (err == -EPROBE_DEFER)
			dev_info(kbdev->dev, "Device initialization Deferred\n");
		else
			dev_err(kbdev->dev, "Device initialization failed\n");

		dev_set_drvdata(kbdev->dev, NULL);
		kbase_device_free(kbdev);
#if (KERNEL_VERSION(5, 3, 0) <= LINUX_VERSION_CODE)
		mutex_unlock(&kbase_probe_mutex);
#endif
	} else {

#if IS_ENABLED(CONFIG_MALI_MTK_GPU_BM_2)
		err = mtk_bandwith_resource_init(kbdev);
		if (err)
			pr_info("@%s: GPU BM init failed (JM)\n", __func__);
#endif

#if defined(CONFIG_MALI_MTK_GPU_BM_CSF)
		err = mtk_bandwidth_resource_init();
		if (err)
			pr_info("@%s: GPU BM init failed (CSF)\n", __func__);
#endif

#if (KERNEL_VERSION(6, 1, 0) <= LINUX_VERSION_CODE)

		dev_info(kbdev->dev, KBASE_COMPILED_MMAP_MIN_ADDR_MSG);
#endif

		dev_info(kbdev->dev, "Probed as %s\n", dev_name(kbdev->mdev.this_device));
#if IS_ENABLED(CONFIG_MALI_MTK_LOG_BUFFER)
		mtk_logbuffer_type_print(kbdev, MTK_LOGBUFFER_TYPE_EXCEPTION,
			"Probed as %s\n", dev_name(kbdev->mdev.this_device));
#endif
		if (PAGE_SHIFT != 12)
			dev_warn(kbdev->dev, "Experimental feature: %s with Page Size of %luKiB",
				 dev_name(kbdev->mdev.this_device), PAGE_SIZE / 1024);

		kbase_increment_device_id();
#if (KERNEL_VERSION(5, 3, 0) <= LINUX_VERSION_CODE)
		mutex_unlock(&kbase_probe_mutex);
#endif
#ifdef CONFIG_MALI_ARBITER_SUPPORT
		mutex_lock(&kbdev->pm.lock);
		kbase_arbiter_pm_vm_event(kbdev, KBASE_VM_GPU_INITIALIZED_EVT);
		mutex_unlock(&kbdev->pm.lock);
#endif
	}
	dev_err(kbdev->dev, "kbase_platform_device_probe [end] err = %d \n", err);

	return err;
}

#undef KBASEP_DEFAULT_REGISTER_HISTORY_SIZE

static int kbase_device_suspend(struct device *dev)
{
	struct kbase_device *kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

	if (kbase_pm_suspend(kbdev)) {
		dev_warn(kbdev->dev, "Abort suspend as GPU suspension failed");
		return -EBUSY;
	}

#ifdef CONFIG_MALI_MIDGARD_DVFS
	kbase_pm_metrics_stop(kbdev);
#endif

#ifdef CONFIG_MALI_DEVFREQ
	dev_dbg(dev, "Callback %s\n", __func__);
	if (kbdev->devfreq) {
		kbase_devfreq_enqueue_work(kbdev, DEVFREQ_WORK_SUSPEND);
		flush_workqueue(kbdev->devfreq_queue.workq);
	}
#endif
#if IS_ENABLED(CONFIG_MALI_MTK_SLC_ALL_CACHE_MODE)
	slbc_invalidate(ID_GPU, slbc_gid_val(ID_GPU));
	slbc_gid_release(ID_GPU, slbc_gid_val(ID_GPU));
#endif

	return 0;
}

static int kbase_device_resume(struct device *dev)
{
	struct kbase_device *kbdev = to_kbase_device(dev);
#if IS_ENABLED(CONFIG_MALI_MTK_SLC_ALL_CACHE_MODE)
	int gid = slbc_gid_val(ID_GPU);
	struct slbc_gid_data slbc_data = {0x51ca11ca,0,0,0,0,0,0,0,0};
#endif

	if (!kbdev)
		return -ENODEV;

	kbase_pm_resume(kbdev);

#ifdef CONFIG_MALI_MIDGARD_DVFS
	kbase_pm_metrics_start(kbdev);
#endif

#ifdef CONFIG_MALI_DEVFREQ
	dev_dbg(dev, "Callback %s\n", __func__);
	if (kbdev->devfreq)
		kbase_devfreq_enqueue_work(kbdev, DEVFREQ_WORK_RESUME);
#endif
#if IS_ENABLED(CONFIG_MALI_MTK_SLC_ALL_CACHE_MODE)
	slbc_gid_request(ID_GPU, &gid, &slbc_data);
	slbc_validate(ID_GPU, slbc_gid_val(ID_GPU));
#endif
	return 0;
}

#ifdef KBASE_PM_RUNTIME
static int kbase_device_runtime_suspend(struct device *dev)
{
	struct kbase_device *kbdev = to_kbase_device(dev);
	int ret = 0;

	if (!kbdev)
		return -ENODEV;

	dev_dbg(dev, "Callback %s\n", __func__);
	KBASE_KTRACE_ADD(kbdev, PM_RUNTIME_SUSPEND_CALLBACK, NULL, 0);

#if MALI_USE_CSF
	ret = kbase_pm_handle_runtime_suspend(kbdev);
	if (ret)
		return ret;
#endif

#ifdef CONFIG_MALI_MIDGARD_DVFS
	kbase_pm_metrics_stop(kbdev);
#endif

#ifdef CONFIG_MALI_DEVFREQ
	if (kbdev->devfreq)
		kbase_devfreq_enqueue_work(kbdev, DEVFREQ_WORK_SUSPEND);
#endif

	if (kbdev->pm.backend.callback_power_runtime_off) {
		kbdev->pm.backend.callback_power_runtime_off(kbdev);
		dev_dbg(dev, "runtime suspend\n");
	}
	return ret;
}
#endif

#ifdef KBASE_PM_RUNTIME
static int kbase_device_runtime_resume(struct device *dev)
{
	int ret = 0;
	struct kbase_device *kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

	dev_dbg(dev, "Callback %s\n", __func__);
	KBASE_KTRACE_ADD(kbdev, PM_RUNTIME_RESUME_CALLBACK, NULL, 0);
	if (kbdev->pm.backend.callback_power_runtime_on) {
		ret = kbdev->pm.backend.callback_power_runtime_on(kbdev);
		dev_dbg(dev, "runtime resume\n");
	}

#ifdef CONFIG_MALI_MIDGARD_DVFS
	kbase_pm_metrics_start(kbdev);
#endif

#ifdef CONFIG_MALI_DEVFREQ
	if (kbdev->devfreq)
		kbase_devfreq_enqueue_work(kbdev, DEVFREQ_WORK_RESUME);
#endif

	return ret;
}
#endif

#ifdef KBASE_PM_RUNTIME

static int kbase_device_runtime_idle(struct device *dev)
{
	struct kbase_device *kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

	dev_dbg(dev, "Callback %s\n", __func__);

	if (kbdev->pm.backend.callback_power_runtime_idle)
		return kbdev->pm.backend.callback_power_runtime_idle(kbdev);

	pm_runtime_mark_last_busy(kbdev->dev);
	return 0;
}
#endif

static const struct dev_pm_ops kbase_pm_ops = {
	.suspend = kbase_device_suspend,
	.resume = kbase_device_resume,
#ifdef KBASE_PM_RUNTIME
	.runtime_suspend = kbase_device_runtime_suspend,
	.runtime_resume = kbase_device_runtime_resume,
	.runtime_idle = kbase_device_runtime_idle,
#endif
};

#if IS_ENABLED(CONFIG_OF)
#if IS_ENABLED(CONFIG_MALI_MTK_COMMON)
static const struct of_device_id kbase_dt_ids[] = { { .compatible = "arm,mali-valhall" },
#else
static const struct of_device_id kbase_dt_ids[] = { { .compatible = "arm,malit6xx" },
						    { .compatible = "arm,mali-midgard" },
						    { .compatible = "arm,mali-bifrost" },
						    { .compatible = "arm,mali-valhall" },
#endif
						    {   } };
MODULE_DEVICE_TABLE(of, kbase_dt_ids);
#endif

static struct platform_driver kbase_platform_driver = {
	.probe = kbase_platform_device_probe,
	.remove = kbase_platform_device_remove,
	.driver = {
		   .name = KBASE_DRV_NAME,
		   .pm = &kbase_pm_ops,
		   .of_match_table = of_match_ptr(kbase_dt_ids),
		   .probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
};

#if (KERNEL_VERSION(5, 3, 0) > LINUX_VERSION_CODE) && IS_ENABLED(CONFIG_OF)
module_platform_driver(kbase_platform_driver);
#else
static int __init kbase_driver_init(void)
{
	int ret;
	printk(KERN_ERR "module_platform_driver() [start]\n");

#if (KERNEL_VERSION(5, 3, 0) <= LINUX_VERSION_CODE)
	mutex_init(&kbase_probe_mutex);
#endif

#ifndef CONFIG_OF
	ret = kbase_platform_register();
	printk(KERN_ERR "kbase_platform_register() \n");
	if (ret) {
		printk(KERN_ERR "kbase_platform_register() return [xxx] \n");
		return ret;
	}
#endif
	ret = platform_driver_register(&kbase_platform_driver);
	printk(KERN_ERR "platform_driver_register() ret = %d \n", ret);
#ifndef CONFIG_OF
	if (ret) {
		kbase_platform_unregister();
		return ret;
	}
#endif

	return ret;
}

static void __exit kbase_driver_exit(void)
{
	platform_driver_unregister(&kbase_platform_driver);
#ifndef CONFIG_OF
	kbase_platform_unregister();
#endif
}

module_init(kbase_driver_init);
module_exit(kbase_driver_exit);
#endif
MODULE_LICENSE("GPL");
MODULE_VERSION(MALI_RELEASE_NAME " (UK version " __stringify(BASE_UK_VERSION_MAJOR) "." __stringify(
	BASE_UK_VERSION_MINOR) ")");
MODULE_SOFTDEP("pre: memory_group_manager");
MODULE_INFO(import_ns, "DMA_BUF");

#define CREATE_TRACE_POINTS

#include "mali_linux_trace.h"

#ifdef CONFIG_MALI_GATOR_SUPPORT
EXPORT_TRACEPOINT_SYMBOL_GPL(mali_job_slots_event);
EXPORT_TRACEPOINT_SYMBOL_GPL(mali_pm_status);
EXPORT_TRACEPOINT_SYMBOL_GPL(mali_page_fault_insert_pages);
EXPORT_TRACEPOINT_SYMBOL_GPL(mali_total_alloc_pages_change);

void kbase_trace_mali_pm_status(u32 dev_id, u32 event, u64 value)
{
	trace_mali_pm_status(dev_id, event, value);
}

void kbase_trace_mali_job_slots_event(u32 dev_id, u32 event, const struct kbase_context *kctx,
				      u8 atom_id)
{
	trace_mali_job_slots_event(dev_id, event, (kctx != NULL ? (u32)kctx->tgid : 0U),
				   (kctx != NULL ? (u32)kctx->pid : 0U), atom_id);
}

void kbase_trace_mali_page_fault_insert_pages(u32 dev_id, int event, u32 value)
{
	trace_mali_page_fault_insert_pages(dev_id, event, value);
}

void kbase_trace_mali_total_alloc_pages_change(u32 dev_id, long long event)
{
	trace_mali_total_alloc_pages_change(dev_id, event);
}
#endif

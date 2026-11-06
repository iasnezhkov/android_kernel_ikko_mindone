// SPDX-License-Identifier: GPL-2.0
#include <mali_kbase.h>
#include <gpu/mali_kbase_gpu_fault.h>
#include <backend/gpu/mali_kbase_instr_internal.h>
#include <backend/gpu/mali_kbase_pm_internal.h>
#include <device/mali_kbase_device.h>
#include <mali_kbase_reset_gpu.h>
#include <mmu/mali_kbase_mmu.h>

#if IS_ENABLED(CONFIG_MALI_MTK_LOG_BUFFER)
#include <platform/mtk_platform_common/mtk_platform_logbuffer.h>
#endif

#if IS_ENABLED(CONFIG_MALI_MTK_DEBUG_DUMP)
#include <platform/mtk_platform_common.h>
#endif

#if IS_ENABLED(CONFIG_MALI_MTK_MBRAIN_SUPPORT)
#include <ged_mali_event.h>
#include <platform/mtk_platform_common/mtk_platform_mali_event.h>
#endif

static int busy_wait_cache_operation(struct kbase_device *kbdev, u32 irq_bit)
{
	const ktime_t wait_loop_start = ktime_get_raw();
	const u32 wait_time_ms = kbase_get_timeout_ms(kbdev, MMU_AS_INACTIVE_WAIT_TIMEOUT);
	bool completed = false;
	s64 diff;
	u32 irq_bits_to_check = irq_bit;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	{
		irq_bits_to_check |= RESET_COMPLETED;
	}

	do {
		unsigned int i;

		for (i = 0; i < 1000; i++) {
			if (kbase_reg_read32(kbdev, GPU_CONTROL_ENUM(GPU_IRQ_RAWSTAT)) &
			    irq_bits_to_check) {
				completed = true;
				break;
			}
		}

		diff = ktime_to_ms(ktime_sub(ktime_get_raw(), wait_loop_start));
	} while ((diff < wait_time_ms) && !completed);

	if (!completed) {
		char *irq_flag_name;

		switch (irq_bit) {
		case CLEAN_CACHES_COMPLETED:
			irq_flag_name = "CLEAN_CACHES_COMPLETED";
			break;
		case FLUSH_PA_RANGE_COMPLETED:
			irq_flag_name = "FLUSH_PA_RANGE_COMPLETED";
			break;
		default:
			irq_flag_name = "UNKNOWN";
			break;
		}

		dev_err(kbdev->dev,
			"Stuck waiting on %s bit, might be due to unstable GPU clk/pwr or possible faulty FPGA connector\n",
			irq_flag_name);
#if IS_ENABLED(CONFIG_MALI_MTK_LOG_BUFFER)
		mtk_logbuffer_type_print(kbdev, MTK_LOGBUFFER_TYPE_CRITICAL | MTK_LOGBUFFER_TYPE_EXCEPTION,
			"Stuck waiting on %s bit, might be due to unstable GPU clk/pwr or possible faulty FPGA connector\n",
			irq_flag_name);
#endif

#if IS_ENABLED(CONFIG_MALI_MTK_DEBUG_DUMP)
		mtk_common_debug(MTK_COMMON_DBG_DUMP_PM_STATUS, NULL, MTK_DBG_HOOK_BITSTUCK_FAIL);
		mtk_common_debug(MTK_COMMON_DBG_DUMP_INFRA_STATUS, NULL, MTK_DBG_HOOK_BITSTUCK_FAIL);
		mtk_common_debug(MTK_COMMON_DBG_DUMP_DB_BY_SETTING, NULL, MTK_DBG_HOOK_BITSTUCK_FAIL);
#endif

#if IS_ENABLED(CONFIG_MALI_MTK_TRIGGER_KE)
		if (kbdev->exception_mask & (1u << EXCEPTION_BIT_STUCK))
			BUG_ON(1);
#endif

		if (kbase_prepare_to_reset_gpu_locked(kbdev, RESET_FLAGS_NONE)) {
#if IS_ENABLED(CONFIG_MALI_MTK_MBRAIN_SUPPORT)
			ged_mali_event_update_gpu_reset_nolock(GPU_RESET_BUSY_WAIT_CACHE_OP_TIMEOUT);
#endif
			kbase_reset_gpu_locked(kbdev);
		}

		return -EBUSY;
	}

	KBASE_KTRACE_ADD(kbdev, CORE_GPU_IRQ_CLEAR, NULL, irq_bit);
	kbase_reg_write32(kbdev, GPU_CONTROL_ENUM(GPU_IRQ_CLEAR), irq_bit);

	return 0;
}

#if MALI_USE_CSF

int kbase_gpu_cache_flush_pa_range_and_busy_wait(struct kbase_device *kbdev, phys_addr_t phys,
						 size_t nr_bytes, u32 flush_op)
{
	u64 start_pa, end_pa;
	int ret = 0;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	kbase_reg_write32(kbdev, GPU_CONTROL_ENUM(GPU_IRQ_CLEAR), FLUSH_PA_RANGE_COMPLETED);

	start_pa = phys;
	end_pa = start_pa + nr_bytes - 1;

	kbase_reg_write64(kbdev, GPU_CONTROL_ENUM(GPU_COMMAND_ARG0), start_pa);
	kbase_reg_write64(kbdev, GPU_CONTROL_ENUM(GPU_COMMAND_ARG1), end_pa);
	kbase_reg_write32(kbdev, GPU_CONTROL_ENUM(GPU_COMMAND), flush_op);

	ret = busy_wait_cache_operation(kbdev, (u32)FLUSH_PA_RANGE_COMPLETED);

	return ret;
}
#endif

int kbase_gpu_cache_flush_and_busy_wait(struct kbase_device *kbdev, u32 flush_op)
{
	int need_to_wake_up = 0;
	int ret = 0;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (kbdev->cache_clean_in_progress) {

		u32 irq_mask = kbase_reg_read32(kbdev, GPU_CONTROL_ENUM(GPU_IRQ_MASK));

		kbase_reg_write32(kbdev, GPU_CONTROL_ENUM(GPU_IRQ_MASK),
				  irq_mask & ~CLEAN_CACHES_COMPLETED);

		ret = busy_wait_cache_operation(kbdev, CLEAN_CACHES_COMPLETED);
		if (ret)
			return ret;

		flush_op = GPU_COMMAND_FLUSH_CACHE_MERGE(kbdev->cache_clean_queued, flush_op);

		need_to_wake_up = 1;
	} else {

		kbase_reg_write32(kbdev, GPU_CONTROL_ENUM(GPU_IRQ_CLEAR), CLEAN_CACHES_COMPLETED);
	}

	KBASE_KTRACE_ADD(kbdev, CORE_GPU_CLEAN_INV_CACHES, NULL, flush_op);
	kbase_reg_write32(kbdev, GPU_CONTROL_ENUM(GPU_COMMAND), flush_op);

	ret = busy_wait_cache_operation(kbdev, CLEAN_CACHES_COMPLETED);
	if (ret)
		return ret;

	if (need_to_wake_up)
		kbase_gpu_cache_clean_wait_complete(kbdev);

	return ret;
}

void kbase_gpu_start_cache_clean_nolock(struct kbase_device *kbdev, u32 flush_op)
{
	u32 irq_mask;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (kbdev->cache_clean_in_progress) {

		kbdev->cache_clean_queued =
			GPU_COMMAND_FLUSH_CACHE_MERGE(kbdev->cache_clean_queued, flush_op);
		return;
	}

	irq_mask = kbase_reg_read32(kbdev, GPU_CONTROL_ENUM(GPU_IRQ_MASK));
	kbase_reg_write32(kbdev, GPU_CONTROL_ENUM(GPU_IRQ_MASK), irq_mask | CLEAN_CACHES_COMPLETED);

	KBASE_KTRACE_ADD(kbdev, CORE_GPU_CLEAN_INV_CACHES, NULL, flush_op);
	kbase_reg_write32(kbdev, GPU_CONTROL_ENUM(GPU_COMMAND), flush_op);

	kbdev->cache_clean_in_progress = true;
}

void kbase_gpu_start_cache_clean(struct kbase_device *kbdev, u32 flush_op)
{
	unsigned long flags;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbase_gpu_start_cache_clean_nolock(kbdev, flush_op);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
}

void kbase_gpu_cache_clean_wait_complete(struct kbase_device *kbdev)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	kbdev->cache_clean_queued = 0;
	kbdev->cache_clean_in_progress = false;
	wake_up(&kbdev->cache_clean_wait);
}

void kbase_clean_caches_done(struct kbase_device *kbdev)
{
	u32 irq_mask;
	unsigned long flags;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	if (kbdev->cache_clean_in_progress) {

		KBASE_KTRACE_ADD(kbdev, CORE_GPU_IRQ_CLEAR, NULL, CLEAN_CACHES_COMPLETED);
		kbase_reg_write32(kbdev, GPU_CONTROL_ENUM(GPU_IRQ_CLEAR), CLEAN_CACHES_COMPLETED);

		if (kbdev->cache_clean_queued) {
			u32 pended_flush_op = kbdev->cache_clean_queued;

			kbdev->cache_clean_queued = 0;

			KBASE_KTRACE_ADD(kbdev, CORE_GPU_CLEAN_INV_CACHES, NULL, pended_flush_op);
			kbase_reg_write32(kbdev, GPU_CONTROL_ENUM(GPU_COMMAND), pended_flush_op);
		} else {

			irq_mask = kbase_reg_read32(kbdev, GPU_CONTROL_ENUM(GPU_IRQ_MASK));
			kbase_reg_write32(kbdev, GPU_CONTROL_ENUM(GPU_IRQ_MASK),
					  irq_mask & ~CLEAN_CACHES_COMPLETED);

			kbase_gpu_cache_clean_wait_complete(kbdev);
		}
	}

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
}

static inline bool get_cache_clean_flag(struct kbase_device *kbdev)
{
	bool cache_clean_in_progress;
	unsigned long flags;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	cache_clean_in_progress = kbdev->cache_clean_in_progress;
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	return cache_clean_in_progress;
}

void kbase_gpu_wait_cache_clean(struct kbase_device *kbdev)
{
	while (get_cache_clean_flag(kbdev)) {
		if (wait_event_interruptible(kbdev->cache_clean_wait,
					     !kbdev->cache_clean_in_progress))
			dev_warn(kbdev->dev, "Wait for cache clean is interrupted");
	}
}

int kbase_gpu_wait_cache_clean_timeout(struct kbase_device *kbdev, unsigned int wait_timeout_ms)
{
	long remaining = (long)msecs_to_jiffies(wait_timeout_ms);
	int result = 0;

	while (remaining && get_cache_clean_flag(kbdev)) {
		remaining = wait_event_timeout(kbdev->cache_clean_wait,
					       !kbdev->cache_clean_in_progress, remaining);
	}

	if (!remaining) {
		dev_err(kbdev->dev,
			"Cache clean timed out. Might be caused by unstable GPU clk/pwr or faulty system");
#if IS_ENABLED(CONFIG_MALI_MTK_LOG_BUFFER)
		mtk_logbuffer_type_print(kbdev, MTK_LOGBUFFER_TYPE_CRITICAL | MTK_LOGBUFFER_TYPE_EXCEPTION,
			"Cache clean timed out. Might be caused by unstable GPU clk/pwr or faulty system\n");
#endif

#if IS_ENABLED(CONFIG_MALI_MTK_DEBUG_DUMP)
		mtk_common_debug(MTK_COMMON_DBG_DUMP_INFRA_STATUS, NULL, MTK_DBG_HOOK_BITSTUCK_FAIL);
		mtk_common_debug(MTK_COMMON_DBG_DUMP_GIC_STATUS, NULL, MTK_DBG_HOOK_BITSTUCK_FAIL);
#endif

		if (kbase_prepare_to_reset_gpu_locked(kbdev, RESET_FLAGS_HWC_UNRECOVERABLE_ERROR)) {
#if IS_ENABLED(CONFIG_MALI_MTK_MBRAIN_SUPPORT)
			ged_mali_event_update_gpu_reset_nolock(GPU_RESET_GPU_WAIT_CACHE_CLEAN_TIMEOUT);
#endif
			kbase_reset_gpu_locked(kbdev);
		}

		result = -ETIMEDOUT;
	}

	return result;
}

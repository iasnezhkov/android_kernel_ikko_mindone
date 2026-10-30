// SPDX-License-Identifier: GPL-2.0
#include <device/mali_kbase_device.h>
#include <linux/bitops.h>
#include <mali_kbase.h>
#include <mali_kbase_ctx_sched.h>
#include <mali_kbase_mem.h>
#include <mali_kbase_reset_gpu.h>
#include <mmu/mali_kbase_mmu_hw.h>
#include <tl/mali_kbase_tracepoints.h>
#include <linux/delay.h>

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

#if MALI_USE_CSF

static bool mmu_has_flush_skip_pgd_levels(struct kbase_gpu_props const *gpu_props)
{
	return gpu_props->gpu_id.arch_id >= GPU_ID_ARCH_MAKE(12, 0, 4);
}
#endif

static int lock_region(struct kbase_gpu_props const *gpu_props, u64 *lockaddr,
		       const struct kbase_mmu_hw_op_param *op_param)
{
	const u64 lockaddr_base = op_param->vpfn << PAGE_SHIFT;
	const u64 lockaddr_end = ((op_param->vpfn + op_param->nr) << PAGE_SHIFT) - 1;
	u64 lockaddr_size_log2;

	if (op_param->nr == 0)
		return -EINVAL;

	lockaddr_size_log2 = (u64)fls64(lockaddr_base ^ lockaddr_end);

	if (lockaddr_size_log2 > KBASE_LOCK_REGION_MAX_SIZE_LOG2)
		return -EINVAL;

	lockaddr_size_log2 =
		MAX(lockaddr_size_log2, kbase_get_lock_region_min_size_log2(gpu_props));

	*lockaddr = lockaddr_base & ~((1ull << lockaddr_size_log2) - 1);
	*lockaddr |= lockaddr_size_log2 - 1;

#if MALI_USE_CSF
	if (mmu_has_flush_skip_pgd_levels(gpu_props))
		*lockaddr =
			AS_LOCKADDR_FLUSH_SKIP_LEVELS_SET(*lockaddr, op_param->flush_skip_levels);
#endif

	return 0;
}

static int wait_ready(struct kbase_device *kbdev, unsigned int as_nr)
{
	u32 val;
	int err;
	const u32 timeout_us =
		kbase_get_timeout_ms(kbdev, MMU_AS_INACTIVE_WAIT_TIMEOUT) * USEC_PER_MSEC;

	if (unlikely(kbdev->mmu_unresponsive))
		return -EBUSY;

	err = kbase_reg_poll32_timeout(kbdev, MMU_AS_OFFSET(as_nr, STATUS), val,
				       !(val & AS_STATUS_AS_ACTIVE_EXT_MASK), 0, timeout_us, false);
	if (!err)
		return 0;

	dev_err(kbdev->dev,
		"AS_ACTIVE bit stuck for as %u. Might be caused by unstable GPU clk/pwr or faulty system",
		as_nr);
#if IS_ENABLED(CONFIG_MALI_MTK_LOG_BUFFER)
	mtk_logbuffer_type_print(kbdev, MTK_LOGBUFFER_TYPE_CRITICAL | MTK_LOGBUFFER_TYPE_EXCEPTION,
		"AS_ACTIVE bit stuck for as %u. Might be caused by unstable GPU clk/pwr or faulty system\n",
		as_nr);
#endif
	kbdev->mmu_unresponsive = true;

#if IS_ENABLED(CONFIG_MALI_MTK_DEBUG_DUMP)
	mtk_common_debug(MTK_COMMON_DBG_DUMP_AS_STATUS_NOLOCK, NULL, MTK_DBG_HOOK_BITSTUCK_FAIL);
	mtk_common_debug(MTK_COMMON_DBG_DUMP_PM_STATUS, NULL, MTK_DBG_HOOK_BITSTUCK_FAIL);
	mtk_common_debug(MTK_COMMON_DBG_DUMP_INFRA_STATUS, NULL, MTK_DBG_HOOK_BITSTUCK_FAIL);
	mtk_common_debug(MTK_COMMON_DBG_DUMP_DB_BY_SETTING, NULL, MTK_DBG_HOOK_BITSTUCK_FAIL);
#endif

#if IS_ENABLED(CONFIG_MALI_MTK_TRIGGER_KE)
	if (kbdev->exception_mask & (1u << EXCEPTION_BIT_STUCK))
		BUG_ON(1);
#endif

	if (kbase_prepare_to_reset_gpu_locked(kbdev, RESET_FLAGS_HWC_UNRECOVERABLE_ERROR)) {
#if IS_ENABLED(CONFIG_MALI_MTK_MBRAIN_SUPPORT)
		ged_mali_event_update_gpu_reset_nolock(GPU_RESET_MMU_WAIT_READY_TIMEOUT);
#endif
		kbase_reset_gpu_locked(kbdev);
	}

	return -ETIMEDOUT;
}

static int write_cmd(struct kbase_device *kbdev, unsigned int as_nr, u32 cmd)
{

	const int status = wait_ready(kbdev, as_nr);

	if (likely(status == 0))
		kbase_reg_write32(kbdev, MMU_AS_OFFSET(as_nr, COMMAND),
				  AS_COMMAND_COMMAND_SET(0U, cmd));
	else if (status == -EBUSY) {
		dev_dbg(kbdev->dev,
			"Skipped the wait for AS_ACTIVE bit for as %u, before sending MMU command %u",
			as_nr, cmd);
	} else {
		dev_err(kbdev->dev,
			"Wait for AS_ACTIVE bit failed for as %u, before sending MMU command %u",
			as_nr, cmd);
#if IS_ENABLED(CONFIG_MALI_MTK_LOG_BUFFER)
		mtk_logbuffer_type_print(kbdev, MTK_LOGBUFFER_TYPE_CRITICAL | MTK_LOGBUFFER_TYPE_EXCEPTION,
			"Wait for AS_ACTIVE bit failed for as %u, before sending MMU command %u\n",
			as_nr, cmd);
#endif
	}

	return status;
}

#if MALI_USE_CSF
static int wait_l2_power_trans_complete(struct kbase_device *kbdev)
{
	u32 val;
	const u32 timeout_us =
		kbase_get_timeout_ms(kbdev, MMU_AS_INACTIVE_WAIT_TIMEOUT) * USEC_PER_MSEC;
	const int err = kbase_reg_poll64_timeout(kbdev, GPU_CONTROL_ENUM(L2_PWRTRANS), val,
						 val == 0, 1, timeout_us, false);

	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (err) {
		dev_warn(kbdev->dev, "L2_PWRTRANS %016llx set for too long",
			 kbase_reg_read64(kbdev, GPU_CONTROL_ENUM(L2_PWRTRANS)));
		if (kbase_prepare_to_reset_gpu_locked(kbdev, RESET_FLAGS_NONE))
			kbase_reset_gpu_locked(kbdev);
	}

	return err;
}

#if !IS_ENABLED(CONFIG_MALI_NO_MALI)
static int wait_cores_power_trans_complete(struct kbase_device *kbdev)
{
#define WAIT_TIMEOUT 50000
#define DELAY_TIME_IN_US 1
	const int max_iterations = WAIT_TIMEOUT;
	int loop;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	for (loop = 0; loop < max_iterations; loop++) {
		u64 val = kbase_reg_read64(kbdev, GPU_CONTROL_ENUM(SHADER_PWRTRANS));

		if (!val)
			break;

		udelay(DELAY_TIME_IN_US);
	}

	if (loop == max_iterations) {
		dev_warn(kbdev->dev, "SHADER_PWRTRANS %016llx set for too long",
			 kbase_reg_read64(kbdev, GPU_CONTROL_ENUM(SHADER_PWRTRANS)));
		return -ETIMEDOUT;
	}

	return 0;
}

static int apply_hw_issue_GPU2019_3901_wa(struct kbase_device *kbdev, u32 *mmu_cmd,
					  unsigned int as_nr)
{
	int ret = 0;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (kbdev->pm.backend.l2_state != KBASE_L2_OFF) {
		if (unlikely(*mmu_cmd != AS_COMMAND_COMMAND_FLUSH_MEM)) {
			dev_warn(kbdev->dev, "Unexpected MMU command(%u) received", *mmu_cmd);
			return -EINVAL;
		}

		ret = wait_ready(kbdev, as_nr);
		if (unlikely(ret))
			return ret;

		ret = kbase_gpu_cache_flush_and_busy_wait(kbdev, GPU_COMMAND_CACHE_CLN_INV_LSC);
		if (unlikely(ret))
			return ret;

		ret = wait_cores_power_trans_complete(kbdev);
		if (unlikely(ret)) {
#if IS_ENABLED(CONFIG_MALI_MTK_LOG_BUFFER)
			mtk_logbuffer_type_print(kbdev, MTK_LOGBUFFER_TYPE_CRITICAL | MTK_LOGBUFFER_TYPE_EXCEPTION,
				"wait_cores_power_trans_complete fail, try to do reset\n");
#endif
			if (kbase_prepare_to_reset_gpu_locked(kbdev,
							      RESET_FLAGS_HWC_UNRECOVERABLE_ERROR)) {
#if IS_ENABLED(CONFIG_MALI_MTK_MBRAIN_SUPPORT)
				ged_mali_event_update_gpu_reset_nolock(GPU_RESET_HW_ISSUE_2019_3901_WA);
#endif
				kbase_reset_gpu_locked(kbdev);
			}
			return ret;
		}

		*mmu_cmd = AS_COMMAND_COMMAND_FLUSH_PT;
	}

	return ret;
}
#endif
#endif

void kbase_mmu_hw_configure(struct kbase_device *kbdev, struct kbase_as *as)
{
	struct kbase_mmu_setup *current_setup = &as->current_setup;
	u64 transcfg = 0;

	lockdep_assert_held(&kbdev->hwaccess_lock);
#if !MALI_USE_CSF
	lockdep_assert_held(&kbdev->mmu_hw_mutex);
#endif

	transcfg = current_setup->transcfg;

	transcfg = AS_TRANSCFG_PTW_MEMATTR_SET(transcfg, AS_TRANSCFG_PTW_MEMATTR_WRITE_BACK);

	transcfg |= AS_TRANSCFG_R_ALLOCATE_MASK;

	if (kbdev->system_coherency != COHERENCY_NONE) {

		transcfg = AS_TRANSCFG_PTW_SH_SET(transcfg, AS_TRANSCFG_PTW_SH_OUTER_SHAREABLE);
	}

	kbase_reg_write64(kbdev, MMU_AS_OFFSET(as->number, TRANSCFG), transcfg);
	kbase_reg_write64(kbdev, MMU_AS_OFFSET(as->number, TRANSTAB), current_setup->transtab);
	kbase_reg_write64(kbdev, MMU_AS_OFFSET(as->number, MEMATTR), current_setup->memattr);

	KBASE_TLSTREAM_TL_ATTRIB_AS_CONFIG(kbdev, as, current_setup->transtab,
					   current_setup->memattr, transcfg);

	write_cmd(kbdev, as->number, AS_COMMAND_COMMAND_UPDATE);
#if MALI_USE_CSF

	wait_ready(kbdev, as->number);
#endif
}

static void mmu_command_instr(struct kbase_device *kbdev, u32 kctx_id, u32 cmd, u64 lock_addr,
			      enum kbase_caller_mmu_sync_info mmu_sync_info)
{
	u64 lock_addr_base = AS_LOCKADDR_LOCKADDR_BASE_GET(lock_addr);
	u32 lock_addr_size = AS_LOCKADDR_LOCKADDR_SIZE_GET(lock_addr);

	bool is_mmu_synchronous = (mmu_sync_info == CALLER_MMU_SYNC);

	KBASE_TLSTREAM_AUX_MMU_COMMAND(kbdev, kctx_id, cmd, is_mmu_synchronous, lock_addr_base,
				       lock_addr_size);
}

static int mmu_hw_set_lock_addr(struct kbase_device *kbdev, unsigned int as_nr, u64 *lock_addr,
				const struct kbase_mmu_hw_op_param *op_param)
{
	int ret;

	ret = lock_region(&kbdev->gpu_props, lock_addr, op_param);

	if (!ret) {

		kbase_reg_write64(kbdev, MMU_AS_OFFSET(as_nr, LOCKADDR), *lock_addr);
	}
	return ret;
}

static int mmu_hw_do_lock_no_wait(struct kbase_device *kbdev, struct kbase_as *as, u64 *lock_addr,
				  const struct kbase_mmu_hw_op_param *op_param)
{
	int ret;

	ret = mmu_hw_set_lock_addr(kbdev, as->number, lock_addr, op_param);

	if (likely(!ret))
		ret = write_cmd(kbdev, as->number, AS_COMMAND_COMMAND_LOCK);

	return ret;
}

static int mmu_hw_do_lock(struct kbase_device *kbdev, struct kbase_as *as,
			  const struct kbase_mmu_hw_op_param *op_param)
{
	int ret;
	u64 lock_addr = 0x0;

	if (WARN_ON(kbdev == NULL) || WARN_ON(as == NULL))
		return -EINVAL;

	ret = mmu_hw_do_lock_no_wait(kbdev, as, &lock_addr, op_param);

	if (!ret)
		ret = wait_ready(kbdev, as->number);

	if (!ret)
		mmu_command_instr(kbdev, op_param->kctx_id, AS_COMMAND_COMMAND_LOCK, lock_addr,
				  op_param->mmu_sync_info);

	return ret;
}

int kbase_mmu_hw_do_lock(struct kbase_device *kbdev, struct kbase_as *as,
			 const struct kbase_mmu_hw_op_param *op_param)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	return mmu_hw_do_lock(kbdev, as, op_param);
}

int kbase_mmu_hw_do_unlock_no_addr(struct kbase_device *kbdev, struct kbase_as *as,
				   const struct kbase_mmu_hw_op_param *op_param)
{
	int ret = 0;

	if (WARN_ON(kbdev == NULL) || WARN_ON(as == NULL))
		return -EINVAL;

	ret = write_cmd(kbdev, as->number, AS_COMMAND_COMMAND_UNLOCK);

	if (likely(!ret))
		ret = wait_ready(kbdev, as->number);

	if (likely(!ret)) {

		u64 lock_addr = kbase_reg_read64(kbdev, MMU_AS_OFFSET(as->number, LOCKADDR));

		mmu_command_instr(kbdev, op_param->kctx_id, AS_COMMAND_COMMAND_UNLOCK, lock_addr,
				  op_param->mmu_sync_info);
	}

	return ret;
}

int kbase_mmu_hw_do_unlock(struct kbase_device *kbdev, struct kbase_as *as,
			   const struct kbase_mmu_hw_op_param *op_param)
{
	int ret = 0;
	u64 lock_addr = 0x0;

	if (WARN_ON(kbdev == NULL) || WARN_ON(as == NULL))
		return -EINVAL;

	ret = mmu_hw_set_lock_addr(kbdev, as->number, &lock_addr, op_param);

	if (!ret)
		ret = kbase_mmu_hw_do_unlock_no_addr(kbdev, as, op_param);

	return ret;
}

int kbase_mmu_hw_do_flush(struct kbase_device *kbdev, struct kbase_as *as,
			  const struct kbase_mmu_hw_op_param *op_param)
{
	int ret;
	u64 lock_addr = 0x0;
	u32 mmu_cmd = AS_COMMAND_COMMAND_FLUSH_MEM;
	const enum kbase_mmu_op_type flush_op = op_param->op;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (WARN_ON(kbdev == NULL) || WARN_ON(as == NULL))
		return -EINVAL;

	if (flush_op != KBASE_MMU_OP_FLUSH_PT && flush_op != KBASE_MMU_OP_FLUSH_MEM) {
		dev_err(kbdev->dev, "Unexpected flush operation received");
#if IS_ENABLED(CONFIG_MALI_MTK_LOG_BUFFER)
		mtk_logbuffer_type_print(kbdev, MTK_LOGBUFFER_TYPE_CRITICAL | MTK_LOGBUFFER_TYPE_EXCEPTION,
			"Unexpected flush operation received\n");
#endif
		return -EINVAL;
	}

	if (flush_op == KBASE_MMU_OP_FLUSH_PT)
		mmu_cmd = AS_COMMAND_COMMAND_FLUSH_PT;

	ret = mmu_hw_do_lock_no_wait(kbdev, as, &lock_addr, op_param);
	if (ret)
		return ret;

#if MALI_USE_CSF && !IS_ENABLED(CONFIG_MALI_NO_MALI)

	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_GPU2019_3901) &&
	    mmu_cmd == AS_COMMAND_COMMAND_FLUSH_MEM) {
		ret = apply_hw_issue_GPU2019_3901_wa(kbdev, &mmu_cmd, as->number);
		if (ret) {
			dev_warn(
				kbdev->dev,
				"Failed to apply WA for HW issue when doing MMU flush op on VA range %llx-%llx for AS %u",
				op_param->vpfn << PAGE_SHIFT,
				((op_param->vpfn + op_param->nr) << PAGE_SHIFT) - 1, as->number);

		}
	}
#endif

	ret = write_cmd(kbdev, as->number, mmu_cmd);

	if (likely(!ret))
		ret = wait_ready(kbdev, as->number);

	if (likely(!ret)) {
		mmu_command_instr(kbdev, op_param->kctx_id, mmu_cmd, lock_addr,
				  op_param->mmu_sync_info);
#if MALI_USE_CSF
		if (flush_op == KBASE_MMU_OP_FLUSH_MEM &&
		    kbdev->pm.backend.apply_hw_issue_TITANHW_2938_wa &&
		    kbdev->pm.backend.l2_state == KBASE_L2_PEND_OFF)
			ret = wait_l2_power_trans_complete(kbdev);
#endif
	}

	return ret;
}

int kbase_mmu_hw_do_flush_on_gpu_ctrl(struct kbase_device *kbdev, struct kbase_as *as,
				      const struct kbase_mmu_hw_op_param *op_param)
{
	int ret, ret2;
	u32 gpu_cmd = GPU_COMMAND_CACHE_CLN_INV_L2_LSC;
	const enum kbase_mmu_op_type flush_op = op_param->op;

	if (WARN_ON(kbdev == NULL) || WARN_ON(as == NULL))
		return -EINVAL;

	if (flush_op != KBASE_MMU_OP_FLUSH_PT && flush_op != KBASE_MMU_OP_FLUSH_MEM) {
		dev_err(kbdev->dev, "Unexpected flush operation received");
#if IS_ENABLED(CONFIG_MALI_MTK_LOG_BUFFER)
		mtk_logbuffer_type_print(kbdev, MTK_LOGBUFFER_TYPE_CRITICAL | MTK_LOGBUFFER_TYPE_EXCEPTION,
			"Unexpected flush operation received\n");
#endif
		return -EINVAL;
	}

	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (flush_op == KBASE_MMU_OP_FLUSH_PT)
		gpu_cmd = GPU_COMMAND_CACHE_CLN_INV_L2;

	ret = mmu_hw_do_lock(kbdev, as, op_param);
	if (ret)
		return ret;

	ret = kbase_gpu_cache_flush_and_busy_wait(kbdev, gpu_cmd);

	ret2 = kbase_mmu_hw_do_unlock_no_addr(kbdev, as, op_param);

#if MALI_USE_CSF
	if (!ret && !ret2) {
		if (flush_op == KBASE_MMU_OP_FLUSH_MEM &&
		    kbdev->pm.backend.apply_hw_issue_TITANHW_2938_wa &&
		    kbdev->pm.backend.l2_state == KBASE_L2_PEND_OFF)
			ret = wait_l2_power_trans_complete(kbdev);
	}
#endif

	return ret ?: ret2;
}

void kbase_mmu_hw_clear_fault(struct kbase_device *kbdev, struct kbase_as *as,
			      enum kbase_mmu_fault_type type)
{
	unsigned long flags;
	u32 pf_bf_mask;

	CSTD_UNUSED(type);

	spin_lock_irqsave(&kbdev->mmu_mask_change, flags);

	if (kbdev->irq_reset_flush)
		goto unlock;

	pf_bf_mask = MMU_PAGE_FAULT(as->number);
#if !MALI_USE_CSF
	if (type == KBASE_MMU_FAULT_TYPE_BUS || type == KBASE_MMU_FAULT_TYPE_BUS_UNEXPECTED)
		pf_bf_mask |= MMU_BUS_ERROR(as->number);
#endif
	kbase_reg_write32(kbdev, MMU_CONTROL_ENUM(IRQ_CLEAR), pf_bf_mask);

unlock:
	spin_unlock_irqrestore(&kbdev->mmu_mask_change, flags);
}

void kbase_mmu_hw_enable_fault(struct kbase_device *kbdev, struct kbase_as *as,
			       enum kbase_mmu_fault_type type)
{
	unsigned long flags;
	u32 irq_mask;

	CSTD_UNUSED(type);

	spin_lock_irqsave(&kbdev->mmu_mask_change, flags);

	if (kbdev->irq_reset_flush)
		goto unlock;

	irq_mask = kbase_reg_read32(kbdev, MMU_CONTROL_ENUM(IRQ_MASK)) | MMU_PAGE_FAULT(as->number);

#if !MALI_USE_CSF
	if (type == KBASE_MMU_FAULT_TYPE_BUS || type == KBASE_MMU_FAULT_TYPE_BUS_UNEXPECTED)
		irq_mask |= MMU_BUS_ERROR(as->number);
#endif
	kbase_reg_write32(kbdev, MMU_CONTROL_ENUM(IRQ_MASK), irq_mask);

unlock:
	spin_unlock_irqrestore(&kbdev->mmu_mask_change, flags);
}

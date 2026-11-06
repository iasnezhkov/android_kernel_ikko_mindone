// SPDX-License-Identifier: GPL-2.0
#include <mali_kbase.h>
#include <hw_access/mali_kbase_hw_access_regmap.h>
#include <mali_kbase_hwaccess_instr.h>
#include <device/mali_kbase_device.h>
#include <backend/gpu/mali_kbase_instr_internal.h>

#define WAIT_FOR_DUMP_TIMEOUT_MS 5000

static int wait_prfcnt_ready(struct kbase_device *kbdev)
{
	u32 val;
	const u32 timeout_us =
		kbase_get_timeout_ms(kbdev, KBASE_PRFCNT_ACTIVE_TIMEOUT) * USEC_PER_MSEC;
	const int err = kbase_reg_poll32_timeout(kbdev, GPU_CONTROL_ENUM(GPU_STATUS), val,
						 !(val & GPU_STATUS_PRFCNT_ACTIVE), 0, timeout_us,
						 false);
	if (err) {
		dev_err(kbdev->dev, "PRFCNT_ACTIVE bit stuck\n");
		return -EBUSY;
	}
	return 0;
}

int kbase_instr_hwcnt_enable_internal(struct kbase_device *kbdev, struct kbase_context *kctx,
				      struct kbase_instr_hwcnt_enable *enable)
{
	unsigned long flags;
	int err = -EINVAL;
	u32 irq_mask;
	u32 prfcnt_config;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	if ((enable->dump_buffer == 0ULL) || (enable->dump_buffer & (2048 - 1)))
		return err;

	spin_lock_irqsave(&kbdev->hwcnt.lock, flags);

	if (kbdev->hwcnt.backend.state != KBASE_INSTR_STATE_DISABLED) {

		spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
		return err;
	}

	if (kbase_is_gpu_removed(kbdev)) {

		spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
		return err;
	}

	irq_mask = kbase_reg_read32(kbdev, GPU_CONTROL_ENUM(GPU_IRQ_MASK));
	kbase_reg_write32(kbdev, GPU_CONTROL_ENUM(GPU_IRQ_MASK),
			  irq_mask | PRFCNT_SAMPLE_COMPLETED);

	kbdev->hwcnt.kctx = kctx;

	kbdev->hwcnt.addr = enable->dump_buffer;
	kbdev->hwcnt.addr_bytes = enable->dump_buffer_bytes;

	spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);

	prfcnt_config = (u32)kctx->as_nr << PRFCNT_CONFIG_AS_SHIFT;
#ifdef CONFIG_MALI_PRFCNT_SET_SELECT_VIA_DEBUG_FS
	prfcnt_config |= (u32)kbdev->hwcnt.backend.override_counter_set
			 << PRFCNT_CONFIG_SETSELECT_SHIFT;
#else
	prfcnt_config |= (u32)enable->counter_set << PRFCNT_CONFIG_SETSELECT_SHIFT;
#endif

	err = wait_prfcnt_ready(kbdev);
	if (err)
		return err;

	kbase_reg_write32(kbdev, GPU_CONTROL_ENUM(PRFCNT_CONFIG),
			  prfcnt_config | PRFCNT_CONFIG_MODE_OFF);

	err = wait_prfcnt_ready(kbdev);
	if (err)
		return err;

	kbase_reg_write64(kbdev, GPU_CONTROL_ENUM(PRFCNT_BASE), enable->dump_buffer);

	kbase_reg_write32(kbdev, GPU_CONTROL_ENUM(PRFCNT_JM_EN), enable->fe_bm);

	kbase_reg_write32(kbdev, GPU_CONTROL_ENUM(PRFCNT_SHADER_EN), enable->shader_bm);
	kbase_reg_write32(kbdev, GPU_CONTROL_ENUM(PRFCNT_MMU_L2_EN), enable->mmu_l2_bm);

	kbase_reg_write32(kbdev, GPU_CONTROL_ENUM(PRFCNT_TILER_EN), enable->tiler_bm);

	kbase_reg_write32(kbdev, GPU_CONTROL_ENUM(PRFCNT_CONFIG),
			  prfcnt_config | PRFCNT_CONFIG_MODE_MANUAL);

	spin_lock_irqsave(&kbdev->hwcnt.lock, flags);

	kbdev->hwcnt.backend.state = KBASE_INSTR_STATE_IDLE;
	kbdev->hwcnt.backend.triggered = 1;
	wake_up(&kbdev->hwcnt.backend.wait);

	spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);

	dev_dbg(kbdev->dev, "HW counters dumping set-up for context %pK", kctx);
	return 0;
}

static void kbasep_instr_hwc_disable_hw_prfcnt(struct kbase_device *kbdev)
{
	u32 irq_mask;

	lockdep_assert_held(&kbdev->hwaccess_lock);
	lockdep_assert_held(&kbdev->hwcnt.lock);

	if (kbase_is_gpu_removed(kbdev))

		return;

	irq_mask = kbase_reg_read32(kbdev, GPU_CONTROL_ENUM(GPU_IRQ_MASK));

	kbase_reg_write32(kbdev, GPU_CONTROL_ENUM(GPU_IRQ_MASK),
			  irq_mask & ~PRFCNT_SAMPLE_COMPLETED);

	wait_prfcnt_ready(kbdev);
	kbase_reg_write32(kbdev, GPU_CONTROL_ENUM(PRFCNT_CONFIG), 0);

	kbdev->hwcnt.kctx = NULL;
	kbdev->hwcnt.addr = 0ULL;
	kbdev->hwcnt.addr_bytes = 0ULL;
}

int kbase_instr_hwcnt_disable_internal(struct kbase_context *kctx)
{
	unsigned long flags, pm_flags;
	struct kbase_device *kbdev = kctx->kbdev;
	const unsigned long timeout = msecs_to_jiffies(WAIT_FOR_DUMP_TIMEOUT_MS);
	unsigned int remaining;

	while (1) {
		spin_lock_irqsave(&kbdev->hwaccess_lock, pm_flags);
		spin_lock_irqsave(&kbdev->hwcnt.lock, flags);

		if (kbdev->hwcnt.backend.state == KBASE_INSTR_STATE_UNRECOVERABLE_ERROR) {

			spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
			spin_unlock_irqrestore(&kbdev->hwaccess_lock, pm_flags);

			return 0;
		}

		if (kbdev->hwcnt.backend.state == KBASE_INSTR_STATE_DISABLED) {

			spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
			spin_unlock_irqrestore(&kbdev->hwaccess_lock, pm_flags);
			return -EINVAL;
		}

		if (kbdev->hwcnt.kctx != kctx) {

			spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
			spin_unlock_irqrestore(&kbdev->hwaccess_lock, pm_flags);
			return -EINVAL;
		}

		if (kbdev->hwcnt.backend.state == KBASE_INSTR_STATE_IDLE)
			break;

		spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, pm_flags);

		remaining = wait_event_timeout(kbdev->hwcnt.backend.wait,
					       kbdev->hwcnt.backend.triggered != 0, timeout);

		if (remaining == 0)
			kbdev->hwcnt.backend.state = KBASE_INSTR_STATE_UNRECOVERABLE_ERROR;
	}

	kbdev->hwcnt.backend.state = KBASE_INSTR_STATE_DISABLED;
	kbdev->hwcnt.backend.triggered = 0;

	kbasep_instr_hwc_disable_hw_prfcnt(kbdev);

	spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, pm_flags);

	dev_dbg(kbdev->dev, "HW counters dumping disabled for context %pK", kctx);

	return 0;
}

int kbase_instr_hwcnt_request_dump(struct kbase_context *kctx)
{
	unsigned long flags;
	int err = -EINVAL;
	struct kbase_device *kbdev = kctx->kbdev;

	spin_lock_irqsave(&kbdev->hwcnt.lock, flags);

	if (kbdev->hwcnt.kctx != kctx) {

		goto unlock;
	}

	if (kbdev->hwcnt.backend.state != KBASE_INSTR_STATE_IDLE) {

		goto unlock;
	}

	if (kbase_is_gpu_removed(kbdev)) {

		goto unlock;
	}

	kbdev->hwcnt.backend.triggered = 0;

	kbdev->hwcnt.backend.state = KBASE_INSTR_STATE_DUMPING;

	err = wait_prfcnt_ready(kbdev);
	if (err)
		goto unlock;

	kbase_reg_write64(kbdev, GPU_CONTROL_ENUM(PRFCNT_BASE), kbdev->hwcnt.addr);

	KBASE_KTRACE_ADD(kbdev, CORE_GPU_PRFCNT_SAMPLE, NULL, kbdev->hwcnt.addr);

	kbase_reg_write32(kbdev, GPU_CONTROL_ENUM(GPU_COMMAND), GPU_COMMAND_PRFCNT_SAMPLE);

	dev_dbg(kbdev->dev, "HW counters dumping done for context %pK", kctx);

unlock:
	spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
	return err;
}
KBASE_EXPORT_SYMBOL(kbase_instr_hwcnt_request_dump);

bool kbase_instr_hwcnt_dump_complete(struct kbase_context *kctx, bool *const success)
{
	unsigned long flags;
	bool complete = false;
	struct kbase_device *kbdev = kctx->kbdev;

	spin_lock_irqsave(&kbdev->hwcnt.lock, flags);

	if (kbdev->hwcnt.backend.state == KBASE_INSTR_STATE_IDLE) {
		*success = true;
		complete = true;
	} else if (kbdev->hwcnt.backend.state == KBASE_INSTR_STATE_FAULT) {
		*success = false;
		complete = true;
		kbdev->hwcnt.backend.state = KBASE_INSTR_STATE_IDLE;
	}

	spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);

	return complete;
}
KBASE_EXPORT_SYMBOL(kbase_instr_hwcnt_dump_complete);

void kbase_instr_hwcnt_sample_done(struct kbase_device *kbdev)
{
	unsigned long flags;

	spin_lock_irqsave(&kbdev->hwcnt.lock, flags);

	if (kbdev->hwcnt.backend.state == KBASE_INSTR_STATE_FAULT) {
		kbdev->hwcnt.backend.triggered = 1;
		wake_up(&kbdev->hwcnt.backend.wait);
	} else if (kbdev->hwcnt.backend.state == KBASE_INSTR_STATE_DUMPING) {

		kbdev->hwcnt.backend.state = KBASE_INSTR_STATE_IDLE;
		kbdev->hwcnt.backend.triggered = 1;
		wake_up(&kbdev->hwcnt.backend.wait);
	}

	spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
}

int kbase_instr_hwcnt_wait_for_dump(struct kbase_context *kctx)
{
	struct kbase_device *kbdev = kctx->kbdev;
	unsigned long flags;
	int err;

	unsigned long remaining;
	const unsigned long timeout = msecs_to_jiffies(WAIT_FOR_DUMP_TIMEOUT_MS);

	remaining = wait_event_timeout(kbdev->hwcnt.backend.wait,
				       kbdev->hwcnt.backend.triggered != 0, timeout);
	if (remaining == 0) {
		err = -ETIME;

		kbdev->hwcnt.backend.state = KBASE_INSTR_STATE_UNRECOVERABLE_ERROR;
		goto timed_out;
	}

	spin_lock_irqsave(&kbdev->hwcnt.lock, flags);

	if (kbdev->hwcnt.backend.state == KBASE_INSTR_STATE_FAULT) {
		err = -EINVAL;
		kbdev->hwcnt.backend.state = KBASE_INSTR_STATE_IDLE;
	} else if (kbdev->hwcnt.backend.state == KBASE_INSTR_STATE_UNRECOVERABLE_ERROR) {
		err = -EIO;
	} else {

		KBASE_DEBUG_ASSERT(kbdev->hwcnt.backend.state == KBASE_INSTR_STATE_IDLE);
		err = 0;
	}

	spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
timed_out:
	return err;
}

int kbase_instr_hwcnt_clear(struct kbase_context *kctx)
{
	unsigned long flags;
	int err = -EINVAL;
	struct kbase_device *kbdev = kctx->kbdev;

	spin_lock_irqsave(&kbdev->hwcnt.lock, flags);

	if (kbdev->hwcnt.kctx != kctx || kbdev->hwcnt.backend.state != KBASE_INSTR_STATE_IDLE)
		goto unlock;

	if (kbase_is_gpu_removed(kbdev)) {

		goto unlock;
	}

	err = wait_prfcnt_ready(kbdev);
	if (err)
		goto unlock;

	KBASE_KTRACE_ADD(kbdev, CORE_GPU_PRFCNT_CLEAR, NULL, 0);
	kbase_reg_write32(kbdev, GPU_CONTROL_ENUM(GPU_COMMAND), GPU_COMMAND_PRFCNT_CLEAR);

unlock:
	spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
	return err;
}
KBASE_EXPORT_SYMBOL(kbase_instr_hwcnt_clear);

void kbase_instr_hwcnt_on_unrecoverable_error(struct kbase_device *kbdev)
{
	unsigned long flags;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	spin_lock_irqsave(&kbdev->hwcnt.lock, flags);

	if (kbdev->hwcnt.backend.state == KBASE_INSTR_STATE_UNRECOVERABLE_ERROR) {
		spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
		return;
	}

	kbdev->hwcnt.backend.state = KBASE_INSTR_STATE_UNRECOVERABLE_ERROR;

	if (kbdev->hwcnt.backend.state != KBASE_INSTR_STATE_DISABLED)
		kbasep_instr_hwc_disable_hw_prfcnt(kbdev);

	kbdev->hwcnt.backend.triggered = 1;
	wake_up(&kbdev->hwcnt.backend.wait);

	spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
}
KBASE_EXPORT_SYMBOL(kbase_instr_hwcnt_on_unrecoverable_error);

void kbase_instr_hwcnt_on_before_reset(struct kbase_device *kbdev)
{
	unsigned long flags;

	spin_lock_irqsave(&kbdev->hwcnt.lock, flags);

	if (kbdev->hwcnt.backend.state == KBASE_INSTR_STATE_UNRECOVERABLE_ERROR)
		kbdev->hwcnt.backend.state = KBASE_INSTR_STATE_DISABLED;

	spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
}
KBASE_EXPORT_SYMBOL(kbase_instr_hwcnt_on_before_reset);

int kbase_instr_backend_init(struct kbase_device *kbdev)
{
	spin_lock_init(&kbdev->hwcnt.lock);

	kbdev->hwcnt.backend.state = KBASE_INSTR_STATE_DISABLED;

	init_waitqueue_head(&kbdev->hwcnt.backend.wait);

#ifdef CONFIG_MALI_PRFCNT_SET_SELECT_VIA_DEBUG_FS

#if defined(CONFIG_MALI_PRFCNT_SET_SECONDARY)
	kbdev->hwcnt.backend.override_counter_set = KBASE_HWCNT_PHYSICAL_SET_SECONDARY;
#elif defined(CONFIG_MALI_PRFCNT_SET_TERTIARY)
	kbdev->hwcnt.backend.override_counter_set = KBASE_HWCNT_PHYSICAL_SET_TERTIARY;
#else

	kbdev->hwcnt.backend.override_counter_set = KBASE_HWCNT_PHYSICAL_SET_PRIMARY;
#endif
#endif
	return 0;
}

void kbase_instr_backend_term(struct kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
}

#ifdef CONFIG_MALI_PRFCNT_SET_SELECT_VIA_DEBUG_FS
void kbase_instr_backend_debugfs_init(struct kbase_device *kbdev)
{

	debugfs_create_u8("hwcnt_set_select", 0644, kbdev->mali_debugfs_directory,
			  (u8 *)&kbdev->hwcnt.backend.override_counter_set);
}
#endif

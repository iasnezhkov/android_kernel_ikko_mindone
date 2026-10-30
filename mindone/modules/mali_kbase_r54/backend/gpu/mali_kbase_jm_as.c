// SPDX-License-Identifier: GPL-2.0
#include <mali_kbase.h>
#include <mali_kbase_hwaccess_jm.h>
#include <mali_kbase_ctx_sched.h>

static void assign_and_activate_kctx_addr_space(struct kbase_device *kbdev,
						struct kbase_context *kctx,
						struct kbase_as *current_as)
{
	struct kbasep_js_device_data *js_devdata = &kbdev->js_data;

	CSTD_UNUSED(current_as);

	lockdep_assert_held(&kctx->jctx.sched_info.ctx.jsctx_mutex);
	lockdep_assert_held(&js_devdata->runpool_mutex);
	lockdep_assert_held(&kbdev->hwaccess_lock);

#if !MALI_USE_CSF

	kbasep_js_ctx_attr_runpool_retain_ctx(kbdev, kctx);
#endif

	kbasep_js_set_submit_allowed(js_devdata, kctx);

	kbase_js_runpool_inc_context_count(kbdev, kctx);
}

bool kbase_backend_use_ctx_sched(struct kbase_device *kbdev, struct kbase_context *kctx,
				 unsigned int js)
{
	int i;

	if (kbdev->hwaccess.active_kctx[js] == kctx) {

		return true;
	}

	for (i = 0; i < kbdev->nr_hw_address_spaces; i++) {
		if (kbdev->as_to_kctx[i] == kctx) {

			return true;
		}
	}

	return false;
}

void kbase_backend_release_ctx_irq(struct kbase_device *kbdev, struct kbase_context *kctx)
{
	int as_nr = kctx->as_nr;

	if (as_nr == KBASEP_AS_NR_INVALID) {
		WARN(1, "Attempting to release context without ASID\n");
		return;
	}

	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (atomic_read(&kctx->refcount) != 1) {
		WARN(1, "Attempting to release active ASID\n");
		return;
	}

	kbasep_js_clear_submit_allowed(&kbdev->js_data, kctx);

	kbase_ctx_sched_release_ctx(kctx);
	kbase_js_runpool_dec_context_count(kbdev, kctx);
}

void kbase_backend_release_ctx_noirq(struct kbase_device *kbdev, struct kbase_context *kctx)
{
	CSTD_UNUSED(kbdev);
	CSTD_UNUSED(kctx);
}

int kbase_backend_find_and_release_free_address_space(struct kbase_device *kbdev,
						      struct kbase_context *kctx)
{
	struct kbasep_js_device_data *js_devdata;
	struct kbasep_js_kctx_info *js_kctx_info;
	unsigned long flags;
	int i;

	js_devdata = &kbdev->js_data;
	js_kctx_info = &kctx->jctx.sched_info;

	mutex_lock(&js_kctx_info->ctx.jsctx_mutex);
	mutex_lock(&js_devdata->runpool_mutex);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	for (i = 0; i < kbdev->nr_hw_address_spaces; i++) {
		struct kbasep_js_kctx_info *as_js_kctx_info;
		struct kbase_context *as_kctx;

		as_kctx = kbdev->as_to_kctx[i];
		as_js_kctx_info = &as_kctx->jctx.sched_info;

		if (as_kctx && !kbase_ctx_flag(as_kctx, KCTX_PRIVILEGED) &&
		    atomic_read(&as_kctx->refcount) == 1) {
			if (!kbase_ctx_sched_inc_refcount_nolock(as_kctx)) {
				WARN(1, "Failed to retain active context\n");

				spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
				mutex_unlock(&js_devdata->runpool_mutex);
				mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);

				return KBASEP_AS_NR_INVALID;
			}

			kbasep_js_clear_submit_allowed(js_devdata, as_kctx);

			spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
			mutex_unlock(&js_devdata->runpool_mutex);
			mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);

			mutex_lock(&as_js_kctx_info->ctx.jsctx_mutex);
			mutex_lock(&js_devdata->runpool_mutex);

			kbasep_js_runpool_release_ctx_nolock(kbdev, as_kctx);

			if (!kbase_ctx_flag(as_kctx, KCTX_SCHEDULED)) {
				kbasep_js_runpool_requeue_or_kill_ctx(kbdev, as_kctx, true);

				mutex_unlock(&js_devdata->runpool_mutex);
				mutex_unlock(&as_js_kctx_info->ctx.jsctx_mutex);

				return i;
			}

			mutex_unlock(&js_devdata->runpool_mutex);
			mutex_unlock(&as_js_kctx_info->ctx.jsctx_mutex);

			mutex_lock(&js_kctx_info->ctx.jsctx_mutex);
			mutex_lock(&js_devdata->runpool_mutex);
			spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		}
	}

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	mutex_unlock(&js_devdata->runpool_mutex);
	mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);

	return KBASEP_AS_NR_INVALID;
}

bool kbase_backend_use_ctx(struct kbase_device *kbdev, struct kbase_context *kctx, int as_nr)
{
	struct kbasep_js_device_data *js_devdata;
	struct kbase_as *new_address_space = NULL;
	int js;

	js_devdata = &kbdev->js_data;

	for (js = 0; js < BASE_JM_MAX_NR_SLOTS; js++) {
		if (kbdev->hwaccess.active_kctx[js] == kctx) {
			WARN(1, "Context is already scheduled in\n");
			return false;
		}
	}

	new_address_space = &kbdev->as[as_nr];

	lockdep_assert_held(&js_devdata->runpool_mutex);
	lockdep_assert_held(&kbdev->mmu_hw_mutex);
	lockdep_assert_held(&kbdev->hwaccess_lock);

	assign_and_activate_kctx_addr_space(kbdev, kctx, new_address_space);

	if (kbase_ctx_flag(kctx, KCTX_PRIVILEGED)) {

		kbase_ctx_sched_retain_ctx_refcount(kctx);
	}

	return true;
}

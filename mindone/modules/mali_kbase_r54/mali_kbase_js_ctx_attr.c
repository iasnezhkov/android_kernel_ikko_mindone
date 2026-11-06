// SPDX-License-Identifier: GPL-2.0
#include <mali_kbase.h>
#include <mali_kbase_config.h>

static bool kbasep_js_ctx_attr_runpool_retain_attr(struct kbase_device *kbdev,
						   struct kbase_context *kctx,
						   enum kbasep_js_ctx_attr attribute)
{
	struct kbasep_js_device_data *js_devdata;
	struct kbasep_js_kctx_info *js_kctx_info;
	bool runpool_state_changed = false;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);
	KBASE_DEBUG_ASSERT(attribute < KBASEP_JS_CTX_ATTR_COUNT);
	js_devdata = &kbdev->js_data;
	js_kctx_info = &kctx->jctx.sched_info;

	lockdep_assert_held(&js_kctx_info->ctx.jsctx_mutex);
	lockdep_assert_held(&kbdev->hwaccess_lock);

	KBASE_DEBUG_ASSERT(kbase_ctx_flag(kctx, KCTX_SCHEDULED));

	if (kbasep_js_ctx_attr_is_attr_on_ctx(kctx, attribute) != false) {
		KBASE_DEBUG_ASSERT(js_devdata->runpool_irq.ctx_attr_ref_count[attribute] < S8_MAX);
		++(js_devdata->runpool_irq.ctx_attr_ref_count[attribute]);

		if (js_devdata->runpool_irq.ctx_attr_ref_count[attribute] == 1) {

			runpool_state_changed = true;
			KBASE_KTRACE_ADD_JM(kbdev, JS_CTX_ATTR_NOW_ON_RUNPOOL, kctx, NULL, 0u,
					    attribute);
		}
	}

	return runpool_state_changed;
}

static bool kbasep_js_ctx_attr_runpool_release_attr(struct kbase_device *kbdev,
						    struct kbase_context *kctx,
						    enum kbasep_js_ctx_attr attribute)
{
	struct kbasep_js_device_data *js_devdata;
	struct kbasep_js_kctx_info *js_kctx_info;
	bool runpool_state_changed = false;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);
	KBASE_DEBUG_ASSERT(attribute < KBASEP_JS_CTX_ATTR_COUNT);
	js_devdata = &kbdev->js_data;
	js_kctx_info = &kctx->jctx.sched_info;

	lockdep_assert_held(&js_kctx_info->ctx.jsctx_mutex);
	lockdep_assert_held(&kbdev->hwaccess_lock);
	KBASE_DEBUG_ASSERT(kbase_ctx_flag(kctx, KCTX_SCHEDULED));

	if (kbasep_js_ctx_attr_is_attr_on_ctx(kctx, attribute) != false) {
		KBASE_DEBUG_ASSERT(js_devdata->runpool_irq.ctx_attr_ref_count[attribute] > 0);
		--(js_devdata->runpool_irq.ctx_attr_ref_count[attribute]);

		if (js_devdata->runpool_irq.ctx_attr_ref_count[attribute] == 0) {

			runpool_state_changed = true;
			KBASE_KTRACE_ADD_JM(kbdev, JS_CTX_ATTR_NOW_OFF_RUNPOOL, kctx, NULL, 0u,
					    attribute);
		}
	}

	return runpool_state_changed;
}

static bool kbasep_js_ctx_attr_ctx_retain_attr(struct kbase_device *kbdev,
					       struct kbase_context *kctx,
					       enum kbasep_js_ctx_attr attribute)
{
	struct kbasep_js_kctx_info *js_kctx_info;
	bool runpool_state_changed = false;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);
	KBASE_DEBUG_ASSERT(attribute < KBASEP_JS_CTX_ATTR_COUNT);
	js_kctx_info = &kctx->jctx.sched_info;

	lockdep_assert_held(&kbdev->hwaccess_lock);
	lockdep_assert_held(&js_kctx_info->ctx.jsctx_mutex);
	KBASE_DEBUG_ASSERT(js_kctx_info->ctx.ctx_attr_ref_count[attribute] < U32_MAX);

	++(js_kctx_info->ctx.ctx_attr_ref_count[attribute]);

	if (kbase_ctx_flag(kctx, KCTX_SCHEDULED) &&
	    js_kctx_info->ctx.ctx_attr_ref_count[attribute] == 1) {

		KBASE_KTRACE_ADD_JM(kbdev, JS_CTX_ATTR_NOW_ON_CTX, kctx, NULL, 0u, attribute);
		runpool_state_changed =
			kbasep_js_ctx_attr_runpool_retain_attr(kbdev, kctx, attribute);
	}

	return runpool_state_changed;
}

static bool kbasep_js_ctx_attr_ctx_release_attr(struct kbase_device *kbdev,
						struct kbase_context *kctx,
						enum kbasep_js_ctx_attr attribute)
{
	struct kbasep_js_kctx_info *js_kctx_info;
	bool runpool_state_changed = false;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);
	KBASE_DEBUG_ASSERT(attribute < KBASEP_JS_CTX_ATTR_COUNT);
	js_kctx_info = &kctx->jctx.sched_info;

	lockdep_assert_held(&js_kctx_info->ctx.jsctx_mutex);
	KBASE_DEBUG_ASSERT(js_kctx_info->ctx.ctx_attr_ref_count[attribute] > 0);

	if (kbase_ctx_flag(kctx, KCTX_SCHEDULED) &&
	    js_kctx_info->ctx.ctx_attr_ref_count[attribute] == 1) {
		lockdep_assert_held(&kbdev->hwaccess_lock);

		runpool_state_changed =
			kbasep_js_ctx_attr_runpool_release_attr(kbdev, kctx, attribute);
		KBASE_KTRACE_ADD_JM(kbdev, JS_CTX_ATTR_NOW_OFF_CTX, kctx, NULL, 0u, attribute);
	}

	--(js_kctx_info->ctx.ctx_attr_ref_count[attribute]);

	return runpool_state_changed;
}

void kbasep_js_ctx_attr_runpool_retain_ctx(struct kbase_device *kbdev, struct kbase_context *kctx)
{
	bool runpool_state_changed;
	int i;

	for (i = 0; i < KBASEP_JS_CTX_ATTR_COUNT; ++i) {
		if (kbasep_js_ctx_attr_is_attr_on_ctx(kctx, (enum kbasep_js_ctx_attr)i) != false) {

			runpool_state_changed = kbasep_js_ctx_attr_runpool_retain_attr(
				kbdev, kctx, (enum kbasep_js_ctx_attr)i);

			CSTD_UNUSED(runpool_state_changed);
		}
	}
}

bool kbasep_js_ctx_attr_runpool_release_ctx(struct kbase_device *kbdev, struct kbase_context *kctx)
{
	bool runpool_state_changed = false;
	int i;

	for (i = 0; i < KBASEP_JS_CTX_ATTR_COUNT; ++i) {
		if (kbasep_js_ctx_attr_is_attr_on_ctx(kctx, (enum kbasep_js_ctx_attr)i) != false) {

			runpool_state_changed |= kbasep_js_ctx_attr_runpool_release_attr(
				kbdev, kctx, (enum kbasep_js_ctx_attr)i);
		}
	}

	return runpool_state_changed;
}

void kbasep_js_ctx_attr_ctx_retain_atom(struct kbase_device *kbdev, struct kbase_context *kctx,
					struct kbase_jd_atom *katom)
{
	bool runpool_state_changed = false;
	base_jd_core_req core_req;

	KBASE_DEBUG_ASSERT(katom);
	core_req = katom->core_req;

	if (core_req & BASE_JD_REQ_ONLY_COMPUTE)
		runpool_state_changed |=
			kbasep_js_ctx_attr_ctx_retain_attr(kbdev, kctx, KBASEP_JS_CTX_ATTR_COMPUTE);
	else
		runpool_state_changed |= kbasep_js_ctx_attr_ctx_retain_attr(
			kbdev, kctx, KBASEP_JS_CTX_ATTR_NON_COMPUTE);

	if ((core_req & (BASE_JD_REQ_CS | BASE_JD_REQ_ONLY_COMPUTE | BASE_JD_REQ_T)) != 0 &&
	    (core_req & (BASE_JD_REQ_COHERENT_GROUP | BASE_JD_REQ_SPECIFIC_COHERENT_GROUP)) == 0) {

		runpool_state_changed |= kbasep_js_ctx_attr_ctx_retain_attr(
			kbdev, kctx, KBASEP_JS_CTX_ATTR_COMPUTE_ALL_CORES);
	}

	CSTD_UNUSED(runpool_state_changed);
}

bool kbasep_js_ctx_attr_ctx_release_atom(struct kbase_device *kbdev, struct kbase_context *kctx,
					 struct kbasep_js_atom_retained_state *katom_retained_state)
{
	bool runpool_state_changed = false;
	base_jd_core_req core_req;

	KBASE_DEBUG_ASSERT(katom_retained_state);
	core_req = katom_retained_state->core_req;

	if (kbasep_js_atom_retained_state_is_valid(katom_retained_state) == false)
		return false;

	if (core_req & BASE_JD_REQ_ONLY_COMPUTE)
		runpool_state_changed |= kbasep_js_ctx_attr_ctx_release_attr(
			kbdev, kctx, KBASEP_JS_CTX_ATTR_COMPUTE);
	else
		runpool_state_changed |= kbasep_js_ctx_attr_ctx_release_attr(
			kbdev, kctx, KBASEP_JS_CTX_ATTR_NON_COMPUTE);

	if ((core_req & (BASE_JD_REQ_CS | BASE_JD_REQ_ONLY_COMPUTE | BASE_JD_REQ_T)) != 0 &&
	    (core_req & (BASE_JD_REQ_COHERENT_GROUP | BASE_JD_REQ_SPECIFIC_COHERENT_GROUP)) == 0) {

		runpool_state_changed |= kbasep_js_ctx_attr_ctx_release_attr(
			kbdev, kctx, KBASEP_JS_CTX_ATTR_COMPUTE_ALL_CORES);
	}

	return runpool_state_changed;
}

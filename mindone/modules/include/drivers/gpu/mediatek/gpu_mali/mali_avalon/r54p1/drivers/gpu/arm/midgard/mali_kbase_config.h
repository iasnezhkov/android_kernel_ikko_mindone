/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_CONFIG_H_
#define _KBASE_CONFIG_H_

#include <mali_malisw.h>

#include <linux/mm.h>

struct kbase_device;

#if !MALI_USE_CSF

struct kbase_context;

struct kbase_jd_atom;
#endif

struct kbase_platform_funcs_conf {

	int (*platform_init_func)(struct kbase_device *kbdev);

	void (*platform_term_func)(struct kbase_device *kbdev);

	int (*platform_late_init_func)(struct kbase_device *kbdev);

	void (*platform_late_term_func)(struct kbase_device *kbdev);

#if !MALI_USE_CSF

	int (*platform_handler_context_init_func)(struct kbase_context *kctx);

	void (*platform_handler_context_term_func)(struct kbase_context *kctx);

	void (*platform_handler_atom_submit_func)(struct kbase_jd_atom *katom);

	void (*platform_handler_atom_complete_func)(struct kbase_jd_atom *katom);
#endif
};

struct kbase_pm_callback_conf {

	void (*power_off_callback)(struct kbase_device *kbdev);

	int (*power_on_callback)(struct kbase_device *kbdev);

	void (*power_suspend_callback)(struct kbase_device *kbdev);

	void (*power_resume_callback)(struct kbase_device *kbdev);

	int (*power_runtime_init_callback)(struct kbase_device *kbdev);

	void (*power_runtime_term_callback)(struct kbase_device *kbdev);

	void (*power_runtime_off_callback)(struct kbase_device *kbdev);

	int (*power_runtime_on_callback)(struct kbase_device *kbdev);

	int (*power_runtime_idle_callback)(struct kbase_device *kbdev);

	int (*soft_reset_callback)(struct kbase_device *kbdev);

	void (*power_runtime_gpu_idle_callback)(struct kbase_device *kbdev);

	void (*power_runtime_gpu_active_callback)(struct kbase_device *kbdev);
};

struct kbase_gpu_clk_notifier_data {
	void *gpu_clk_handle;
	unsigned long old_rate;
	unsigned long new_rate;
};

struct kbase_clk_rate_trace_op_conf {

	void *(*enumerate_gpu_clk)(struct kbase_device *kbdev, unsigned int index);

	unsigned long (*get_gpu_clk_rate)(struct kbase_device *kbdev, void *gpu_clk_handle);

	int (*gpu_clk_notifier_register)(struct kbase_device *kbdev, void *gpu_clk_handle,
					 struct notifier_block *nb);

	void (*gpu_clk_notifier_unregister)(struct kbase_device *kbdev, void *gpu_clk_handle,
					    struct notifier_block *nb);
};

#if IS_ENABLED(CONFIG_OF)
struct kbase_platform_config {
};
#else

struct kbase_io_memory_region {
	u64 start;
	u64 end;
};

struct kbase_io_resources {
	u32 job_irq_number;
	u32 mmu_irq_number;
	u32 gpu_irq_number;
	struct kbase_io_memory_region io_memory_region;
};

struct kbase_platform_config {
	const struct kbase_io_resources *io_resources;
};

#endif

struct kbase_platform_config *kbase_get_platform_config(void);

int kbasep_platform_device_init(struct kbase_device *kbdev);

void kbasep_platform_device_term(struct kbase_device *kbdev);

int kbasep_platform_device_late_init(struct kbase_device *kbdev);

void kbasep_platform_device_late_term(struct kbase_device *kbdev);

#if !MALI_USE_CSF

int kbasep_platform_context_init(struct kbase_context *kctx);

void kbasep_platform_context_term(struct kbase_context *kctx);

void kbasep_platform_event_atom_submit(struct kbase_jd_atom *katom);

void kbasep_platform_event_atom_complete(struct kbase_jd_atom *katom);
#endif

#ifndef CONFIG_OF

int kbase_platform_register(void);

void kbase_platform_unregister(void);
#endif

#endif

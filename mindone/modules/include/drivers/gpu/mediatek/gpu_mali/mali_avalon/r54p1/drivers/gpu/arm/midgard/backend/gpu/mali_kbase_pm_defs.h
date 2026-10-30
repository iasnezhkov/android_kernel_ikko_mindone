/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_PM_HWACCESS_DEFS_H_
#define _KBASE_PM_HWACCESS_DEFS_H_

#include "mali_kbase_pm_always_on.h"
#include "mali_kbase_pm_coarse_demand.h"

#include <hw_access/mali_kbase_hw_access_regmap.h>

#include <mtk_ddk_define.h>

#if defined(CONFIG_PM_RUNTIME) || defined(CONFIG_PM)
#define KBASE_PM_RUNTIME 1
#endif

#if IS_ENABLED(CONFIG_MALI_MIDGARD_DVFS) && \
    IS_ENABLED(CONFIG_MALI_MTK_DVFS_POLICY) && \
    IS_ENABLED(CONFIG_MALI_MTK_GPU_DVFS_ASYNC)

#define NUM_PERF_COUNTERS (9)

#elif IS_ENABLED(CONFIG_MALI_MIDGARD_DVFS) && \
    IS_ENABLED(CONFIG_MALI_MTK_DVFS_POLICY)

#define NUM_PERF_COUNTERS (6)
#endif

struct kbase_device;
struct kbase_jd_atom;

#if MALI_USE_CSF
enum kbase_pm_core_type {
	KBASE_PM_CORE_L2 = HOST_POWER_ENUM(L2_PRESENT),
	KBASE_PM_CORE_SHADER = HOST_POWER_ENUM(SHADER_PRESENT),
	KBASE_PM_CORE_TILER = HOST_POWER_ENUM(TILER_PRESENT),
	KBASE_PM_CORE_STACK = HOST_POWER_ENUM(STACK_PRESENT),

	KBASE_PM_CORE_NEURAL = HOST_POWER_ENUM(NEURAL_PRESENT),

	KBASE_PM_CORE_BASE = HOST_POWER_ENUM(BASE_PRESENT)
};
#else
enum kbase_pm_core_type {
	KBASE_PM_CORE_L2 = GPU_CONTROL_ENUM(L2_PRESENT),
	KBASE_PM_CORE_SHADER = GPU_CONTROL_ENUM(SHADER_PRESENT),
	KBASE_PM_CORE_TILER = GPU_CONTROL_ENUM(TILER_PRESENT),
	KBASE_PM_CORE_STACK = GPU_CONTROL_ENUM(STACK_PRESENT)
};
#endif

enum kbase_l2_core_state {
#define KBASEP_L2_STATE(n) KBASE_L2_##n,
#include "mali_kbase_pm_l2_states.h"
#undef KBASEP_L2_STATE
};

#if MALI_USE_CSF

enum kbase_mcu_state {
#define KBASEP_MCU_STATE(n) KBASE_MCU_##n,
#include "mali_kbase_pm_mcu_states.h"
#undef KBASEP_MCU_STATE
};
#endif

enum kbase_shader_core_state {
#define KBASEP_SHADER_STATE(n) KBASE_SHADERS_##n,
#include "mali_kbase_pm_shader_states.h"
#undef KBASEP_SHADER_STATE
};

enum kbase_pm_runtime_suspend_abort_reason {
	ABORT_REASON_NONE,
	ABORT_REASON_DB_MIRROR_IRQ,
	ABORT_REASON_NON_IDLE_CGS
};

#define KBASE_GPU_SUPPORTS_GPU_SLEEP ((uint8_t)0)
#define KBASE_GPU_SUPPORTS_FW_SLEEP_ON_IDLE ((uint8_t)1)
#define KBASE_GPU_PERF_COUNTERS_COLLECTION_ENABLED ((uint8_t)2)
#define KBASE_GPU_IGNORE_IDLE_EVENT ((uint8_t)3)
#define KBASE_GPU_NON_IDLE_OFF_SLOT_GROUPS_AVAILABLE ((uint8_t)4)

#define KBASE_GPU_FW_SLEEP_ON_IDLE_ALLOWED                             \
	((uint8_t)((1 << KBASE_GPU_SUPPORTS_GPU_SLEEP) |               \
		   (1 << KBASE_GPU_SUPPORTS_FW_SLEEP_ON_IDLE) |        \
		   (0 << KBASE_GPU_PERF_COUNTERS_COLLECTION_ENABLED) | \
		   (0 << KBASE_GPU_IGNORE_IDLE_EVENT) |                \
		   (0 << KBASE_GPU_NON_IDLE_OFF_SLOT_GROUPS_AVAILABLE)))

struct kbasep_pm_metrics {
#if !MALI_USE_CSF || \
	!IS_ENABLED(CONFIG_MALI_MIDGARD_DVFS) || !IS_ENABLED(CONFIG_MALI_MTK_DVFS_POLICY)
	u32 time_busy;
	u32 time_idle;
#endif
#if MALI_USE_CSF
	u32 time_in_protm;
#if IS_ENABLED(CONFIG_MALI_MIDGARD_DVFS) && \
	IS_ENABLED(CONFIG_MALI_MTK_DVFS_POLICY)

	u32 time_busy[NUM_PERF_COUNTERS];
	u32 time_idle[NUM_PERF_COUNTERS];
	u32 counterRaw[NUM_PERF_COUNTERS];
	u32 busy_cl[2];
	u32 busy_gl;
	u32 busy_gl_plus[3];
#endif

#else
	u32 busy_cl[2];
	u32 busy_gl;
	u32 busy_gl_plus[3];
#endif
};

struct kbasep_pm_metrics_state {
	ktime_t time_period_start;
#if MALI_USE_CSF
	void *ipa_control_client;
	bool skip_gpu_active_sanity_check;
#else
	bool gpu_active;
	u32 active_cl_ctx[2];
	u32 active_gl_ctx[3];
#endif
	spinlock_t lock;

	void *platform_data;
	struct kbase_device *kbdev;

	struct kbasep_pm_metrics values;

#ifdef CONFIG_MALI_MIDGARD_DVFS
	bool initialized;
	struct hrtimer timer;
	atomic_t timer_state;
	struct kbasep_pm_metrics dvfs_last;
	struct kbasep_pm_metrics dvfs_diff;
#endif
};

struct kbasep_pm_tick_timer_state {
	struct workqueue_struct *wq;
	struct work_struct work;
	struct hrtimer timer;

	ktime_t configured_interval;
	unsigned int default_ticks;
	unsigned int configured_ticks;
	unsigned int remaining_ticks;

	bool cancel_queued;
	bool needed;
};

union kbase_pm_policy_data {
	struct kbasep_pm_policy_always_on always_on;
	struct kbasep_pm_policy_coarse_demand coarse_demand;
};

struct kbase_pm_backend_data {
	const struct kbase_pm_policy *pm_current_policy;
	union kbase_pm_policy_data pm_policy_data;
	bool reset_done;
	wait_queue_head_t reset_done_wait;
	int gpu_cycle_counter_requests;
	spinlock_t gpu_cycle_counter_requests_lock;

	wait_queue_head_t gpu_in_desired_state_wait;

	bool gpu_powered;
	bool gpu_ready;

	u64 pm_shaders_core_mask;

	bool cg1_disabled;

	struct kbasep_pm_metrics_state metrics;

	struct kbasep_pm_tick_timer_state shader_tick_timer;

	bool poweroff_wait_in_progress;
	bool invoke_poweroff_wait_wq_when_l2_off;
	bool poweron_required;

	struct workqueue_struct *gpu_poweroff_wait_wq;
	struct work_struct gpu_poweroff_wait_work;

	wait_queue_head_t poweroff_wait;

	int (*callback_power_on)(struct kbase_device *kbdev);
	void (*callback_power_off)(struct kbase_device *kbdev);
	void (*callback_power_suspend)(struct kbase_device *kbdev);
	void (*callback_power_resume)(struct kbase_device *kbdev);
	int (*callback_power_runtime_on)(struct kbase_device *kbdev);
	void (*callback_power_runtime_off)(struct kbase_device *kbdev);
	int (*callback_power_runtime_idle)(struct kbase_device *kbdev);
	int (*callback_soft_reset)(struct kbase_device *kbdev);
	void (*callback_power_runtime_gpu_idle)(struct kbase_device *kbdev);
	void (*callback_power_runtime_gpu_active)(struct kbase_device *kbdev);

	u64 ca_cores_enabled;

#if MALI_USE_CSF
	bool apply_hw_issue_TITANHW_2938_wa;
	enum kbase_mcu_state mcu_state;
#endif
	enum kbase_l2_core_state l2_state;
	enum kbase_shader_core_state shaders_state;
	u64 shaders_avail;
	u64 shaders_desired_mask;
#if MALI_USE_CSF
	bool mcu_desired;
	bool mcu_poweron_required;
	bool policy_change_clamp_state_to_off;
	unsigned int csf_pm_sched_flags;
	struct mutex policy_change_lock;
	struct workqueue_struct *core_idle_wq;
	struct work_struct core_idle_work;

#ifdef KBASE_PM_RUNTIME
	unsigned long gpu_sleep_allowed;
	bool gpu_sleep_mode_active;
	bool exit_gpu_sleep_mode;
	bool gpu_idled;
	bool gpu_wakeup_override;
	bool db_mirror_interrupt_enabled;
	enum kbase_pm_runtime_suspend_abort_reason runtime_suspend_abort_reason;
#endif

	bool l2_force_off_after_mcu_halt;
#endif
	bool l2_desired;
	bool l2_always_on;
	bool shaders_desired;

	bool in_reset;
#if IS_ENABLED(CONFIG_MALI_MTK_RESET_RELOAD_ON_FW)
	bool fw_reload_on_reset_worker;
#endif
#if !MALI_USE_CSF
	bool partial_shaderoff;

	bool protected_entry_transition_override;
	bool protected_transition_override;
	int protected_l2_override;
#endif

	bool hwcnt_desired;
	bool hwcnt_disabled;
	struct work_struct hwcnt_disable_work;

	u64 gpu_clock_suspend_freq;
	bool gpu_clock_slow_down_wa;
	bool gpu_clock_slow_down_desired;
	bool gpu_clock_slowed_down;
	struct work_struct gpu_clock_control_work;
};

#if MALI_USE_CSF

#define CSF_DYNAMIC_PM_CORE_KEEP_ON (1 << 0)

#define CSF_DYNAMIC_PM_SCHED_IGNORE_IDLE (1 << 1)

#define CSF_DYNAMIC_PM_SCHED_NO_SUSPEND (1 << 2)

#define ALWAYS_ON_PM_SCHED_FLAGS                                          \
	(CSF_DYNAMIC_PM_CORE_KEEP_ON | CSF_DYNAMIC_PM_SCHED_IGNORE_IDLE | \
	 CSF_DYNAMIC_PM_SCHED_NO_SUSPEND)
#define COARSE_ON_DEMAND_PM_SCHED_FLAGS (0)
#if !MALI_CUSTOMER_RELEASE
#define ALWAYS_ON_DEMAND_PM_SCHED_FLAGS (CSF_DYNAMIC_PM_SCHED_IGNORE_IDLE)
#endif
#endif

enum kbase_pm_policy_id {
	KBASE_PM_POLICY_ID_COARSE_DEMAND,
#if !MALI_CUSTOMER_RELEASE
	KBASE_PM_POLICY_ID_ALWAYS_ON_DEMAND,
#endif
	KBASE_PM_POLICY_ID_ALWAYS_ON
};

enum kbase_pm_policy_event {

	KBASE_PM_POLICY_EVENT_IDLE,

	KBASE_PM_POLICY_EVENT_POWER_ON,

	KBASE_PM_POLICY_EVENT_TIMER_HIT,

	KBASE_PM_POLICY_EVENT_TIMER_MISS
};

struct kbase_pm_policy {
	char *name;

	void (*init)(struct kbase_device *kbdev);

	void (*term)(struct kbase_device *kbdev);

	bool (*shaders_needed)(struct kbase_device *kbdev);

	bool (*get_core_active)(struct kbase_device *kbdev);

	void (*handle_event)(struct kbase_device *kbdev, enum kbase_pm_policy_event event);

	enum kbase_pm_policy_id id;

#if MALI_USE_CSF

	unsigned int pm_sched_flags;
#endif
};

#endif

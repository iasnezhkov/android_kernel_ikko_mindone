/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_DEFS_H_
#define _KBASE_DEFS_H_

#include <mali_kbase_config.h>
#include <mali_base_hwconfig_features.h>
#include <mali_base_hwconfig_issues.h>
#include <mali_kbase_mem_lowlevel.h>
#include <mmu/mali_kbase_mmu_hw.h>
#include <backend/gpu/mali_kbase_instr_defs.h>
#include <backend/gpu/mali_kbase_pm_defs.h>
#include <mali_kbase_pm.h>
#include <mali_kbase_reg_track.h>
#include <mali_kbase_gpuprops_types.h>
#include <hwcnt/mali_kbase_hwcnt_watchdog_if.h>

#if MALI_USE_CSF
#include <hwcnt/backend/mali_kbase_hwcnt_backend_csf.h>
#else
#include <hwcnt/backend/mali_kbase_hwcnt_backend_jm.h>
#include <hwcnt/backend/mali_kbase_hwcnt_backend_jm_watchdog.h>
#endif

#include "debug/mali_kbase_debug_ktrace_defs.h"

#include <protected_mode_switcher.h>
#include <linux/version_compat_defs.h>

#if IS_ENABLED(CONFIG_DEBUG_FS)
#include <linux/debugfs.h>
#endif

#ifdef CONFIG_MALI_DEVFREQ
#include <linux/devfreq.h>
#endif

#ifdef CONFIG_MALI_ARBITER_SUPPORT
#include <arbiter/mali_kbase_arbiter_defs.h>
#endif

#include <linux/memory_group_manager.h>

#include <linux/atomic.h>
#include <linux/mempool.h>
#include <linux/notifier.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/sizes.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>

#if IS_ENABLED(CONFIG_MALI_MTK_GPU_IDLE_STRESS_TEST) && IS_ENABLED(CONFIG_MALI_MTK_API_SYNC_UPDATE)
#include <linux/hrtimer.h>
#endif

#if IS_ENABLED(CONFIG_MALI_MTK_PREVENT_PRINTK_TOO_MUCH)
#include "platform/mtk_platform_utils.h"
#endif

#if IS_ENABLED(CONFIG_MALI_MTK_LOG_BUFFER)
#include <platform/mtk_platform_common/mtk_platform_logbuffer.h>
#endif

#define RESET_TIMEOUT 500

#define BASE_JM_MAX_NR_SLOTS 3

#define BASE_MAX_NR_AS 16

#define MIDGARD_MMU_LEVEL(x) (x)

#define MIDGARD_MMU_TOPLEVEL MIDGARD_MMU_LEVEL(0)

#define MIDGARD_MMU_BOTTOMLEVEL MIDGARD_MMU_LEVEL(3)

#define GROWABLE_FLAGS_REQUIRED (KBASE_REG_PF_GROW | KBASE_REG_GPU_WR)

#define KBASEP_AS_NR_INVALID (-1)

#define KBASE_LOCK_REGION_MAX_SIZE_LOG2 (48)

#include "mali_kbase_hwaccess_defs.h"

#define KBASE_PERMANENTLY_MAPPED_MEM_LIMIT_PAGES ((64 * 1024ul * 1024ul) >> PAGE_SHIFT)

#define KBASE_HWCNT_GPU_VIRTUALIZER_DUMP_THRESHOLD_NS (200 * NSEC_PER_USEC)

#if MALI_USE_CSF

#define KBASE_HWCNT_BACKEND_CSF_RING_BUFFER_COUNT (128)
#endif

#define BASE_MAX_NR_CLOCKS_REGULATORS (2)

#if IS_ENABLED(CONFIG_MALI_MTK_ACP_FORCE_SYNC_DEBUG)

#define FORCE_SYNC_NONE 0
#define FORCE_SYNC_CMD  1
#define FORCE_SYNC_DTS  2
#endif

#if IS_ENABLED(CONFIG_MALI_MTK_GPU_IDLE_STRESS_TEST) && IS_ENABLED(CONFIG_MALI_MTK_API_SYNC_UPDATE)
#define API_SYNC_FLAG_RESET 0x00060000
#define API_SYNC_FLAG_SET   0x00070000
#define API_SYNC_FLAG_DEBUG 0x39000000
#define API_SYNC_FLAG_DEBUG_INIT (API_SYNC_FLAG_DEBUG | 0x00030000)

#define API_SYNC_LEVEL_0 0
#define API_SYNC_LEVEL_1 3
#define API_SYNC_LEVEL_2 6

#define API_SYNC_DEFAULT_TIMEOUT_MS 360000
#define API_SYNC_MAXIMUM_TIMEOUT_MIN 0xFF

struct api_sync_target_level {
	int orig_level;
	int mapping_level;
};
#endif

struct kbase_context;
struct kbase_device;
struct kbase_as;
struct kbase_mmu_setup;
struct kbase_kinstr_jm;

#if IS_ENABLED(CONFIG_MALI_TRACE_POWER_GPU_WORK_PERIOD)

struct kbase_gpu_metrics {
	struct list_head active_list;
	struct list_head inactive_list;
};

struct kbase_gpu_metrics_ctx {
	struct list_head link;
	u64 active_start_time;
	u64 active_end_time;
	unsigned int aid;
	unsigned int kctx_count;
	u8 active_cnt;
};
#endif

struct kbase_io_access {
	uintptr_t addr;
	u32 value;
};

struct kbase_io_history {
	bool enabled;

	spinlock_t lock;
	size_t count;
	u16 size;
	struct kbase_io_access *buf;
};

struct kbase_debug_copy_buffer {
	size_t size;
	struct page **pages;
	bool is_vmalloc;
	unsigned int nr_pages;
	size_t offset;
	struct kbase_mem_phy_alloc *gpu_alloc;

	struct page **extres_pages;
	unsigned int nr_extres_pages;
};

struct kbase_device_info {
	u32 features;
};

struct kbase_mmu_setup {
	u64 transtab;
	u64 memattr;
	u64 transcfg;
};

struct kbase_fault {
	u64 addr;
	u64 extra_addr;
	u32 status;
	bool protected_mode;
};

#define MAX_PAGES_FOR_FREE_PGDS ((size_t)9)

#define MAX_FREE_PGDS ((PAGE_SIZE / sizeof(phys_addr_t)) * MAX_PAGES_FOR_FREE_PGDS)

struct kbase_mmu_table {
	struct mutex mmu_lock;
	phys_addr_t pgd;
	u8 group_id;
	struct kbase_context *kctx;
	union {

		struct {

			u64 levels[MIDGARD_MMU_BOTTOMLEVEL][GPU_PAGE_SIZE / sizeof(u64)];
		} teardown_pages;

		struct {

			phys_addr_t pgds[MAX_FREE_PGDS];

			size_t head_index;
		} free_pgds;
	} scratch_mem;

#if GPU_PAGES_PER_CPU_PAGE > 1
	struct list_head pgd_pages_list;
	struct page *last_allocated_pgd_page;
	struct page *last_freed_pgd_page;
	u32 num_free_pgd_sub_pages;
#endif
};

#if MALI_USE_CSF
#include "csf/mali_kbase_csf_defs.h"
#else
#include "jm/mali_kbase_jm_defs.h"
#endif

#include "mali_kbase_hwaccess_time.h"

static inline int kbase_as_has_bus_fault(struct kbase_as *as, struct kbase_fault *fault)
{
	return (fault == &as->bf_data);
}

static inline int kbase_as_has_page_fault(struct kbase_as *as, struct kbase_fault *fault)
{
	return (fault == &as->pf_data);
}

struct kbasep_mem_device {
	atomic_t used_pages;
};

struct kbase_clk_rate_listener;

typedef void kbase_clk_rate_listener_on_change_t(struct kbase_clk_rate_listener *listener,
						 u32 clk_index, u32 clk_rate_hz);

struct kbase_clk_rate_listener {
	struct list_head node;
	kbase_clk_rate_listener_on_change_t *notify;
};

struct kbase_clk_rate_trace_manager {
	bool gpu_idle;
	struct kbase_clk_data *clks[BASE_MAX_NR_CLOCKS_REGULATORS];
	struct kbase_clk_rate_trace_op_conf *clk_rate_trace_ops;
	struct list_head listeners;
	spinlock_t lock;
};

struct kbase_pm_device_data {
	struct mutex lock;
	int active_count;
	bool suspending;
	bool resuming;
#if MALI_USE_CSF
	bool runtime_active;
#endif
#ifdef CONFIG_MALI_ARBITER_SUPPORT
	atomic_t gpu_lost;
#endif
	wait_queue_head_t zero_active_count_wait;
	wait_queue_head_t resume_wait;

#if MALI_USE_CSF
	u64 debug_core_mask;
#else

	u64 debug_core_mask[BASE_JM_MAX_NR_SLOTS];
	u64 debug_core_mask_all;
#endif

	int (*callback_power_runtime_init)(struct kbase_device *kbdev);
	void (*callback_power_runtime_term)(struct kbase_device *kbdev);
	u32 dvfs_period;
	struct kbase_pm_backend_data backend;
#ifdef CONFIG_MALI_ARBITER_SUPPORT
	struct kbase_arbiter_vm_state *arb_vm_state;
	atomic_t gpu_users_waiting;
#endif
	struct kbase_clk_rate_trace_manager clk_rtm;
};

struct kbase_mem_pool {
	struct kbase_device *kbdev;
	size_t cur_size;
	size_t max_size;
	u8 order;
	u8 group_id;
	spinlock_t pool_lock;
	struct list_head page_list;
	DEFINE_KBASE_SHRINKER reclaim;
	atomic_t isolation_in_progress_cnt;

	struct kbase_mem_pool *next_pool;

	bool dying;
	bool dont_reclaim;
};

struct kbase_mem_pool_group {
	struct kbase_mem_pool small[MEMORY_GROUP_MANAGER_NR_GROUPS];
	struct kbase_mem_pool large[MEMORY_GROUP_MANAGER_NR_GROUPS];
};

struct kbase_mem_pool_config {
	size_t max_size;
};

struct kbase_mem_pool_group_config {
	struct kbase_mem_pool_config small[MEMORY_GROUP_MANAGER_NR_GROUPS];
	struct kbase_mem_pool_config large[MEMORY_GROUP_MANAGER_NR_GROUPS];
};

struct kbase_devfreq_opp {
	u64 opp_freq;
	u64 core_mask;
	u64 real_freqs[BASE_MAX_NR_CLOCKS_REGULATORS];
	u32 opp_volts[BASE_MAX_NR_CLOCKS_REGULATORS];
};

#define KBASE_MMU_MODE_HAS_NON_CACHEABLE (1ul << 0)

struct kbase_mmu_mode {
	void (*update)(struct kbase_device *kbdev, struct kbase_mmu_table *mmut, int as_nr);
	void (*get_as_setup)(struct kbase_mmu_table *mmut, struct kbase_mmu_setup *const setup);
	void (*disable_as)(struct kbase_device *kbdev, int as_nr);
	phys_addr_t (*pte_to_phy_addr)(u64 entry);
	int (*ate_is_valid)(u64 ate, int level);
	int (*pte_is_valid)(u64 pte, int level);
	void (*entry_set_ate)(u64 *entry, struct tagged_addr phy, unsigned long flags, int level);
	void (*entry_set_pte)(u64 *entry, phys_addr_t phy);
	void (*entries_invalidate)(u64 *entry, u32 count);
	unsigned int (*get_num_valid_entries)(u64 *pgd);
	void (*set_num_valid_entries)(u64 *pgd, unsigned int num_of_valid_entries);
	unsigned long flags;
};

struct kbase_mmu_mode const *kbase_mmu_mode_get_aarch64(void);

#define DEVNAME_SIZE 16

#if IS_ENABLED(CONFIG_MALI_MTK_GPU_BM_2)
#ifndef JOB_STATUS_QOS
#define JOB_STATUS_QOS
struct job_status_qos {
	phys_addr_t phyaddr;
	size_t size;
};
#endif
#endif

enum kbase_devfreq_work_type { DEVFREQ_WORK_NONE, DEVFREQ_WORK_SUSPEND, DEVFREQ_WORK_RESUME };

struct kbase_devfreq_queue_info {
	struct workqueue_struct *workq;
	struct work_struct work;
	enum kbase_devfreq_work_type req_type;
	enum kbase_devfreq_work_type acted_type;
};

struct kbase_process {
	pid_t tgid;
	size_t total_gpu_pages;
	struct list_head kctx_list;

	struct rb_node kprcs_node;
	struct rb_root dma_buf_root;
};

struct kbase_mem_migrate {
	struct list_head free_pages_list;
	spinlock_t free_pages_lock;
	struct workqueue_struct *free_pages_workq;
	struct work_struct free_pages_work;
#if (KERNEL_VERSION(6, 0, 0) > LINUX_VERSION_CODE)
	struct inode *inode;
#endif
};

#if IS_ENABLED(CONFIG_MALI_MTK_UNHANDLED_PAGE_FAULT_DEBUG)
#define MMU_DEBUG_INFO_BUFFER_SIZE 4096

enum kbase_mmu_dbg_op_type {
	MMU_OP_MAP = 0,
	MMU_OP_UNMAP = 1,
};

struct kbase_mmu_debug_info {
	u64 time;
	size_t pgds;
	u64 va;
	pid_t tgid;
	u32 id;
	int as_nr;
	bool ipm;
	enum kbase_mmu_dbg_op_type mmu_op_type;
};
#endif

#if IS_ENABLED(CONFIG_MALI_MTK_TRIGGER_KE)
enum exception_type {
	EXCEPTION_BIT_STUCK = 0,
	EXCEPTION_PM_TIMED_OUT = 1,
	EXCEPTION_RESET_FAILED = 2,
};
#endif

#if IS_ENABLED(CONFIG_MALI_MTK_KBASE_MMU_DBG_LOG)
enum mmu_dbg_log_config {
	MMU_DBG_CFG_LOG_DIS = 0,
	MMU_DBG_CFG_LOG_EN = 1,
	MMU_DBG_CFG_LOG_BT_EN = 2,
};
#endif

struct kbase_device {
	u32 hw_quirks_sc;
	u32 hw_quirks_tiler;
	u32 hw_quirks_mmu;
	u32 hw_quirks_gpu;
#if MALI_USE_CSF
	u32 hw_quirks_ne;
#endif

	struct list_head entry;
	struct device *dev;
	struct miscdevice mdev;
	u64 reg_start;
	size_t reg_size;
	void __iomem *reg;
#if IS_ENABLED(CONFIG_MALI_MTK_GPUEB_IRQ)
	int gpueb_irq;
	u64 low_volt_count;
	u64 brcast_timeout_count;
#endif
	struct {
		void __iomem **regs;
		u32 *flags;
		size_t size;
	} regmap;
	struct {
		u32 irq;
		u32 flags;
	} irqs[3];
	u32 nr_irqs;

	struct clk *clocks[BASE_MAX_NR_CLOCKS_REGULATORS];
	unsigned int nr_clocks;
#if IS_ENABLED(CONFIG_REGULATOR)
	struct regulator *regulators[BASE_MAX_NR_CLOCKS_REGULATORS];
	unsigned int nr_regulators;
#if (KERNEL_VERSION(6, 0, 0) <= LINUX_VERSION_CODE)
	int token;
#elif (KERNEL_VERSION(4, 10, 0) <= LINUX_VERSION_CODE)
	struct opp_table *opp_table;
#endif
#endif
	char devname[DEVNAME_SIZE];
	u32 id;

#if !IS_ENABLED(CONFIG_MALI_REAL_HW)
	void *model;
	struct kmem_cache *irq_slab;
	struct workqueue_struct *irq_workq;
	atomic_t serving_job_irq;
	atomic_t serving_gpu_irq;
	atomic_t serving_mmu_irq;
	spinlock_t reg_op_lock;
#endif
	struct kbase_pm_device_data pm;

	struct kbase_mem_pool_group mem_pools;
	struct kbasep_mem_device memdev;
	struct kbase_mmu_mode const *mmu_mode;

	struct memory_group_manager_device *mgm_dev;

	bool mmu_unresponsive;
	struct kbase_as as[BASE_MAX_NR_AS];
	struct kbase_context *as_to_kctx[BASE_MAX_NR_AS];
	u16 as_free;

	spinlock_t mmu_mask_change;

	struct kbase_gpu_props gpu_props;

	unsigned long hw_issues_mask[(BASE_HW_ISSUE_END + BITS_PER_LONG - 1) / BITS_PER_LONG];
	unsigned long hw_features_mask[(BASE_HW_FEATURE_END + BITS_PER_LONG - 1) / BITS_PER_LONG];

	struct {
		atomic_t count;
		atomic_t state;
	} disjoint_event;

	s8 nr_hw_address_spaces;
	s8 nr_user_address_spaces;

	u8 pbha_propagate_bits;

#if MALI_USE_CSF
	struct kbase_hwcnt_backend_csf_if hwcnt_backend_csf_if_fw;
#else
	struct kbase_hwcnt {
		spinlock_t lock;

		struct kbase_context *kctx;
		u64 addr;
		u64 addr_bytes;

		struct kbase_instr_backend backend;
	} hwcnt;

	struct kbase_hwcnt_backend_interface hwcnt_gpu_jm_backend;
#endif

	struct kbase_hwcnt_backend_interface hwcnt_gpu_iface;
	struct kbase_hwcnt_watchdog_interface hwcnt_watchdog_timer;

	struct kbase_hwcnt_context *hwcnt_gpu_ctx;
	struct kbase_hwcnt_virtualizer *hwcnt_gpu_virt;
	struct kbase_kinstr_prfcnt_context *kinstr_prfcnt_ctx;

	atomic_t timeline_flags;
	struct kbase_timeline *timeline;

#if KBASE_KTRACE_TARGET_RBUF
	struct kbase_ktrace ktrace;
#endif
	u32 reset_timeout_ms;

	u64 lowest_gpu_freq_khz;

	struct kbase_backend_time backend_time;

#if IS_ENABLED(CONFIG_MALI_MTK_GPU_IDLE_STRESS_TEST) && IS_ENABLED(CONFIG_MALI_MTK_API_SYNC_UPDATE)
	bool api_sync_update_in_progress;
	int temp_api_sync_flag;
	int final_api_sync_flag;
	int api_sync_level;
	bool api_sync_force_reset;
	bool api_sync_restore_always_on;
	unsigned int api_sync_timeout_ms;
	struct hrtimer api_sync_timer;
	int api_sync_debug_level;
#endif

	bool cache_clean_in_progress;
	u32 cache_clean_queued;
	wait_queue_head_t cache_clean_wait;

	void *platform_context;

	struct list_head kctx_list;
	struct mutex kctx_list_lock;

#ifdef CONFIG_MALI_DEVFREQ
	struct devfreq_dev_profile devfreq_profile;
	struct devfreq *devfreq;
	unsigned long current_freqs[BASE_MAX_NR_CLOCKS_REGULATORS];
	unsigned long current_nominal_freq;
	unsigned long current_voltages[BASE_MAX_NR_CLOCKS_REGULATORS];
	u64 current_core_mask;
	struct kbase_devfreq_opp *devfreq_table;
	unsigned int num_opps;
	struct kbasep_pm_metrics last_devfreq_metrics;
	struct kbase_devfreq_queue_info devfreq_queue;

#if IS_ENABLED(CONFIG_DEVFREQ_THERMAL)
	struct thermal_cooling_device *devfreq_cooling;
	bool ipa_protection_mode_switched;
	struct {

		struct mutex lock;
		struct kbase_ipa_model *configured_model;
		struct kbase_ipa_model *fallback_model;

		struct kbasep_pm_metrics last_metrics;

		bool force_fallback_model;

		ktime_t last_sample_time;
	} ipa;
#endif
#endif
	unsigned long previous_frequency;

#if !MALI_USE_CSF
	atomic_t job_fault_debug;
#endif

#if IS_ENABLED(CONFIG_DEBUG_FS)
	struct dentry *mali_debugfs_directory;
	struct dentry *debugfs_ctx_directory;
	struct dentry *debugfs_instr_directory;

#ifdef CONFIG_MALI_DEBUG
	u64 debugfs_as_read_bitmap;
#endif

#if !MALI_USE_CSF
	wait_queue_head_t job_fault_wq;
	wait_queue_head_t job_fault_resume_wq;
	struct workqueue_struct *job_fault_resume_workq;
	struct list_head job_fault_event_list;
	spinlock_t job_fault_event_lock;
#endif

#if !MALI_CUSTOMER_RELEASE
	struct {
		u32 reg_offset;
	} regs_dump_debugfs_data;
#endif
#endif

	atomic_t ctx_num;

#if IS_ENABLED(CONFIG_DEBUG_FS)
	struct kbase_io_history io_history;
#endif

	struct kbase_hwaccess_data hwaccess;

	atomic_t faults_pending;

	bool mmu_page_migrate_in_progress;
	bool poweroff_pending;

	bool infinite_cache_active_default;

	struct kbase_mem_pool_group_config mem_pool_defaults;

	u32 current_gpu_coherency_mode;
	u32 system_coherency;

	bool cci_snoop_enabled;

	u32 snoop_enable_smc;
	u32 snoop_disable_smc;

	const struct protected_mode_ops *protected_ops;

	struct protected_mode_device *protected_dev;

	bool protected_mode;

	bool protected_mode_transition;

	bool protected_mode_hwcnt_desired;

	bool protected_mode_hwcnt_disabled;

	struct work_struct protected_mode_hwcnt_disable_work;

	bool irq_reset_flush;

	u32 inited_subsys;

	spinlock_t hwaccess_lock;

	struct mutex mmu_hw_mutex;

	u8 l2_size_override;
	u8 l2_hash_override;
	bool l2_hash_values_override;
	u32 l2_hash_values[GPU_L2_SLICE_HASH_COUNT];

	u32 sysc_alloc[GPU_SYSC_ALLOC_COUNT];

	struct mutex fw_load_lock;
#if MALI_USE_CSF

	struct kbase_csf_device csf;
#else
	struct kbasep_js_device_data js_data;

	u32 js_ctx_scheduling_mode;

	u8 serialize_jobs;

#ifdef CONFIG_MALI_CINSTR_GWT
	u8 backup_serialize_jobs;
#endif

#endif

	struct rb_root process_root;
	struct rb_root dma_buf_root;

	size_t total_gpu_pages;
	struct mutex dma_buf_lock;
	spinlock_t gpu_mem_usage_lock;

	struct {
		struct kbase_context *kctx;
		u64 jc;
		u32 slot;
		u64 flags;
	} dummy_job_wa;
	bool dummy_job_wa_loaded;

#ifdef CONFIG_MALI_ARBITER_SUPPORT
	struct kbase_arbiter_device arb;
#endif

	struct priority_control_manager_device *pcm_dev;

	struct notifier_block oom_notifier_block;

#if IS_ENABLED(CONFIG_MALI_MTK_GPU_BM_2)
	struct job_status_qos job_status_addr;
	struct v1_data* v1;
#endif
#if IS_ENABLED(CONFIG_MALI_MTK_LOG_BUFFER)
	struct mtk_logbuffer_info logbuf_regular;
	struct mtk_logbuffer_info logbuf_critical;
	struct mtk_logbuffer_info logbuf_exception;
	struct mtk_logbuffer_info logbuf_deferred;
#endif

	struct kbase_mem_migrate mem_migrate;

#if IS_ENABLED(CONFIG_MALI_MTK_UNHANDLED_PAGE_FAULT_DEBUG)
	struct mutex register_check_lock;
	struct kbase_mmu_debug_info mmu_dbg[MMU_DEBUG_INFO_BUFFER_SIZE];
	struct mutex mmu_debug_info_lock;
	size_t mmu_debug_info_head;
#endif

#if IS_ENABLED(CONFIG_MALI_MTK_JIT_RECLAIM_ANTITHRASHING)
	u32 jit_reclaim_timeout_ms;
#endif

#if IS_ENABLED(CONFIG_MALI_MTK_TRIGGER_KE)
	u32 exception_mask;
#endif

#if IS_ENABLED(CONFIG_MALI_MTK_KBASE_MMU_DBG_LOG)
	u32 mmu_dbg_config_value;
#endif

#if IS_ENABLED(CONFIG_MALI_MTK_MEMORY_DEBUG)
	int memory_debug_mode;
#endif

#if IS_ENABLED(CONFIG_MALI_MTK_WHITEBOX_MEMORY_FOOTPRINT)
	bool mem_whitebox_debug;
#endif

#if IS_ENABLED(CONFIG_MALI_MTK_GPU_RESET_DEBUG)
	u32 reset_exception_mask;
#endif

#if IS_ENABLED(CONFIG_MALI_MTK_GOV_CORE_MASK_DEBUG)
	u32 gov_core_mask_disable;
#endif

#if MALI_USE_CSF && IS_ENABLED(CONFIG_SYNC_FILE)
	atomic_t live_fence_metadata;
#endif
	struct kmem_cache *va_region_slab;
	struct kmem_cache *page_metadata_slab;

#if IS_ENABLED(CONFIG_MALI_TRACE_POWER_GPU_WORK_PERIOD)

	struct kbase_gpu_metrics gpu_metrics;
#endif
#if MALI_USE_CSF
	atomic_t fence_signal_timeout_enabled;
#endif

	struct notifier_block pcm_prioritized_process_nb;

#if IS_ENABLED(CONFIG_MALI_MTK_FENCE_DEBUG)
	ktime_t scheduler_kthread_exec_begin_time;
	ktime_t scheduler_kthread_exec_end_time;
#endif

#if IS_ENABLED(CONFIG_MALI_MTK_ACP_FORCE_SYNC_DEBUG)
	u32 acp_dbg_force_sync;
#endif
};

enum kbase_file_state {
	KBASE_FILE_NEED_VSN,
	KBASE_FILE_VSN_IN_PROGRESS,
	KBASE_FILE_NEED_CTX,
	KBASE_FILE_CTX_IN_PROGRESS,
	KBASE_FILE_COMPLETE
};

struct kbase_file {
	struct kbase_device *kbdev;
	struct file *filp;
	struct kbase_context *kctx;
	unsigned long api_version;
	atomic_t setup_state;
};
#if MALI_JIT_PRESSURE_LIMIT_BASE

enum kbase_context_flags {
	KCTX_COMPAT = 1U << 0,
	KCTX_RUNNABLE_REF = 1U << 1,
	KCTX_ACTIVE = 1U << 2,
	KCTX_PULLED = 1U << 3,
	KCTX_MEM_PROFILE_INITIALIZED = 1U << 4,
	KCTX_INFINITE_CACHE = 1U << 5,
	KCTX_SUBMIT_DISABLED = 1U << 6,
	KCTX_PRIVILEGED = 1U << 7,
	KCTX_SCHEDULED = 1U << 8,
	KCTX_DYING = 1U << 9,
	KCTX_FORCE_SAME_VA = 1U << 11,
	KCTX_PULLED_SINCE_ACTIVE_JS0 = 1U << 12,
	KCTX_PULLED_SINCE_ACTIVE_JS1 = 1U << 13,
	KCTX_PULLED_SINCE_ACTIVE_JS2 = 1U << 14,
	KCTX_AS_DISABLED_ON_FAULT = 1U << 15,
	KCTX_JPL_ENABLED = 1U << 16,
	KCTX_PAGE_FAULT_REPORT_SKIP = 1U << 17,
};
#else

enum kbase_context_flags {
	KCTX_COMPAT = 1U << 0,
	KCTX_RUNNABLE_REF = 1U << 1,
	KCTX_ACTIVE = 1U << 2,
	KCTX_PULLED = 1U << 3,
	KCTX_MEM_PROFILE_INITIALIZED = 1U << 4,
	KCTX_INFINITE_CACHE = 1U << 5,
	KCTX_SUBMIT_DISABLED = 1U << 6,
	KCTX_PRIVILEGED = 1U << 7,
	KCTX_SCHEDULED = 1U << 8,
	KCTX_DYING = 1U << 9,
	KCTX_FORCE_SAME_VA = 1U << 11,
	KCTX_PULLED_SINCE_ACTIVE_JS0 = 1U << 12,
	KCTX_PULLED_SINCE_ACTIVE_JS1 = 1U << 13,
	KCTX_PULLED_SINCE_ACTIVE_JS2 = 1U << 14,
	KCTX_AS_DISABLED_ON_FAULT = 1U << 15,
	KCTX_PAGE_FAULT_REPORT_SKIP = 1U << 16,
};
#endif

struct kbase_sub_alloc {
	struct list_head link;
	struct page *page;
	DECLARE_BITMAP(sub_pages, NUM_PAGES_IN_2MB_LARGE_PAGE);
};

#if IS_ENABLED(CONFIG_MALI_MTK_MEMORY_DEBUG)
enum kbase_memory_category {
	KBASE_MEM_API,
	KBASE_MEM_GROW,
	KBASE_MEM_JIT,
	KBASE_MEM_MMU,
	KBASE_MEM_TILER,
	KBASE_MEM_LABEL_COUNT,
	KBASE_MEM_CONTEXT = KBASE_MEM_LABEL_COUNT,
	KBASE_MEM_JM,
	KBASE_MEM_UNKNOWN,
	KBASE_MEM_COUNT
};

#define MAX_PROCESS_NAME_LEN        128
#endif

struct kbase_context {
	struct file *filp;
	struct kbase_device *kbdev;
	struct list_head kctx_list_link;
	struct kbase_mmu_table mmu;

	u32 id;
	unsigned long api_version;
	struct list_head event_list;
	struct list_head event_coalesce_list;
	struct mutex event_mutex;
#if !MALI_USE_CSF
	atomic_t event_closed;
#endif
	struct workqueue_struct *event_workq;
	atomic_t event_count;
	int event_coalesce_count;

	atomic_t flags;

	struct tagged_addr aliasing_sink_page;

	spinlock_t mem_partials_lock;
	struct list_head mem_partials;

	struct mutex reg_lock;
#if MALI_USE_CSF
	atomic64_t num_fixable_allocs;
	atomic64_t num_fixed_allocs;
#endif
	struct kbase_reg_zone reg_zone[CONTEXT_ZONE_MAX];

#if MALI_USE_CSF
	struct kbase_csf_context csf;
#else
	struct kbase_jd_context jctx;
	struct jsctx_queue jsctx_queue[KBASE_JS_ATOM_SCHED_PRIO_COUNT][BASE_JM_MAX_NR_SLOTS];
	struct kbase_jsctx_slot_tracking slot_tracking[BASE_JM_MAX_NR_SLOTS];
	atomic_t atoms_pulled_all_slots;

	struct list_head completed_jobs;
	atomic_t work_count;
	struct timer_list soft_job_timeout;

	int priority;
	s16 atoms_count[KBASE_JS_ATOM_SCHED_PRIO_COUNT];
	u32 slots_pullable;
	u32 age_count;
#endif

	DECLARE_BITMAP(cookies, BITS_PER_LONG);
	struct kbase_va_region *pending_regions[BITS_PER_LONG];

	wait_queue_head_t event_queue;
	pid_t tgid;
	pid_t pid;
	atomic_t prioritized;
	atomic_t used_pages;
	atomic_t nonmapped_pages;
	atomic_t permanent_mapped_pages;
#if IS_ENABLED(CONFIG_MALI_MTK_MEMORY_DEBUG)
	bool target_mem_profiling;
	char process_name[MAX_PROCESS_NAME_LEN];
	atomic_t used_pages_categories[KBASE_MEM_LABEL_COUNT];
#endif

	struct kbase_mem_pool_group mem_pools;

	DEFINE_KBASE_SHRINKER reclaim;

	struct list_head evict_list;
	atomic_t evict_nents;

	struct list_head waiting_soft_jobs;
	spinlock_t waiting_soft_jobs_lock;

	int as_nr;

	atomic_t refcount;

	struct mm_struct *process_mm;
	u64 gpu_va_end;
#if MALI_USE_CSF
	u32 running_total_tiler_heap_nr_chunks;
	u64 running_total_tiler_heap_memory;
	u64 peak_total_tiler_heap_memory;
#endif
	bool jit_va;

#if IS_ENABLED(CONFIG_DEBUG_FS)
	char *mem_profile_data;
	size_t mem_profile_size;
	struct mutex mem_profile_lock;
	struct dentry *kctx_dentry;

	unsigned int *reg_dump;
	atomic_t job_fault_count;
	struct list_head job_fault_resume_event_list;
	unsigned int mem_view_column_width;

	struct work_struct *job_fault_work;
#endif
	struct kbase_va_region *jit_alloc[1 + BASE_JIT_ALLOC_COUNT];
	u8 jit_max_allocations;
	u8 jit_current_allocations;
	u8 jit_current_allocations_per_bin[256];
	u8 jit_group_id;
#if MALI_JIT_PRESSURE_LIMIT_BASE
	u64 jit_phys_pages_limit;
	u64 jit_current_phys_pressure;
	u64 jit_phys_pages_to_be_allocated;
#endif
	struct list_head jit_active_head;
	struct list_head jit_pool_head;
	struct list_head jit_destroy_head;
	struct mutex jit_evict_lock;
	struct work_struct jit_work;

	struct list_head ext_res_meta_head;

	u8 trim_level;

	struct kbase_process *kprcs;
	struct list_head kprcs_link;

#ifdef CONFIG_MALI_CINSTR_GWT
	bool gwt_enabled;
	bool gwt_was_enabled;
	struct list_head gwt_current_list;
	struct list_head gwt_snapshot_list;
#endif

	base_context_create_flags create_flags;

#if !MALI_USE_CSF
	struct kbase_kinstr_jm *kinstr_jm;
#endif
	struct list_head tl_kctx_list_node;

	u64 limited_core_mask;

#if !MALI_USE_CSF
	void *platform_data;
#endif

	struct task_struct *task;

#if IS_ENABLED(CONFIG_MALI_TRACE_POWER_GPU_WORK_PERIOD)

	struct kbase_gpu_metrics_ctx *gpu_metrics_ctx;
#endif

#if IS_ENABLED(CONFIG_MALI_MTK_UNHANDLED_PAGE_FAULT_DEBUG)
	char group_leader_comm[TASK_COMM_LEN];
#endif

#if IS_ENABLED(CONFIG_MALI_MTK_FENCE_DEBUG)
	uint64_t notification_fd_signal_time;
	uint64_t notification_data_read_time;
	uint64_t notification_polling_start_time;
#endif

	char comm[TASK_COMM_LEN];
};

#ifdef CONFIG_MALI_CINSTR_GWT

struct kbasep_gwt_list_element {
	struct list_head link;
	struct kbase_va_region *region;
	u64 page_addr;
	u64 num_pages;
};

#endif

struct kbase_ctx_ext_res_meta {
	struct list_head ext_res_node;
	struct kbase_va_region *reg;
	u32 ref;
};

enum kbase_reg_access_type { REG_READ, REG_WRITE };

enum kbase_share_attr_bits {

	SHARE_BOTH_BITS = (2ULL << 8),
	SHARE_INNER_BITS = (3ULL << 8)
};

static inline bool kbase_device_is_cpu_coherent(struct kbase_device *kbdev)
{
	if ((kbdev->system_coherency == COHERENCY_ACE_LITE) ||
	    (kbdev->system_coherency == COHERENCY_ACE))
		return true;

	return false;
}

static inline u64 kbase_get_lock_region_min_size_log2(struct kbase_gpu_props const *gpu_props)
{
	if (gpu_props->gpu_id.product_model >= GPU_ID_MODEL_MAKE(12, 0))
		return 12;

	return 15;
}

#define HR_TIMER_DELAY_MSEC(x) (ns_to_ktime(((u64)(x)) * 1000000U))
#define HR_TIMER_DELAY_NSEC(x) (ns_to_ktime(x))

#endif

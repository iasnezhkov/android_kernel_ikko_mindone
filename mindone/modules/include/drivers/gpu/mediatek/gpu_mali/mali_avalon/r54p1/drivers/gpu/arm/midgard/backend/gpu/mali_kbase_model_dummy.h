/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_MODEL_DUMMY_H_
#define _KBASE_MODEL_DUMMY_H_

#include <uapi/gpu/arm/midgard/backend/gpu/mali_kbase_model_linux.h>
#include <uapi/gpu/arm/midgard/backend/gpu/mali_kbase_model_dummy.h>

#define model_error_log(module, ...) pr_err(__VA_ARGS__)

#define NUM_SLOTS 4

#define KBASE_JOB_INTERRUPTED (1 << 0)
#define KBASE_JOB_STOPPED (1 << 1)
#define KBASE_JOB_TERMINATED (1 << 2)

#define KBASE_JOB_CONFIG_FAULT (1 << 3)
#define KBASE_JOB_POWER_FAULT (1 << 4)
#define KBASE_JOB_READ_FAULT (1 << 5)
#define KBASE_JOB_WRITE_FAULT (1 << 6)
#define KBASE_JOB_AFFINITY_FAULT (1 << 7)
#define KBASE_JOB_BUS_FAULT (1 << 8)
#define KBASE_INSTR_INVALID_PC (1 << 9)
#define KBASE_INSTR_INVALID_ENC (1 << 10)
#define KBASE_INSTR_TYPE_MISMATCH (1 << 11)
#define KBASE_INSTR_OPERAND_FAULT (1 << 12)
#define KBASE_INSTR_TLS_FAULT (1 << 13)
#define KBASE_INSTR_BARRIER_FAULT (1 << 14)
#define KBASE_INSTR_ALIGN_FAULT (1 << 15)
#define KBASE_DATA_INVALID_FAULT (1 << 16)
#define KBASE_TILE_RANGE_FAULT (1 << 17)
#define KBASE_ADDR_RANGE_FAULT (1 << 18)
#define KBASE_OUT_OF_MEMORY (1 << 19)
#define KBASE_UNKNOWN (1 << 20)

#define KBASE_DELAYED_BUS_FAULT (1 << 21)
#define KBASE_SHAREABILITY_FAULT (1 << 22)

#define KBASE_TRANSLATION_FAULT (1 << 23)
#define KBASE_PERMISSION_FAULT (1 << 24)
#define KBASE_TRANSTAB_BUS_FAULT (1 << 25)
#define KBASE_ACCESS_FLAG (1 << 26)

#define IS_A_JOB_ERROR ((KBASE_UNKNOWN << 1) - KBASE_JOB_INTERRUPTED)
#define IS_A_MMU_ERROR ((KBASE_ACCESS_FLAG << 1) - KBASE_TRANSLATION_FAULT)
#define IS_A_GPU_ERROR (KBASE_DELAYED_BUS_FAULT | KBASE_SHAREABILITY_FAULT)

#define NUM_MMU_AS \
	16

struct kbase_device;

struct kbase_error_params {
	u64 jc;
	u32 errors_mask;
	u32 mmu_table_level;
	u16 faulty_mmu_as;
	u16 padding[3];
};

enum kbase_model_control_command {

	KBASE_MC_DISABLE_JOBS
};

struct kbase_model_control_params {
	s32 command;
	s32 value;
};

struct kbase_error_atom {
	struct kbase_error_params params;
	struct kbase_error_atom *next;
};

struct error_status_t {
	spinlock_t access_lock;

	u32 errors_mask;
	u32 mmu_table_level;
	u32 faulty_mmu_as;

	u64 current_jc;
	u32 current_job_slot;

	u32 job_irq_rawstat;
	u32 job_irq_status;
	u32 js_status[NUM_SLOTS];

	u32 mmu_irq_mask;
	u32 mmu_irq_rawstat;

	u32 gpu_error_irq;
	u32 gpu_fault_status;

	u32 as_faultstatus[NUM_MMU_AS];
	u32 as_command[NUM_MMU_AS];
	u64 as_transtab[NUM_MMU_AS];
};

struct gpu_model_prfcnt_en {
	u32 fe;
	u32 tiler;
	u32 l2;
	u32 shader;
};

void midgard_set_error(u32 job_slot);
int job_atom_inject_error(struct kbase_error_params *params);
int gpu_model_control(void *h, struct kbase_model_control_params *params);

int gpu_model_set_dummy_prfcnt_user_sample(u32 __user *data, u32 size);

void gpu_model_set_dummy_prfcnt_kernel_sample(u64 *data, u32 size);

void gpu_model_get_dummy_prfcnt_cores(struct kbase_device *kbdev, u64 *l2_present,
				      u64 *shader_present);
void gpu_model_set_dummy_prfcnt_cores(struct kbase_device *kbdev, u64 l2_present,
				      u64 shader_present);

void gpu_model_clear_prfcnt_values(void);

#if MALI_USE_CSF

void gpu_model_prfcnt_dump_request(uint32_t *sample_buf, struct gpu_model_prfcnt_en enable_maps);

void gpu_model_glb_request_job_irq(void *model);
#endif

extern struct error_status_t hw_error_status;

#endif

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_KINSTR_PRFCNT_H_
#define _KBASE_KINSTR_PRFCNT_H_

#include "hwcnt/mali_kbase_hwcnt_types.h"
#include <uapi/gpu/arm/midgard/mali_kbase_hwcnt_reader.h>

struct kbase_kinstr_prfcnt_context;
struct kbase_kinstr_prfcnt_client;
struct kbase_hwcnt_virtualizer;
struct kbase_ioctl_hwcnt_reader_setup;
struct kbase_ioctl_kinstr_prfcnt_enum_info;
union kbase_ioctl_kinstr_prfcnt_setup;

typedef enum {
	pm_non,
	pm_ltr,
	pm_swpm,
	pm_met
} mtk_pm_tool_used;

int kbase_kinstr_prfcnt_init(struct kbase_hwcnt_virtualizer *hvirt,
			     struct kbase_kinstr_prfcnt_context **out_kinstr_ctx);

void kbase_kinstr_prfcnt_term(struct kbase_kinstr_prfcnt_context *kinstr_ctx);

void kbase_kinstr_prfcnt_suspend(struct kbase_kinstr_prfcnt_context *kinstr_ctx);

void kbase_kinstr_prfcnt_resume(struct kbase_kinstr_prfcnt_context *kinstr_ctx);

int kbasep_kinstr_prfcnt_get_block_info_list(const struct kbase_hwcnt_metadata *metadata,
					     size_t block_set, struct prfcnt_enum_item *item_arr,
					     size_t *arr_idx);

size_t kbasep_kinstr_prfcnt_get_sample_md_count(const struct kbase_hwcnt_metadata *metadata,
						struct kbase_hwcnt_enable_map *enable_map);

int kbasep_kinstr_prfcnt_set_block_meta_items(struct kbase_hwcnt_enable_map *enable_map,
					      struct kbase_hwcnt_dump_buffer *dst,
					      struct prfcnt_metadata **block_meta_base,
					      u8 *base_addr, u8 counter_set);

int kbasep_kinstr_prfcnt_client_create(struct kbase_kinstr_prfcnt_context *kinstr_ctx,
				       union kbase_ioctl_kinstr_prfcnt_setup *setup,
				       struct kbase_kinstr_prfcnt_client **out_vcli,
				       struct prfcnt_request_item *req_arr);

int kbasep_kinstr_prfcnt_cmd(struct kbase_kinstr_prfcnt_client *cli,
			     struct prfcnt_control_cmd *control_cmd);

void kbasep_kinstr_prfcnt_client_destroy(struct kbase_kinstr_prfcnt_client *cli);

int kbase_kinstr_prfcnt_enum_info(struct kbase_kinstr_prfcnt_context *kinstr_ctx,
				  struct kbase_ioctl_kinstr_prfcnt_enum_info *enum_info);

int kbase_kinstr_prfcnt_setup(struct kbase_kinstr_prfcnt_context *kinstr_ctx,
			      union kbase_ioctl_kinstr_prfcnt_setup *setup);

int kbasep_kinstr_populate_prfcnt_enum_list(const struct kbase_hwcnt_metadata *metadata,
					    struct prfcnt_enum_item *item_array, size_t array_size);

int MTK_kbase_vinstr_hwcnt_reader_setup(
	struct kbase_kinstr_prfcnt_context *kinstr_ctx,
	union kbase_ioctl_kinstr_prfcnt_setup *setup);
void MTK_update_mtk_pm(int flag);
int MTK_get_mtk_pm(void);
void MTK_kbasep_vinstr_hwcnt_set_interval(unsigned int interval);
void MTK_kbasep_vinstr_hwcnt_release(void);
void MTK_update_gpu_LTR(void);

#if IS_ENABLED(CONFIG_MALI_MTK_HWCNT_HINT)
void hwcnt_hint(bool is_init);
#endif

#endif

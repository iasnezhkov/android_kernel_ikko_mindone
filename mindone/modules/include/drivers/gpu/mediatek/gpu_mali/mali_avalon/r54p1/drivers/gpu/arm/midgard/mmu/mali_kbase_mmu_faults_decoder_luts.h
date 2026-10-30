/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MALI_KBASE_MMU_FAULTS_DECODER_LUTS_H_
#define _MALI_KBASE_MMU_FAULTS_DECODER_LUTS_H_

#include <linux/types.h>

const char *decode_fault_source_core_id_t_desc(u16 idx, u32 gpu_id);

const char *decode_fault_source_core_id_t_core_type(u16 idx, u32 gpu_id);

const char *decode_fault_source_core_type_t_name(u16 idx, u32 gpu_id);

const char *decode_fault_source_core_type_t_desc(u16 idx, u32 gpu_id);

const char *decode_fault_source_shader_r_t(u16 idx, u32 gpu_id);

const char *decode_fault_source_shader_w_t(u16 idx, u32 gpu_id);

const char *decode_fault_source_tiler_r_t(u16 idx, u32 gpu_id);

const char *decode_fault_source_tiler_w_t(u16 idx, u32 gpu_id);

#endif

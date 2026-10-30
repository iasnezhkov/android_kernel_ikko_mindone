/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_GPU_FAULT_H_
#define _KBASE_GPU_FAULT_H_

const char *kbase_gpu_exception_name(u32 exception_code);

const char *kbase_gpu_access_type_name(u32 fault_status);

#endif

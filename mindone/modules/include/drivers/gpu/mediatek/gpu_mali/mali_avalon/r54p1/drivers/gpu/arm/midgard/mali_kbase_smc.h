/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_SMC_H_
#define _KBASE_SMC_H_

#if IS_ENABLED(CONFIG_ARM64)

#include <linux/types.h>

#define SMC_FAST_CALL (1U << 31)
#define SMC_64 (1U << 30)

#define SMC_OEN_OFFSET 24
#define SMC_OEN_MASK (0x3FU << SMC_OEN_OFFSET)
#define SMC_OEN_SIP (2 << SMC_OEN_OFFSET)
#define SMC_OEN_STD (4 << SMC_OEN_OFFSET)

u64 kbase_invoke_smc_fid(u32 fid, u64 arg0, u64 arg1, u64 arg2);

u64 kbase_invoke_smc(u32 oen, u16 function_number, bool smc64, u64 arg0, u64 arg1, u64 arg2);

#endif

#endif

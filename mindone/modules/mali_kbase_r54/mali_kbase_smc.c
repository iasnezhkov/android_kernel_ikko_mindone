// SPDX-License-Identifier: GPL-2.0
#if IS_ENABLED(CONFIG_ARM64)

#include <mali_kbase.h>
#include <mali_kbase_smc.h>

#include <linux/compiler.h>

#ifndef __asmeq

#define __asmeq(x, y) ".ifnc " x "," y " ; .err ; .endif\n\t"
#endif

static noinline u64 invoke_smc_fid(u64 function_id, u64 arg0, u64 arg1, u64 arg2)
{
	register u64 x0 asm("x0") = function_id;
	register u64 x1 asm("x1") = arg0;
	register u64 x2 asm("x2") = arg1;
	register u64 x3 asm("x3") = arg2;

	asm volatile(__asmeq("%0", "x0") __asmeq("%1", "x1") __asmeq("%2", "x2")
			     __asmeq("%3", "x3") "smc    #0\n"
		     : "+r"(x0)
		     : "r"(x1), "r"(x2), "r"(x3));

	return x0;
}

u64 kbase_invoke_smc_fid(u32 fid, u64 arg0, u64 arg1, u64 arg2)
{

	KBASE_DEBUG_ASSERT(fid & ~SMC_FAST_CALL);

	KBASE_DEBUG_ASSERT((fid & (0xFF << 16)) == 0);

	return invoke_smc_fid(fid, arg0, arg1, arg2);
}

u64 kbase_invoke_smc(u32 oen, u16 function_number, bool smc64, u64 arg0, u64 arg1, u64 arg2)
{
	u32 fid = 0;

	KBASE_DEBUG_ASSERT((oen & ~SMC_OEN_MASK) == 0);

	fid |= SMC_FAST_CALL;
	if (smc64)
		fid |= SMC_64;
	fid |= oen;

	fid |= (function_number);

	return kbase_invoke_smc_fid(fid, arg0, arg1, arg2);
}

#endif

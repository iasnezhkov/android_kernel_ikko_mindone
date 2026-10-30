// SPDX-License-Identifier: GPL-2.0
#include <mali_kbase.h>
#include <mali_kbase_defs.h>
#include <gpu/mali_kbase_gpu_fault.h>

const char *kbase_gpu_access_type_name(u32 fault_status)
{
	switch (AS_FAULTSTATUS_ACCESS_TYPE_GET(fault_status)) {
	case AS_FAULTSTATUS_ACCESS_TYPE_ATOMIC:
		return "ATOMIC";
	case AS_FAULTSTATUS_ACCESS_TYPE_READ:
		return "READ";
	case AS_FAULTSTATUS_ACCESS_TYPE_WRITE:
		return "WRITE";
	case AS_FAULTSTATUS_ACCESS_TYPE_EXECUTE:
		return "EXECUTE";
	default:
		WARN_ON(1);
		return NULL;
	}
}

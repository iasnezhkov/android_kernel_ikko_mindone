// SPDX-License-Identifier: GPL-2.0
#include <mali_kbase_debug.h>
#include <mali_kbase_linux.h>

static struct kbasep_debug_assert_cb kbasep_debug_assert_registered_cb = { NULL, NULL };

void kbase_debug_assert_register_hook(kbase_debug_assert_hook *func, void *param)
{
	kbasep_debug_assert_registered_cb.func = func;
	kbasep_debug_assert_registered_cb.param = param;
}

void kbasep_debug_assert_call_hook(void)
{
	if (kbasep_debug_assert_registered_cb.func != NULL)
		kbasep_debug_assert_registered_cb.func(kbasep_debug_assert_registered_cb.param);
}
KBASE_EXPORT_SYMBOL(kbasep_debug_assert_call_hook);

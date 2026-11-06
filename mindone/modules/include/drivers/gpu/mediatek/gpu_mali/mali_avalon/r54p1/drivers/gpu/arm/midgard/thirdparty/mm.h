/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024 MediaTek Inc.
 * Copyright 2018 The Hafnium Authors.
 */
#include <linux/version.h>

#if (KERNEL_VERSION(4, 20, 0) > LINUX_VERSION_CODE)

#include <linux/mm.h>

static inline vm_fault_t vmf_insert_pfn_prot(struct vm_area_struct *vma, unsigned long addr,
					     unsigned long pfn, pgprot_t pgprot)
{
	int err = vm_insert_pfn_prot(vma, addr, pfn, pgprot);

	if (unlikely(err == -ENOMEM))
		return VM_FAULT_OOM;
	if (unlikely(err < 0 && err != -EBUSY))
		return VM_FAULT_SIGBUS;

	return VM_FAULT_NOPAGE;
}
#endif

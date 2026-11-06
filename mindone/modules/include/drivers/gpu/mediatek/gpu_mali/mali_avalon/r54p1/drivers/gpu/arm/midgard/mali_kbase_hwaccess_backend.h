/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_HWACCESS_BACKEND_H_
#define _KBASE_HWACCESS_BACKEND_H_

int kbase_backend_devfreq_init(struct kbase_device *kbdev);

void kbase_backend_devfreq_term(struct kbase_device *kbdev);

#endif

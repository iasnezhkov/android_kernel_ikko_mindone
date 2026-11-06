/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_JS_BACKEND_H_
#define _KBASE_JS_BACKEND_H_

int kbase_backend_timer_init(struct kbase_device *kbdev);

void kbase_backend_timer_term(struct kbase_device *kbdev);

void kbase_backend_timer_suspend(struct kbase_device *kbdev);

void kbase_backend_timer_resume(struct kbase_device *kbdev);

#endif

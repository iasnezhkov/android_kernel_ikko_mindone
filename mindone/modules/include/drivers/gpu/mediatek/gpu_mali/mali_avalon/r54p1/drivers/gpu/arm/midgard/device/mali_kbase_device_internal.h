/* SPDX-License-Identifier: GPL-2.0 */
#include <mali_kbase.h>

typedef int kbase_device_init_method(struct kbase_device *kbdev);
typedef void kbase_device_term_method(struct kbase_device *kbdev);

struct kbase_device_init {
	kbase_device_init_method *init;
	kbase_device_term_method *term;
	char *err_mes;
};

int kbase_device_kinstr_prfcnt_init(struct kbase_device *kbdev);
void kbase_device_kinstr_prfcnt_term(struct kbase_device *kbdev);

int kbase_device_timeline_init(struct kbase_device *kbdev);
void kbase_device_timeline_term(struct kbase_device *kbdev);

int kbase_device_hwcnt_context_init(struct kbase_device *kbdev);
void kbase_device_hwcnt_context_term(struct kbase_device *kbdev);

int kbase_device_hwcnt_virtualizer_init(struct kbase_device *kbdev);
void kbase_device_hwcnt_virtualizer_term(struct kbase_device *kbdev);

int kbase_device_list_init(struct kbase_device *kbdev);
void kbase_device_list_term(struct kbase_device *kbdev);

int kbase_device_io_history_init(struct kbase_device *kbdev);
void kbase_device_io_history_term(struct kbase_device *kbdev);

int kbase_device_misc_register(struct kbase_device *kbdev);
void kbase_device_misc_deregister(struct kbase_device *kbdev);

void kbase_device_id_init(struct kbase_device *kbdev);

int kbase_device_early_init(struct kbase_device *kbdev);

void kbase_device_early_term(struct kbase_device *kbdev);

int kbase_device_late_init(struct kbase_device *kbdev);

void kbase_device_late_term(struct kbase_device *kbdev);

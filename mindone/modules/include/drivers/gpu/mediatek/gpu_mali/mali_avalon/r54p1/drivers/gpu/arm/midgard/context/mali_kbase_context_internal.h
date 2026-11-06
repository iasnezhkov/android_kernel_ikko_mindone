/* SPDX-License-Identifier: GPL-2.0 */
#include <mali_kbase.h>

typedef int kbase_context_init_method(struct kbase_context *kctx);
typedef void kbase_context_term_method(struct kbase_context *kctx);

struct kbase_context_init {
	kbase_context_init_method *init;
	kbase_context_term_method *term;
	char *err_mes;
};

int kbase_context_common_init(struct kbase_context *kctx);
void kbase_context_common_term(struct kbase_context *kctx);

int kbase_context_mem_pool_group_init(struct kbase_context *kctx);
void kbase_context_mem_pool_group_term(struct kbase_context *kctx);

int kbase_context_mmu_init(struct kbase_context *kctx);
void kbase_context_mmu_term(struct kbase_context *kctx);

int kbase_context_mem_alloc_page(struct kbase_context *kctx);
void kbase_context_mem_pool_free(struct kbase_context *kctx);

void kbase_context_sticky_resource_term(struct kbase_context *kctx);

int kbase_context_add_to_dev_list(struct kbase_context *kctx);
void kbase_context_remove_from_dev_list(struct kbase_context *kctx);

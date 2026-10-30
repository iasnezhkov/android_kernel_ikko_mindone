// SPDX-License-Identifier: GPL-2.0
#include <mali_kbase.h>
#include <mali_kbase_mem_linux.h>
#include <mali_kbase_defs.h>
#include <mali_kbase_trace_gpu_mem.h>

struct kbase_dma_buf {
	struct dma_buf *dma_buf;
	struct rb_node dma_buf_node;
	u32 import_count;
};

static bool kbase_delete_dma_buf_mapping(struct kbase_context *kctx, struct dma_buf *dma_buf,
					 struct rb_root *tree)
{
	struct kbase_dma_buf *buf_node = NULL;
	struct rb_node *node = tree->rb_node;
	bool mapping_removed = false;

	lockdep_assert_held(&kctx->kbdev->dma_buf_lock);

	while (node) {
		buf_node = rb_entry(node, struct kbase_dma_buf, dma_buf_node);

		if (dma_buf == buf_node->dma_buf) {
			WARN_ON(!buf_node->import_count);

			buf_node->import_count--;

			if (!buf_node->import_count) {
				rb_erase(&buf_node->dma_buf_node, tree);
				kfree(buf_node);
				mapping_removed = true;
			}

			break;
		}

		if (dma_buf < buf_node->dma_buf)
			node = node->rb_left;
		else
			node = node->rb_right;
	}

	WARN_ON(!buf_node);
	return mapping_removed;
}

static bool kbase_capture_dma_buf_mapping(struct kbase_context *kctx, struct dma_buf *dma_buf,
					  struct rb_root *root)
{
	struct kbase_dma_buf *buf_node = NULL;
	struct rb_node *node = root->rb_node;
	bool unique_buf_imported = true;

	lockdep_assert_held(&kctx->kbdev->dma_buf_lock);

	while (node) {
		buf_node = rb_entry(node, struct kbase_dma_buf, dma_buf_node);

		if (dma_buf == buf_node->dma_buf) {
			unique_buf_imported = false;
			break;
		}

		if (dma_buf < buf_node->dma_buf)
			node = node->rb_left;
		else
			node = node->rb_right;
	}

	if (unique_buf_imported) {
		struct kbase_dma_buf *new_buf_node = kzalloc(sizeof(*new_buf_node), GFP_KERNEL);

		if (new_buf_node == NULL) {
			dev_err(kctx->kbdev->dev, "Error allocating memory for kbase_dma_buf\n");

			unique_buf_imported = false;
		} else {
			struct rb_node **new = &(root->rb_node), *parent = NULL;

			new_buf_node->dma_buf = dma_buf;
			new_buf_node->import_count = 1;
			while (*new) {
				struct kbase_dma_buf *new_node;

				parent = *new;
				new_node = rb_entry(parent, struct kbase_dma_buf, dma_buf_node);
				if (dma_buf < new_node->dma_buf)
					new = &(*new)->rb_left;
				else
					new = &(*new)->rb_right;
			}
			rb_link_node(&new_buf_node->dma_buf_node, parent, new);
			rb_insert_color(&new_buf_node->dma_buf_node, root);
		}
	} else if (!WARN_ON(!buf_node)) {
		buf_node->import_count++;
	}

	return unique_buf_imported;
}

void kbase_remove_dma_buf_usage(struct kbase_context *kctx, struct kbase_mem_phy_alloc *alloc)
{
	struct kbase_device *kbdev = kctx->kbdev;
	bool dev_mapping_removed, prcs_mapping_removed;

	mutex_lock(&kbdev->dma_buf_lock);

	dev_mapping_removed = kbase_delete_dma_buf_mapping(kctx, alloc->imported.umm.dma_buf,
							   &kbdev->dma_buf_root);

	prcs_mapping_removed = kbase_delete_dma_buf_mapping(kctx, alloc->imported.umm.dma_buf,
							    &kctx->kprcs->dma_buf_root);

	WARN_ON(dev_mapping_removed && !prcs_mapping_removed);

	spin_lock(&kbdev->gpu_mem_usage_lock);
	if (dev_mapping_removed)
		kbdev->total_gpu_pages -= alloc->nents;

	if (prcs_mapping_removed)
		kctx->kprcs->total_gpu_pages -= alloc->nents;

	if (dev_mapping_removed || prcs_mapping_removed)
		kbase_trace_gpu_mem_usage(kbdev, kctx);
	spin_unlock(&kbdev->gpu_mem_usage_lock);

	mutex_unlock(&kbdev->dma_buf_lock);
}

void kbase_add_dma_buf_usage(struct kbase_context *kctx, struct kbase_mem_phy_alloc *alloc)
{
	struct kbase_device *kbdev = kctx->kbdev;
	bool unique_dev_dmabuf, unique_prcs_dmabuf;

	mutex_lock(&kbdev->dma_buf_lock);

	unique_dev_dmabuf = kbase_capture_dma_buf_mapping(kctx, alloc->imported.umm.dma_buf,
							  &kbdev->dma_buf_root);

	unique_prcs_dmabuf = kbase_capture_dma_buf_mapping(kctx, alloc->imported.umm.dma_buf,
							   &kctx->kprcs->dma_buf_root);

	WARN_ON(unique_dev_dmabuf && !unique_prcs_dmabuf);

	spin_lock(&kbdev->gpu_mem_usage_lock);
	if (unique_dev_dmabuf)
		kbdev->total_gpu_pages += alloc->nents;

	if (unique_prcs_dmabuf)
		kctx->kprcs->total_gpu_pages += alloc->nents;

	if (unique_prcs_dmabuf || unique_dev_dmabuf)
		kbase_trace_gpu_mem_usage(kbdev, kctx);
	spin_unlock(&kbdev->gpu_mem_usage_lock);

	mutex_unlock(&kbdev->dma_buf_lock);
}

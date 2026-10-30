// SPDX-License-Identifier: GPL-2.0
#include "mali_kbase_debug_mem_allocs.h"
#include "mali_kbase_reg_track.h"
#include "mali_kbase.h"

#include <linux/string.h>
#include <linux/list.h>
#include <linux/file.h>

#if IS_ENABLED(CONFIG_DEBUG_FS)

static void debug_zone_mem_allocs_show(struct kbase_reg_zone *zone, struct seq_file *sfile)
{
	struct rb_node *p;
	struct rb_root *rbtree = &zone->reg_rbtree;
	struct kbase_va_region *reg;
	const char *type_names[5] = { "Native", "Imported UMM", "Imported user buf", "Alias",
				      "Raw" };

#define MEM_ALLOCS_HEADER \
	"              VA,          VA size,      Commit size,    Flags,     Mem type\n"
	seq_printf(sfile, "Zone name: %s\n:", kbase_reg_zone_get_name(zone->id));
	seq_printf(sfile, MEM_ALLOCS_HEADER);
	for (p = rb_first(rbtree); p; p = rb_next(p)) {
		reg = rb_entry(p, struct kbase_va_region, rblink);
		if (!(reg->flags & KBASE_REG_FREE)) {
			seq_printf(sfile, "%16llx, %16zx, %16zx, %8lx, %s\n",
				   reg->start_pfn << PAGE_SHIFT, reg->nr_pages << PAGE_SHIFT,
				   kbase_reg_current_backed_size(reg) << PAGE_SHIFT, reg->flags,
				   type_names[reg->gpu_alloc->type]);
		}
	}
}

static int debug_ctx_mem_allocs_show(struct seq_file *sfile, void *data)
{
	struct kbase_context *const kctx = sfile->private;
	enum kbase_memory_zone zone_idx;

	CSTD_UNUSED(data);

	kbase_gpu_vm_lock(kctx);
	for (zone_idx = 0; zone_idx < CONTEXT_ZONE_MAX; zone_idx++) {
		struct kbase_reg_zone *zone;

		zone = &kctx->reg_zone[zone_idx];
		debug_zone_mem_allocs_show(zone, sfile);
	}
	kbase_gpu_vm_unlock(kctx);
	return 0;
}

static int debug_mem_allocs_open(struct inode *in, struct file *file)
{
	return single_open(file, debug_ctx_mem_allocs_show, in->i_private);
}

static const struct file_operations kbase_debug_mem_allocs_fops = {
	.owner = THIS_MODULE,
	.open = debug_mem_allocs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

void kbase_debug_mem_allocs_init(struct kbase_context *const kctx)
{

	if (WARN_ON(!kctx) || WARN_ON(IS_ERR_OR_NULL(kctx->kctx_dentry)))
		return;

	debugfs_create_file("mem_allocs", 0400, kctx->kctx_dentry, kctx,
			    &kbase_debug_mem_allocs_fops);
}
#else

void kbase_debug_mem_allocs_init(struct kbase_context *const kctx)
{
}
#endif

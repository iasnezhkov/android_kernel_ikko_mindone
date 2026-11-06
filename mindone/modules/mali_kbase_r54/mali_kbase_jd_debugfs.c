// SPDX-License-Identifier: GPL-2.0
#if IS_ENABLED(CONFIG_DEBUG_FS)

#include <linux/seq_file.h>
#include <mali_kbase.h>
#include <mali_kbase_jd_debugfs.h>
#if IS_ENABLED(CONFIG_SYNC_FILE)
#include <mali_kbase_sync.h>
#endif
#include <uapi/gpu/arm/midgard/mali_kbase_ioctl.h>

struct kbase_jd_debugfs_depinfo {
	u8 id;
	char type;
};

static void kbase_jd_debugfs_fence_info(struct kbase_jd_atom *atom, struct seq_file *sfile)
{
#if IS_ENABLED(CONFIG_SYNC_FILE)
	struct kbase_sync_fence_info info;
	int res;

	switch (atom->core_req & BASE_JD_REQ_SOFT_JOB_TYPE) {
	case BASE_JD_REQ_SOFT_FENCE_TRIGGER:
		res = kbase_sync_fence_out_info_get(atom, &info);
		if (res == 0)
			seq_printf(sfile, "Sa([%pK]%d) ", info.fence, info.status);
		break;
	case BASE_JD_REQ_SOFT_FENCE_WAIT:
		res = kbase_sync_fence_in_info_get(atom, &info);
		if (res == 0)
			seq_printf(sfile, "Wa([%pK]%d) ", info.fence, info.status);
		break;
	default:
		break;
	}
#endif
}

static void kbasep_jd_debugfs_atom_deps(struct kbase_jd_debugfs_depinfo *deps,
					struct kbase_jd_atom *atom)
{
	struct kbase_context *kctx = atom->kctx;
	int i;

	for (i = 0; i < 2; i++) {
		deps[i].id = (unsigned int)(atom->dep[i].atom ?
							  kbase_jd_atom_id(kctx, atom->dep[i].atom) :
							  0);

		switch (atom->dep[i].dep_type) {
		case BASE_JD_DEP_TYPE_INVALID:
			deps[i].type = ' ';
			break;
		case BASE_JD_DEP_TYPE_DATA:
			deps[i].type = 'D';
			break;
		case BASE_JD_DEP_TYPE_ORDER:
			deps[i].type = '>';
			break;
		default:
			deps[i].type = '?';
			break;
		}
	}
}

static int kbasep_jd_debugfs_atoms_show(struct seq_file *sfile, void *data)
{
	struct kbase_context *kctx = sfile->private;
	struct kbase_jd_atom *atoms;
	unsigned long irq_flags;
	int i;

	CSTD_UNUSED(data);

	KBASE_DEBUG_ASSERT(kctx != NULL);

	seq_printf(sfile, "v%u\n", MALI_JD_DEBUGFS_VERSION);

	seq_printf(sfile, "ukv%u.%u\n", BASE_UK_VERSION_MAJOR, BASE_UK_VERSION_MINOR);

	seq_puts(sfile, " ID, Core req, St,   Predeps,           Start time, Additional info...\n");

	atoms = kctx->jctx.atoms;

	mutex_lock(&kctx->jctx.lock);

	spin_lock_irqsave(&kctx->kbdev->hwaccess_lock, irq_flags);
	for (i = 0; i != BASE_JD_ATOM_COUNT; ++i) {
		struct kbase_jd_atom *atom = &atoms[i];
		s64 start_timestamp = 0;
		struct kbase_jd_debugfs_depinfo deps[2];

		if (atom->status == KBASE_JD_ATOM_STATE_UNUSED)
			continue;

		if (ktime_to_ns(atom->start_timestamp))
			start_timestamp =
				ktime_to_ns(ktime_sub(ktime_get_raw(), atom->start_timestamp));

		kbasep_jd_debugfs_atom_deps(deps, atom);

		seq_printf(sfile, "%3u, %8x, %2u, %c%3u %c%3u, %20lld, ", i, atom->core_req,
			   atom->status, deps[0].type, deps[0].id, deps[1].type, deps[1].id,
			   start_timestamp);

		kbase_jd_debugfs_fence_info(atom, sfile);

		seq_puts(sfile, "\n");
	}
	spin_unlock_irqrestore(&kctx->kbdev->hwaccess_lock, irq_flags);
	mutex_unlock(&kctx->jctx.lock);

	return 0;
}

static int kbasep_jd_debugfs_atoms_open(struct inode *in, struct file *file)
{
	return single_open(file, kbasep_jd_debugfs_atoms_show, in->i_private);
}

static const struct file_operations kbasep_jd_debugfs_atoms_fops = {
	.owner = THIS_MODULE,
	.open = kbasep_jd_debugfs_atoms_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

void kbasep_jd_debugfs_ctx_init(struct kbase_context *kctx)
{
	const mode_t mode = 0444;

	if (WARN_ON(!kctx) || WARN_ON(IS_ERR_OR_NULL(kctx->kctx_dentry)))
		return;

	debugfs_create_file("atoms", mode, kctx->kctx_dentry, kctx, &kbasep_jd_debugfs_atoms_fops);
}

#endif

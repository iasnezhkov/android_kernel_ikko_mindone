// SPDX-License-Identifier: GPL-2.0
#include "mali_kbase.h"
#include "mali_kbase_regs_history_debugfs.h"

#if defined(CONFIG_DEBUG_FS) && !IS_ENABLED(CONFIG_MALI_NO_MALI)

#include <linux/debugfs.h>
#include <linux/version_compat_defs.h>

static int kbase_io_history_resize(struct kbase_io_history *h, u16 new_size)
{
	struct kbase_io_access *old_buf;
	struct kbase_io_access *new_buf;
	unsigned long flags;

	if (!new_size)
		goto out_err;

	new_buf = vmalloc(new_size * sizeof(*h->buf));
	if (!new_buf)
		goto out_err;

	spin_lock_irqsave(&h->lock, flags);

	old_buf = h->buf;

	h->count = 0;
	h->size = new_size;
	h->buf = new_buf;

	spin_unlock_irqrestore(&h->lock, flags);

	vfree(old_buf);

	return 0;

out_err:
	return -1;
}

int kbase_io_history_init(struct kbase_io_history *h, u16 n)
{
	h->enabled = false;
	spin_lock_init(&h->lock);
	h->count = 0;
	h->size = 0;
	h->buf = NULL;
	if (kbase_io_history_resize(h, n))
		return -1;

	return 0;
}

void kbase_io_history_term(struct kbase_io_history *h)
{
	vfree(h->buf);
	h->buf = NULL;
}

void kbase_io_history_add(struct kbase_io_history *h, void __iomem const *addr, u32 value, u8 write)
{
	struct kbase_io_access *io;
	unsigned long flags;

	spin_lock_irqsave(&h->lock, flags);

	io = &h->buf[h->count % h->size];
	io->addr = (uintptr_t)addr | write;
	io->value = value;
	++h->count;

	if (unlikely(!h->count))
		h->count = h->size;

	spin_unlock_irqrestore(&h->lock, flags);
}

void kbase_io_history_dump(struct kbase_device *kbdev)
{
	struct kbase_io_history *const h = &kbdev->io_history;
	size_t i;
	size_t iters;
	unsigned long flags;

	if (!unlikely(h->enabled))
		return;

	spin_lock_irqsave(&h->lock, flags);

	dev_err(kbdev->dev, "Register IO History:");
	iters = (h->size > h->count) ? h->count : h->size;
	dev_err(kbdev->dev, "Last %zu register accesses of %zu total:\n", iters, h->count);
	for (i = 0; i < iters; ++i) {
		struct kbase_io_access *io = &h->buf[(h->count - iters + i) % h->size];
		char const access = (io->addr & 1) ? 'w' : 'r';

		dev_err(kbdev->dev, "%6zu: %c: reg 0x%16pK val %08x\n", i, access,
			(void *)(io->addr & ~(uintptr_t)0x1), io->value);
	}

	spin_unlock_irqrestore(&h->lock, flags);
}

static int regs_history_size_get(void *data, u64 *val)
{
	struct kbase_io_history *const h = data;

	*val = h->size;

	return 0;
}

static int regs_history_size_set(void *data, u64 val)
{
	struct kbase_io_history *const h = data;

	return kbase_io_history_resize(h, (u16)val);
}

DEFINE_DEBUGFS_ATTRIBUTE(regs_history_size_fops, regs_history_size_get, regs_history_size_set,
			 "%llu\n");

static int regs_history_show(struct seq_file *sfile, void *data)
{
	struct kbase_io_history *const h = sfile->private;
	size_t i;
	size_t iters;
	unsigned long flags;

	CSTD_UNUSED(data);

	if (!h->enabled) {
		seq_puts(sfile, "The register access history is disabled\n");
		goto out;
	}

	spin_lock_irqsave(&h->lock, flags);

	iters = (h->size > h->count) ? h->count : h->size;
	seq_printf(sfile, "Last %zu register accesses of %zu total:\n", iters, h->count);
	for (i = 0; i < iters; ++i) {
		struct kbase_io_access *io = &h->buf[(h->count - iters + i) % h->size];
		char const access = (io->addr & 1) ? 'w' : 'r';

		seq_printf(sfile, "%6zu: %c: reg 0x%16pK val %08x\n", i, access,
			   (void *)(io->addr & ~(uintptr_t)0x1), io->value);
	}

	spin_unlock_irqrestore(&h->lock, flags);

out:
	return 0;
}

static int regs_history_open(struct inode *in, struct file *file)
{
	return single_open(file, &regs_history_show, in->i_private);
}

static const struct file_operations regs_history_fops = {
	.owner = THIS_MODULE,
	.open = &regs_history_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

void kbasep_regs_history_debugfs_init(struct kbase_device *kbdev)
{
	debugfs_create_bool("regs_history_enabled", 0644, kbdev->mali_debugfs_directory,
			    &kbdev->io_history.enabled);
	debugfs_create_file("regs_history_size", 0644, kbdev->mali_debugfs_directory,
			    &kbdev->io_history, &regs_history_size_fops);
	debugfs_create_file("regs_history", 0444, kbdev->mali_debugfs_directory, &kbdev->io_history,
			    &regs_history_fops);
}
#endif

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_DEBUGFS_HELPER_H_
#define _KBASE_DEBUGFS_HELPER_H_

typedef void kbase_debugfs_helper_set_attr_fn(void *array, size_t index, size_t value);

int kbase_debugfs_helper_set_attr_from_string(const char *buf, void *array, size_t nelems,
					      kbase_debugfs_helper_set_attr_fn *set_attr_fn);

int kbase_debugfs_string_validator(char *const buf);

typedef size_t kbase_debugfs_helper_get_attr_fn(void *array, size_t index);

ssize_t kbase_debugfs_helper_get_attr_to_string(char *buf, size_t size, void *array, size_t nelems,
						kbase_debugfs_helper_get_attr_fn *get_attr_fn);

int kbase_debugfs_helper_seq_read(struct seq_file *sfile, size_t nelems,
				  kbase_debugfs_helper_get_attr_fn *get_attr_fn);

int kbase_debugfs_helper_seq_write(struct file *file, const char __user *ubuf, size_t count,
				   size_t nelems, kbase_debugfs_helper_set_attr_fn *set_attr_fn);

#endif

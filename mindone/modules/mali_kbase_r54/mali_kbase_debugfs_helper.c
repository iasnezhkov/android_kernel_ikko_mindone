// SPDX-License-Identifier: GPL-2.0
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "mali_kbase_debugfs_helper.h"

#define DEBUGFS_MEM_POOLS_MAX_WRITE_SIZE (256u)

static int set_attr_from_string(char *const buf, void *const array, size_t const nelems,
				kbase_debugfs_helper_set_attr_fn *const set_attr_fn)
{
	size_t index;
	int err = 0;
	char *ptr = buf;

	for (index = 0; index < nelems && *ptr; ++index) {
		unsigned long new_size;
		size_t len;
		char sep;

		while (*ptr == ' ')
			ptr++;

		len = strcspn(ptr, "\n ");
		if (len == 0) {

			break;
		}

		sep = ptr[len];
		if (sep == ' ')
			ptr[len++] = '\0';

		err = kstrtoul(ptr, 0, &new_size);
		if (err)
			break;

		ptr += len;

		set_attr_fn(array, index, new_size);
	}

	return err;
}

int kbase_debugfs_string_validator(char *const buf)
{
	int err = 0;
	char *ptr = buf;

	while (*ptr) {
		unsigned long test_number;
		size_t len;

		while (*ptr == ' ')
			ptr++;

		len = strcspn(ptr, "\n ");
		if (len == 0) {

			break;
		}

		if (ptr[len] == ' ') {
			ptr[len] = '\0';
			err = kstrtoul(ptr, 0, &test_number);
			ptr[len] = ' ';

			len++;
		} else {

			err = kstrtoul(ptr, 0, &test_number);
		}

		if (err)
			break;

		ptr += len;
	}
	return err;
}

int kbase_debugfs_helper_set_attr_from_string(const char *const buf, void *const array,
					      size_t const nelems,
					      kbase_debugfs_helper_set_attr_fn *const set_attr_fn)
{
	char *const wbuf = kstrdup(buf, GFP_KERNEL);
	int err = 0;

	if (!wbuf)
		return -ENOMEM;

	err = kbase_debugfs_string_validator(wbuf);
	if (err) {
		kfree(wbuf);
		return err;
	}

	err = set_attr_from_string(wbuf, array, nelems, set_attr_fn);

	kfree(wbuf);
	return err;
}

ssize_t kbase_debugfs_helper_get_attr_to_string(char *const buf, size_t const size,
						void *const array, size_t const nelems,
						kbase_debugfs_helper_get_attr_fn *const get_attr_fn)
{
	ssize_t total = 0;
	size_t index;

	for (index = 0; index < nelems; ++index) {
		const char *postfix = " ";

		if (index == (nelems - 1))
			postfix = "\n";

		total += scnprintf(buf + total, size - (size_t)total, "%zu%s",
				   get_attr_fn(array, index), postfix);
	}

	return total;
}

int kbase_debugfs_helper_seq_write(struct file *const file, const char __user *const ubuf,
				   size_t const count, size_t const nelems,
				   kbase_debugfs_helper_set_attr_fn *const set_attr_fn)
{
	const struct seq_file *const sfile = file->private_data;
	void *const array = sfile->private;
	int err = 0;
	char *buf;

	if (WARN_ON(!array))
		return -EINVAL;

	if (WARN_ON(count > DEBUGFS_MEM_POOLS_MAX_WRITE_SIZE))
		return -EINVAL;

	buf = kmalloc(count + 1, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	if (copy_from_user(buf, ubuf, count)) {
		kfree(buf);
		return -EFAULT;
	}

	buf[count] = '\0';

	err = kbase_debugfs_string_validator(buf);
	if (err) {
		kfree(buf);
		return err;
	}

	err = set_attr_from_string(buf, array, nelems, set_attr_fn);
	kfree(buf);

	return err;
}

int kbase_debugfs_helper_seq_read(struct seq_file *const sfile, size_t const nelems,
				  kbase_debugfs_helper_get_attr_fn *const get_attr_fn)
{
	void *const array = sfile->private;
	size_t index;

	if (WARN_ON(!array))
		return -EINVAL;

	for (index = 0; index < nelems; ++index) {
		const char *postfix = " ";

		if (index == (nelems - 1))
			postfix = "\n";

		seq_printf(sfile, "%zu%s", get_attr_fn(array, index), postfix);
	}
	return 0;
}

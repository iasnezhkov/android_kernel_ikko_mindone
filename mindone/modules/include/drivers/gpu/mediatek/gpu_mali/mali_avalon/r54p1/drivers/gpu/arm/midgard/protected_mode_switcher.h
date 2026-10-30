/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PROTECTED_MODE_SWITCH_H_
#define _PROTECTED_MODE_SWITCH_H_

struct protected_mode_device;

struct protected_mode_ops {
	int (*protected_mode_enable)(struct protected_mode_device *protected_dev);
	int (*protected_mode_disable)(struct protected_mode_device *protected_dev);
};

struct protected_mode_device {
	struct protected_mode_ops ops;
	void *data;
};

#endif

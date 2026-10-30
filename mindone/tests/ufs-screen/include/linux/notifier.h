/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#ifndef UFS_TEST_NOTIFIER_H
#define UFS_TEST_NOTIFIER_H
#define NOTIFY_DONE 0
#define NOTIFY_OK 1
struct notifier_block {
	int (*notifier_call)(struct notifier_block *nb, unsigned long action, void *data);
};
#endif

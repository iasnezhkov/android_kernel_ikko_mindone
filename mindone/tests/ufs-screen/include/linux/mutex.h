/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#ifndef UFS_TEST_MUTEX_H
#define UFS_TEST_MUTEX_H
struct mutex {
	int held;
};
#define DEFINE_MUTEX(name) struct mutex name
void mutex_lock(struct mutex *lock);
void mutex_unlock(struct mutex *lock);
#endif

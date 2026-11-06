/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#ifndef UFS_TEST_MODULE_H
#define UFS_TEST_MODULE_H
#include <stdio.h>
#define __init
#define __exit
struct kernel_param {
	void *arg;
};
struct kernel_param_ops {
	int (*set)(const char *val, const struct kernel_param *kp);
	int (*get)(char *buffer, const struct kernel_param *kp);
};
int param_get_int(char *buffer, const struct kernel_param *kp);
#define module_param(name, type, perm)
#define module_param_cb(name, ops, arg, perm)
#define MODULE_PARM_DESC(name, text)
#define MODULE_LICENSE(text)
#define MODULE_DESCRIPTION(text)
#define module_init(function) int (*ufs_test_init)(void) = function
#define module_exit(function) void (*ufs_test_exit)(void) = function
extern int ufs_test_warnings;
#define WARN_ON(condition) ({ int warned = !!(condition); ufs_test_warnings += warned; warned; })
#define pr_info(...) ((void)0)
#define pr_warn(...) ((void)0)
#endif

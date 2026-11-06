/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#ifndef PANEL_TEST_MODULE_H
#define PANEL_TEST_MODULE_H
#define module_init(function) int (*panel_test_init)(void) = function
#define module_exit(function) void (*panel_test_exit)(void) = function
#define MODULE_AUTHOR(text)
#define MODULE_DESCRIPTION(text)
#define MODULE_LICENSE(text)
#endif

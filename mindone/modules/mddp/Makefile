# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
obj-m += mddp.o

ccflags-y += -I$(src)/include
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/include/mt-plat

mddp-y := ctrl/mddp_if.o ctrl/mddp_sm.o ctrl/mddpwh_sm.o \
	ctrl/mddp_dev.o ctrl/mddp_ipc.o filter/mddp_filter.o \
	filter/mddp_f_dev.o usage/mddp_usage.o

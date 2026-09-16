# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += cpufreq_sugov_ext.o
cpufreq_sugov_ext-objs := cpufreq_sugov_main.o nonlinear_opp_cap.o common.o
ccflags-y += -DCONFIG_MTK_OPP_CAP_INFO=1 -Wno-error -Wno-error=implicit-int -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src) -I$(srctree)/kernel/  -I$(src)/../include/drivers/thermal/mediatek -I$(src)/../include/drivers/misc/mediatek/sched -I$(src)/../include/drivers/misc/mediatek/include/upower  -include linux/sched/cputime.h
ccflags-y += -I$(src)/../include

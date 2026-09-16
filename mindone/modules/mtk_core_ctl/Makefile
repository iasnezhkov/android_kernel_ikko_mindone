# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += mtk_core_ctl.o
mtk_core_ctl-objs := sched_avg.o core_ctl.o
ccflags-y += -Wno-error -Wno-error=implicit-int -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src) -I$(srctree)/kernel/  -I$(src)/../include/drivers/thermal/mediatek -I$(src)/../include/drivers/misc/mediatek/sched  -include linux/sched/cputime.h

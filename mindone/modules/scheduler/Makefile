# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += scheduler.o
scheduler-objs := eas/sched_main.o eas/sched_sys_common.o eas/eas_plus.o eas/static_power.o eas/topology.o common.o fair.o eas/core_pause.o eas/rotate.o

ccflags-y += -I$(src)
ccflags-y += -I$(src)/eas
ccflags-y += -I$(srctree)/kernel
ccflags-y += -I$(src)/../include/kernel
ccflags-y += -I$(src)/../include/drivers/thermal/mediatek
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/sched
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/include/upower
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/include/ppm_v3
ccflags-y += -Wno-error=implicit-function-declaration -Wno-error=implicit-int -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion

# MINDONE-SCHED-ROTATE: CONFIG_MTK_SCHED_BIG_TASK_ROTATE is a Kconfig symbol
# Kconfig integration, so IS_ENABLED(CONFIG_MTK_SCHED_BIG_TASK_ROTATE) was
# always false and silently compiled out both the big-task-rotation logic
# (task_rotate_init/rotat_*, eas/rotate.c) AND the trace_sched_big_task_
# rotation tracepoint declaration+definition (eas/eas_trace.h, generated in
# eas/sched_main.c under CREATE_TRACE_POINTS) -- which is exactly the
# "undefined!" symbol kocheck reported. Turning this on both fixes the
# link AND enables the actual stock big-task-rotation feature.
ccflags-y += -DCONFIG_MTK_SCHED_BIG_TASK_ROTATE=1

obj-m += mtk_lpm.o
mtk_lpm-objs := lpm_module.o lpm_call.o lpm_registry.o
mtk_lpm-objs += mtk_cpuidle_sysfs.o mtk_lp_sysfs.o mtk_suspend_sysfs.o mtk_spm_sysfs.o mtk_lpm_sysfs.o mtk_lp_sysfs_procfs.o
mtk_lpm-objs += lpm_platform.o lpm_irqremain.o lpm_resource_ctrl.o lpm_rc.o lpm_trace.o
mtk_lpm-objs += lpm_trace_event.o
# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
ccflags-y += -I$(src) -Wno-error -Wno-error=implicit-int -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion
ccflags-y += -DMTK_LPM_MODE_MODULE -I$(src)/../include/include
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/lpm/modules/include
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/lpm/inc
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/lpm

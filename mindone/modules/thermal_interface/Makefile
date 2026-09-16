# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += thermal_interface.o
ccflags-y += -DCONFIG_DEBUG_FS=1 -DCONFIG_MTK_GPUFREQ_V2=1 -DCONFIG_MTK_MD_THERMAL=1 -DCONFIG_MTK_THERMAL_INTERFACE=1 -DCONFIG_MTK_THERMAL_JATM=1 -DCONFIG_MTK_TINYSYS_MCUPM_SUPPORT=1 -DCONFIG_MTK_TINYSYS_SSPM_SUPPORT=1 -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src)  -I$(src)/../include/drivers/gpu/mediatek/gpufreq

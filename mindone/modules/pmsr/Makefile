# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += pmsr.o
ccflags-y += -DCONFIG_MTK_PLAT_POWER_6893=1 -DCONFIG_MTK_TINYSYS_SCMI=1 -DCONFIG_MTK_TINYSYS_SSPM_V2=1 -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src) -I$(src)/../include/drivers/misc/mediatek/sspm/v2 -I$(src)/../include/drivers/misc/mediatek/tinysys_scmi

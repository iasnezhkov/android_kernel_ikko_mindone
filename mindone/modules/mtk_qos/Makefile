# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += mtk_qos.o
mtk_qos-objs := mtk_qos_common.o mtk_qos_bound.o mtk_qos_sysfs.o mtk_qos_share.o qos-v6893.o mtk_qos_scmi.o

ccflags-y += -I$(src)
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/sspm/v3
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/tinysys_scmi
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/sspm/v3
ccflags-y += -DMTK_SCMI -DCONFIG_MTK_TINYSYS_SSPM_V3=1
ccflags-y += -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion

# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += mmqos_common.o
mmqos_common-objs := mmqos-mtk.o mmqos-hrt.o
ccflags-y += -DCONFIG_MTK_MMDVFS_MODULE=1 -DCONFIG_INTERCONNECT_MTK_EXTENSION=1 -DMTK_M4U_DOM_NR_MAX=16 -DMTK_M4U_TAB_NR_MAX=3 -DCONFIG_INTERCONNECT_MTK_MMQOS_COMMON=1 -DCONFIG_MTK_DRAMC=1 -DCONFIG_MTK_SMI=1 -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src) -I$(src)/../include/drivers/iommu -I$(src)/../include/drivers/misc/mediatek/mtk-interconnect -I$(src)/../include/drivers/misc/mediatek/qos -I$(src)/../include/drivers/misc/mediatek/mmdvfs 
ccflags-y += -DCONFIG_MTK_MMDVFS=1
ccflags-y += -I$(src)/../include
ccflags-y += -I$(src)/../include/include

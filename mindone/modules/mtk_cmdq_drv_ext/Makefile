# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += mtk_cmdq_drv_ext.o
mtk_cmdq_drv_ext-objs := mtk-cmdq-mailbox-ext.o mtk-cmdq-helper-ext.o cmdq-util.o
ccflags-y += -DCONFIG_MTK_SMI=1 -DMTK_M4U_DOM_NR_MAX=16 -DCONFIG_MTK_CMDQ_MBOX_EXT=1 -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src)  -I$(src)/../include/drivers/misc/mediatek/iommu  -I$(src)/../include/drivers/misc/mediatek/vcp/rv -I$(src)/../include/drivers/misc/mediatek/vcp/include 
ccflags-y += -I$(src)/../include/include
ccflags-y += -I$(src)/../include

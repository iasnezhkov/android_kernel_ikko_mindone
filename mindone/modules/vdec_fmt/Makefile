# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += vdec_fmt.o
vdec_fmt-objs := vdec_fmt_driver.o vdec_fmt_pm.o vdec_fmt_dmabuf.o vdec_fmt_sync.o
ccflags-y += -DCONFIG_MTK_SMI=1 -DMTK_M4U_DOM_NR_MAX=16 -I$(src)/../include/include -DCONFIG_MTK_CMDQ_MBOX_EXT=1 -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src) -I$(src)/../include/drivers/misc/mediatek/mtk-interconnect -I$(src)/../include/drivers/misc/mediatek/cmdq/mailbox -I$(src)/../include/drivers/misc/mediatek/include -I$(src)/../include/drivers/misc/mediatek/iommu -I$(src)/../include/drivers/iommu
ccflags-y += -I$(src)/../include

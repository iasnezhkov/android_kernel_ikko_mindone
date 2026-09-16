# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += camera_dip_isp6s.o
camera_dip_isp6s-objs := camera_dip.o
ccflags-y += -DMINDONE_NO_COMPAT_IOCTL=1 -DCONFIG_MTK_IOMMU_PGTABLE_EXT=1 -DCONFIG_PM=1 -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src)  -I$(src)/../include/drivers/misc/mediatek/mdp  -I$(src)/../include/drivers/misc/mediatek/include  -I$(src)/../include/drivers/misc/mediatek/cmdq/mailbox -DCONFIG_DEBUG_FS=1 -DCONFIG_MTK_CMDQ_MBOX_EXT=1 -DCONFIG_MTK_MT6382_BDG=1
ccflags-y += -I$(src)/../include
ccflags-y += -I$(src)/../include/include

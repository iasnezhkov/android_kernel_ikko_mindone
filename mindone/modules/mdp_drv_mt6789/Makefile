# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += mdp_drv_mt6789.o
mdp_drv_mt6789-objs := mdp_driver.o mdp_ioctl_ex.o mdp_common.o cmdq_record.o cmdq_virtual.o cmdq_device.o mdp_event_common.o cmdq_subsys_common.o cmdq_helper_ext.o cmdq_mmp.o mdp_fence.o mdp_mt6789.o

ccflags-y += -I$(src)
ccflags-y += -I$(src)/mach_stub
ccflags-y += -I$(src)
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/smi
ccflags-y += -I$(src)/../include/drivers/dma-buf/heaps
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/mtk-interconnect
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/include
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/mmp -I$(src)/../include/drivers/misc/mediatek/smi
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/smi
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/iommu
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/cmdq/mailbox
ccflags-y += -DCONFIG_MTK_CMDQ_MBOX_EXT=1 -DCONFIG_MTK_SMI=1
ccflags-y += -DCMDQ_MDP_COLOR -DMET_USER_EVENT_SUPPORT -DCMDQ_AEE_READY -DCMDQ_LARGE_MAX_FIRSTERROR_BUFFER -DCONFIG_MMPROFILE=1
ccflags-y += -Wno-error=format -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion
ccflags-y += -DCONFIG_INTERCONNECT_MTK_EXTENSION=1

ccflags-y += -DCMDQ_SECURE_PATH_SUPPORT
ccflags-y += -I$(src)/../include/drivers/dma-buf/heaps
ccflags-y += -I$(src)/../include
ccflags-y += -I$(src)/../include/include

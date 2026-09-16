# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += cmdq_sec_drv.o
cmdq_sec_drv-objs := cmdq-sec-gp.o cmdq_sec_mtee.o cmdq-sec-mailbox.o cmdq-sec-helper.o
ccflags-y += -DCONFIG_MMPROFILE=1 -DCONFIG_MTK_ENABLE_GENIEZONE=1 -DCONFIG_MTK_CMDQ_MBOX_EXT=1 -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src)  -I$(src)/../include/drivers/misc/mediatek/mmp  -I$(src)/../include/drivers/misc/mediatek/geniezone/public
ccflags-y += -I$(src)/../include/include

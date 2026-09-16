# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += cmdq_helper_inf.o
ccflags-y += -DCONFIG_MTK_CMDQ_MBOX_EXT=1 -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src)  -I$(src)/../include/drivers/misc/mediatek/cmdq/mailbox 
ccflags-y += -I$(src)/../include/include

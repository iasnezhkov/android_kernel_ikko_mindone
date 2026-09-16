# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += mtk_disp_sec.o
ccflags-y += -DCONFIG_INTERCONNECT_MTK_EXTENSION=1  -DCONFIG_MTK_CMDQ_MBOX_EXT=1 -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src)  -I$(src)/../include/drivers/misc/mediatek/cmdq/mailbox  -I$(src)/../include/drivers/misc/mediatek/mtk-interconnect  -I$(src)/../include/drivers/misc/mediatek/slbc -I$(src)/../include/drivers/misc/mediatek/cmdq/mailbox  -I$(src)/../include/drivers/dma-buf/heaps  -I$(src)/../include/drivers/misc/mediatek/cmdq/mailbox   -I$(src)/../include
ccflags-y += -I$(src)/../include/include

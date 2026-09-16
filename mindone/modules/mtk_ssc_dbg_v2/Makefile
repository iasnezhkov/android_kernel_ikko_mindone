# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += mtk-ssc-dbg-v2.o
mtk-ssc-dbg-v2-y += ssc_dbg_v2.o
ccflags-y += -include $(src)/../include/include/linux/soc/mediatek/mtk_sip_svc.h -Wno-error -I$(src) -I$(src)/../include/include -I$(src)/../include/drivers/misc/mediatek/include  -I$(src)/../include/drivers/misc/mediatek/tinysys_scmi -I$(src)/../include/drivers/misc/mediatek/ssc/inc

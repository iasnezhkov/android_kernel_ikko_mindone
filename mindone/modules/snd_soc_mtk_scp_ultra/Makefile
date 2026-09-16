# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += snd_soc_mtk_scp_ultra.o
snd_soc_mtk_scp_ultra-objs := mtk-scp-ultra-pcm.o mtk-scp-ultra-mem-control.o mtk-scp-ultra-platform-driver.o mtk-scp-ultra-common.o ultra_ipi.o
ccflags-y += -DCONFIG_MTK_HANG_DETECT=1 -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src) -I$(src)/../include/sound/soc/mediatek/common -I$(src)/../include/sound/soc/mediatek/ultrasound/ultra_common -I$(src)/../include/drivers/misc/mediatek/scp/include -I$(src)/../include
ccflags-y += -I$(src)/../include/include
include $(src)/../include/mindone/flags-audio.mk

# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += mtk_scp_audio.o
mtk_scp_audio-objs := scp_audio_buf.o mtk-scp-audio-driver.o mtk-scp-audio-pcm.o mtk-scp-audio-mem-control.o
ccflags-y += -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src) -I$(src)/../include/drivers/misc/mediatek/audio_ipi/include  -I$(src)/../include/drivers/misc/mediatek/scp/include -I$(src)/../include/drivers/misc/mediatek/scp/rv -I$(src)/../include/sound/soc/mediatek/common
ccflags-y += -I$(src)/../include
ccflags-y += -I$(src)/../include/include
include $(src)/../include/mindone/flags-audio.mk

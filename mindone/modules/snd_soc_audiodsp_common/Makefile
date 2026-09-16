# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += snd_soc_audiodsp_common.o
snd_soc_audiodsp_common-objs := audio_buf.o mtk-dsp-dai.o mtk-dsp-pcm.o mtk-dsp-common.o mtk-dsp-platform-driver.o mtk-dsp-mem-control.o
ccflags-y += -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src) -I$(src)/../include/drivers/misc/mediatek/audio_ipi/include -I$(src)/../include/drivers/misc/mediatek/adsp/include  -I$(src)/../include/sound/soc/mediatek/common
ccflags-y += -I$(src)/../include
include $(src)/../include/mindone/flags-audio.mk

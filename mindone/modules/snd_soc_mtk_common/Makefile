# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += snd_soc_mtk_common.o
snd_soc_mtk_common-objs := mtk-afe-platform-driver.o mtk-afe-fe-dai.o mtk-mmap-ion.o mtk-usip.o mtk-sram-manager.o mtk-sp-pcm-ops.o mtk-afe-debug.o
ccflags-y += -DCONFIG_COMPAT=1 -DCONFIG_MTK_AEE_FEATURE=1 -DCONFIG_MTK_AUDIODSP_SUPPORT=1 -DCONFIG_MTK_AURISYS_PHONE_CALL_SUPPORT=1 -DCONFIG_MTK_ECCCI_C2K_USB=1 -DCONFIG_MTK_ULTRASND_PROXIMITY=1 -DCONFIG_MTK_VOW_SUPPORT=1 -DCONFIG_SND_SOC_MTK_AUDIO_DSP=1 -DCONFIG_SND_SOC_MTK_SRAM=1 -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src) -I$(src)/../include/drivers/misc/mediatek/audio_ipi/include -I$(src)/../include/drivers/misc/mediatek/adsp/include -I$(src)/../include/sound/soc/mediatek/common -I$(src)/../include/drivers/misc/mediatek/include -DCONFIG_MTK_HANG_DETECT=1
ccflags-y += -I$(src)/../include
include $(src)/../include/mindone/flags-audio.mk

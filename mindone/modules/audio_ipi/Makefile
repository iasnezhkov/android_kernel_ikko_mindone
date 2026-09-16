# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += audio_ipi.o
audio_ipi-objs := common/audio_ipi.o audio_task_manager.o audio_messenger_ipi.o audio_ipi_queue.o adsp_ipi_queue.o audio_ipi_dma.o audio_ringbuf.o audio_ipi_platform_common.o audio_ipi_platform.o
ccflags-y += -DCONFIG_ADSP_VOIP_LEGACY=1 -DCONFIG_MTK_AUDIODSP_SUPPORT=1 -DCONFIG_MTK_SCP_AUDIO=1 -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src)  -I$(src)/../include/drivers/misc/mediatek/adsp/include -I$(src)/../include/drivers/misc/mediatek/scp/include -I$(src)/../include/drivers/misc/mediatek/scp/rv -I$(src)/../include/sound/soc/mediatek/audio_scp  -I$(src)/../include
ccflags-y += -include $(src)/mtk_sip_svc_ext.h
ccflags-y += -I$(src)/../include/include
include $(src)/../include/mindone/flags-audio.mk

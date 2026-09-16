# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += mtk_scp_audiocommon.o
mtk_scp_audiocommon-objs := scp_audio_ipi.o audio_mbox.o
ccflags-y += -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src) -I$(src)/../include/drivers/misc/mediatek/audio_ipi/include  -I$(src)/../include/drivers/misc/mediatek/scp/include -I$(src)/../include/drivers/misc/mediatek/scp/rv  -I$(src)/../include
ccflags-y += -I$(src)/../include/include
include $(src)/../include/mindone/flags-audio.mk

# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += mt6789_mt6366.o
ccflags-y += -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src)  -I$(src)/../include/sound/soc/mediatek/mt6789 -I$(src)/../include/drivers/misc/mediatek/include -DCONFIG_MTK_HANG_DETECT=1 -DCONFIG_SND_SOC_MTK_BTCVSD=1
ccflags-y += -I$(src)/../include
include $(src)/../include/mindone/flags-audio.mk

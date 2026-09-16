# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += mtk_scp_ultra.o
ccflags-y += -DCONFIG_MTK_ULTRASND_PROXIMITY=1 -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src) -I$(src)/../include/sound/soc/mediatek/common -I$(src)/../include/sound/soc/mediatek/ultrasound/ultra_common -I$(src)/../include/drivers/misc/mediatek/scp/include
ccflags-y += -I$(src)/../include
ccflags-y += -I$(src)/../include/include

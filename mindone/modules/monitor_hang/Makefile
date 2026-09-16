# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
ccflags-y += -I$(src) -I$(src)/../include/drivers/misc/mediatek/aee -I$(src)/../include/drivers/misc/mediatek/include -I$(src)/../include/drivers/misc/mediatek/include/mt-plat -I$(src)/../include/drivers/misc/mediatek/aee/aed -Wno-error -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -DCONFIG_MTK_HANG_DETECT=1 -DCONFIG_MTK_AEE_FEATURE=1
obj-m += monitor_hang.o
monitor_hang-objs := hang_detect.o hang_unwind.o

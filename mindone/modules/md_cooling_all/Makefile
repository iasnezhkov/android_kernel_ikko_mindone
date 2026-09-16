# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += md_cooling_all.o
md_cooling_all-objs := src/md_cooling.o md_cooling_debugfs.o
ccflags-y += -DCONFIG_MTK_MD_THERMAL=1 -Wno-error -Wno-error=strict-prototypes -Wno-error=int-conversion -I$(src) -I$(src)/../include/drivers/misc/mediatek/include/mt-plat


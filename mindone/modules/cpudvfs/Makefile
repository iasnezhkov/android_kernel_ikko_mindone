# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += cpudvfs.o
cpudvfs-objs := eem-dbg-lite.o cpufreq-dbg-lite.o devinfo.o
ccflags-y += -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src) -I$(src)/../include/include -I$(src)/../include/drivers/misc/mediatek/cpufreq_lite


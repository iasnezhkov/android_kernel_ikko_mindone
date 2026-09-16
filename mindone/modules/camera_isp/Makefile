# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += camera_isp.o
ccflags-y += -DMINDONE_NO_COMPAT_IOCTL=1 -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src)  -I$(src)/../include/drivers/misc/mediatek/cam_timesync  -I$(src)/../include/drivers/misc/mediatek/cameraisp/src/common -DCONFIG_COMPAT=1 -DCONFIG_PM=1 -DCONFIG_PM_WAKELOCKS=1
ccflags-y += -I$(src)/../include

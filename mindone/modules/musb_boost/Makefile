# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += musb_boost.o
musb_boost-objs := musb_boost_common.o usb_boost_plat.o
ccflags-y += -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src) -I$(src)/../include/drivers/misc/mediatek/include/mt-plat -I$(src)/../include/drivers/usb/host -I$(src)/../include/drivers/usb/mtu3 -I$(src)/../include/drivers/misc/mediatek/usb20 -I$(src)/../include/drivers/misc/mediatek/include/ppm_v3 -I$(src)/../include/drivers/misc/mediatek/performance/fpsgo_v3/fbt/include
ccflags-y += -DCONFIG_MTK_DVFSRC=1
ccflags-y += -I$(src)/../include

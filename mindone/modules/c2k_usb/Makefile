# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += c2k_usb.o
c2k_usb-objs := f_rawbulk.o rawbulk.o rawbulk_transfer.o
ccflags-y += -DCONFIG_MTK_ECCCI_C2K_USB=1 -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src) -I$(src)/../include/drivers/usb/gadget -I$(src)/../include/drivers/misc/mediatek/include
ccflags-y += -I$(src)/../include

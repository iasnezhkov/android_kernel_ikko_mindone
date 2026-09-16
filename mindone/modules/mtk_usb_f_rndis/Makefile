# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += mtk_usb_f_rndis.o
mtk_usb_f_rndis-objs := f_rndis.o rndis.o
ccflags-y += -Wno-error -I$(src) -I$(src)/../include/drivers/usb/gadget -I$(src)/../include/drivers/usb/gadget/udc 
ccflags-y += -I$(src)/../include

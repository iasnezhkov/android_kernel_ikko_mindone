# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += mtk_pd_adapter.o
ccflags-y += -Wno-error -DCONFIG_TCPC_CLASS=1 -DCONFIG_USB_POWER_DELIVERY=1 -I$(src) -I$(src)/../include/drivers/misc/mediatek/usb20 -I$(src)/../include/drivers/misc/mediatek/typec/tcpc/inc -I$(src)/../include/drivers/misc/mediatek/power_throttling -I$(src)/../include/drivers/misc/mediatek/include  
ccflags-y += -I$(src)/../include

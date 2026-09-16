# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += mt6789_dcm.o
mt6789_dcm-objs := mt6789_dcm_internal.o mt6789_dcm_autogen.o
ccflags-y += -Wno-error -I$(src) -I$(src)/../include/include/linux/soc/mediatek -I$(src)/../include/drivers/misc/mediatek/dcm/include

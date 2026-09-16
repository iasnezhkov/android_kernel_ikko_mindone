# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += mcupm.o
mcupm-objs := mcupm_driver.o
ccflags-y += -Wno-error -I$(src)  -I$(src)/../include/include/linux/soc/mediatek
ccflags-y += -I$(src)/../include/include

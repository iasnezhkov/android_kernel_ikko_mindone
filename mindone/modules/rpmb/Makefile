# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += rpmb.o
ccflags-y += -Wno-error -DCONFIG_RPMB=1 -I$(src) -I$(src)/../include/include

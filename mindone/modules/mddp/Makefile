# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += mddp.o
mddp-objs := mddp_if.o mddp_sm.o mddpwh_sm.o mddp_dev.o mddp_ipc.o mddp_filter.o mddp_f_dev.o mddp_usage.o
ccflags-y += -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src) -I$(src)/../include/drivers/misc/mediatek/include/mt-plat 

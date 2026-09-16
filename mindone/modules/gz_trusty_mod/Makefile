# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += gz_trusty_mod.o
gz_trusty_mod-objs := trusty.o smcnr-table.o trusty-debugfs.o
ccflags-y += -DMAX_USER_RT_PRIO=MAX_RT_PRIO -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src) -DCONFIG_64BIT=1 -DCONFIG_ARM64=1 -DCONFIG_FTRACE=1 -DCONFIG_MTK_AEE_IPANIC=1 -DCONFIG_MT_GZ_TRUSTY_DEBUGFS=1
# revive (temporary reference-tree include paths)
ccflags-y +=  -I$(src)/../include/drivers/misc/mediatek/geniezone/public 

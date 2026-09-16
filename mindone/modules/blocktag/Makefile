# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += blocktag.o
blocktag-objs := blocktag-core.o blocktag-index.o blocktag-ufs.o blocktag-mmc.o blocktag-pm-trace.o fscmd-trace.o blocktag-fuse.o
ccflags-y += -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src) -I$(src)/../include/drivers/misc/mediatek/include/mt-plat -I$(src)/../include/drivers/misc/mediatek/include  -I$(src)/../include/drivers/scsi/ufs -I$(src)/../include/drivers/mmc/core -I$(src)/../include/drivers/mmc/host -I$(src)/../include/fs/fuse -DCONFIG_CGROUP_SCHED=1 -DCONFIG_COMPAT=1 -DCONFIG_MTK_BLOCK_IO_PM_DEBUG=1 -DCONFIG_MTK_BLOCK_IO_TRACER=1 -DCONFIG_MTK_FSCMD_TRACER=1 -DCONFIG_MTK_FUSE_DEBUG=1
ccflags-y += -DCONFIG_MTK_AEE_IPANIC=1
ccflags-y += -I$(src)/../include/include
ccflags-y += -I$(src)/../include

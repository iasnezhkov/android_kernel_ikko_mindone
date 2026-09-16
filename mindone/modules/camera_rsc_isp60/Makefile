# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += camera_rsc_isp60.o
camera_rsc_isp60-objs := engine_request.o camera_rsc.o
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/cameraisp/rsc/isp_6s -DCONFIG_MTK_CMDQ_MBOX_EXT=1 -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src) 
ccflags-y += -I$(src)/../include
ccflags-y += -I$(src)/../include/include

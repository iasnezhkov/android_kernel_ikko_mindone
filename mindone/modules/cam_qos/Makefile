# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += cam_qos.o
ccflags-y += -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src)  -I$(src)/../include/drivers/misc/mediatek/mtk-interconnect  -I$(src)/../include/drivers/misc/mediatek/cameraisp/src/common -DCONFIG_COMPAT=1
ccflags-y += -I$(src)/../include/include

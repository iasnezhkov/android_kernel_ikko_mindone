# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += device_apc_common.o
device_apc_common-objs := devapc-mtk-multi-ao.o
ccflags-y += -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src)
# revive (temporary reference-tree include paths)
ccflags-y += -I$(src)/../include/include 

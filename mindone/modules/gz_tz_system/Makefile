# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += gz_tz_system.o
gz_tz_system-objs := tz_system.o tz_mmap.o kree_mem.o
ccflags-y += -DCONFIG_MTK_ENABLE_GENIEZONE=1 -DCONFIG_COMPAT=1 -DCONFIG_MTK_ENG_BUILD=1 -DCONFIG_PM_SLEEP=1 -DCONFIG_TEE=1 -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src)
# revive (temporary reference-tree include paths)
ccflags-y +=  -I$(src)/../include/drivers/misc/mediatek/geniezone/public 
ccflags-y += -I$(src)/../include

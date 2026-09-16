# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += emi.o
emi-objs := emi-cen.o emi-isu.o emi-dbg.o emi-mpu.o
ccflags-y += -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src)
# revive (temporary reference-tree include paths)
ccflags-y +=  -I$(src)/../include/drivers/misc/mediatek/include 
ccflags-y += -I$(src)/../include/include

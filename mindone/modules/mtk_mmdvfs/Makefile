# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += mtk_mmdvfs.o
ccflags-y += -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src)
# revive (temporary reference-tree include paths)
ccflags-y +=  -I$(src)/../include/drivers/misc/mediatek  -I$(src)/../include/drivers/tty/serial 
ccflags-y += -I$(src)/../include

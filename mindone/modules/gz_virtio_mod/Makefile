# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += gz_virtio_mod.o
gz_virtio_mod-objs := trusty-virtio.o trusty-mem.o
ccflags-y += -Wno-error -I$(src)
# revive (temporary reference-tree include paths)
ccflags-y +=  -I$(src)/../include/drivers/misc/mediatek/geniezone/public 
ccflags-y += -I$(src)/../include

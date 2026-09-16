# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += gz_ipc_mod.o
gz_ipc_mod-objs := trusty-ipc.o
ccflags-y += -Wno-error -I$(src)
# revive (temporary reference-tree include paths)
ccflags-y +=  -I$(src)/../include/drivers/misc/mediatek/geniezone/public 
ccflags-y += -I$(src)/../include

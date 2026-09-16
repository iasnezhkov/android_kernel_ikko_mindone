# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += gz_irq_mod.o
gz_irq_mod-objs := trusty-irq.o
ccflags-y += -Wno-error -I$(src)
# revive (temporary reference-tree include paths)
ccflags-y +=  -I$(src)/../include/drivers/misc/mediatek/geniezone/public 

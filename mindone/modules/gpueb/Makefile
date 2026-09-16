# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += gpueb.o
gpueb-objs := gpueb_init.o gpueb_ipi.o gpueb_logger.o gpueb_plat_service.o gpueb_reserved_mem.o gpueb_gpumpu.o gpueb_hwvoter_dbg.o gpueb_debug.o gpueb_timesync.o
ccflags-y += -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src) -I$(src)/../include/drivers/misc/mediatek/include/mt-plat 
ccflags-y += -I$(src)/../include/include

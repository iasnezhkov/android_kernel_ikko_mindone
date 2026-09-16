# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += mtk_gpufreq_wrapper.o
mtk_gpufreq_wrapper-objs := gpufreq_v2.o gpufreq_debug.o gpu_misc.o
ccflags-y += -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src) -I$(src)/../include/include -I$(src)/../include/drivers/gpu/mediatek/gpufreq/v2/include  -I$(src)/../include/drivers/gpu/mediatek/mt-plat 
ccflags-y += -I$(src)/../include

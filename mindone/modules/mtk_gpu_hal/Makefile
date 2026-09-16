# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += mtk_gpu_hal.o
mtk_gpu_hal-objs := mtk_gpu_utility.o
ccflags-y += -DCONFIG_MTK_GPUFREQ_V2=1 -Wno-error -I$(src)  -I$(src)/../include/drivers/gpu/mediatek -I$(src)/../include/drivers/gpu/mediatek/gpufreq

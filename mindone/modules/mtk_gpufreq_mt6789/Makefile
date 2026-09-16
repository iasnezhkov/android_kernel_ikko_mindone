# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += mtk_gpufreq_mt6789.o
mtk_gpufreq_mt6789-objs := gpufreq_history_common.o gpufreq_history_mt6789.o gpuppm.o gpufreq_mt6789.o gpudfd_mt6789.o
ccflags-y += -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src)  -I$(src)/../include/drivers/gpu/mediatek/gpufreq/v2/include  -I$(src)/../include/drivers/gpu/mediatek/mt-plat  -I$(src)/../include/drivers/clk/mediatek 

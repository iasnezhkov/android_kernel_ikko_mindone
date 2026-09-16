# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += mtk_gpu_power_throttling.o
ccflags-y += -Wno-error -I$(src)
# revive (temporary reference-tree include paths)
ccflags-y +=  -I$(src)/../include/drivers/misc/mediatek/include  -I$(src)/../include/drivers/misc/mediatek/mtk-interconnect  -I$(src)/../include/drivers/misc/mediatek/mmp  -I$(src)/../include/drivers/misc/mediatek/smi  -I$(src)/../include/drivers/misc/mediatek/iommu -I$(src)/../include/drivers/misc/mediatek/mmdvfs -I$(src)/../include/drivers/misc/mediatek/pwm  -I$(src)/../include/drivers/misc/mediatek/pidmap  -I$(src)/../include/drivers/gpu/mediatek/gpufreq 

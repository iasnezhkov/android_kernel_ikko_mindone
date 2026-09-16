# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += mtk_gpu_qos.o
mtk_gpu_qos-objs := gpu_bm.o
ccflags-y += -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src)  -I$(src)/../include/drivers/gpu/mediatek/gpufreq -I$(src)/../include/drivers/gpu/mediatek
ccflags-y += -I$(src)/../include
# MINDONE-GPUQOS-IPI (12.09): without CONFIG_MTK_QOS_FRAMEWORK the module does not
# send the GPU-BW buffer address to SSPM (pr_debug "sspm_ipi is not support!") -- DVFSRC
# does not see GPU load, and DDR sits at 800 MHz under Geekbench Compute.
# qos_ipi_to_sspm_scmi_command is exported by mtk_qos.ko (MTK_SCMI, as in its Makefile).
ccflags-y += -DCONFIG_MTK_QOS_FRAMEWORK=1 -DMTK_SCMI -DCONFIG_MTK_TINYSYS_SSPM_V3=1 -I$(src)/../mtk_qos -I$(src)/../include/drivers/misc/mediatek/tinysys_scmi -I$(src)/../include/drivers/misc/mediatek/sspm/v3

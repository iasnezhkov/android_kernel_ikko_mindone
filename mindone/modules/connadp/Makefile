# vendor-headers/: MediaTek platform headers mirrored at their kernel-tree paths
ccflags-y += -Wno-error
ccflags-y += -I$(src)
ccflags-y += -I$(srctree)
ccflags-y += -I$(src)/vendor-headers
ccflags-y += 
ccflags-y += -I$(src)/vendor-headers/drivers/misc/mediatek/clkbuf/src
ccflags-y += -I$(src)/vendor-headers/drivers/misc/mediatek/include/mt-plat
ccflags-y += -I$(srctree)/drivers/mmc/core
ccflags-y += -I$(src)/vendor-headers/drivers/mmc/core
ccflags-y += -I$(src)/vendor-headers/drivers/misc/mediatek/eccci
ccflags-y += -I$(srctree)/drivers/clk/mediatek/
ccflags-y += -I$(srctree)/drivers/pinctrl/mediatek/
ccflags-y += -I$(src)/vendor-headers/drivers/misc/mediatek/power_throttling
ccflags-y += -I$(src)/vendor-headers/drivers/misc/mediatek/cpufreq_v2/inc
ccflags-y += -I$(src)/vendor-headers/drivers/misc/mediatek/include
ccflags-y += -DCONFIG_PMIC_LBAT_SERVICE=1

obj-m += connadp.o
connadp-y := common/connectivity_build_in_adapter.o common/wmt_build_in_adapter.o power_throttling/adapter.o power_throttling/core.o
ccflags-y += -I$(src)/../include

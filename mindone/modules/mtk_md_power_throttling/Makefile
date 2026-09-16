# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += mtk_md_power_throttling.o
ccflags-y += -Wno-error -I$(src) -I$(src)/../include/drivers/misc/mediatek/include/mt-plat 
ccflags-y += -DCONFIG_PMIC_LBAT_SERVICE=1
ccflags-y +=

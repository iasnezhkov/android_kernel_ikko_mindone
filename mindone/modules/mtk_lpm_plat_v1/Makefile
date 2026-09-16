# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += mtk_lpm_plat_v1.o
mtk_lpm_plat_v1-y := lpm_plat.o lpm_plat_apmcu.o lpm_plat_suspend.o
ccflags-y += -Wno-error -I$(src) -DMTK_LPM_MODE_MODULE -I$(src)/../include/include
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/lpm/modules/include
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/lpm/inc
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/lpm

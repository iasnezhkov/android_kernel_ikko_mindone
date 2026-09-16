# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += mmqos_mt6789.o
ccflags-y += -DCONFIG_INTERCONNECT_MTK_EXTENSION=1 -Wno-error -Wno-error=strict-prototypes -Wno-error=int-conversion -I$(src) -DCONFIG_INTERCONNECT_MTK_MMQOS_COMMON=1
# revive (temporary reference-tree include paths)
ccflags-y +=  -I$(src)/../include/drivers/misc/mediatek/mtk-interconnect 
ccflags-y += -I$(src)/../include/include

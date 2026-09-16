# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += mtk_smi_dbg.o
ccflags-y += -DMTK_M4U_DOM_NR_MAX=16 -DMTK_M4U_TAB_NR_MAX=3 -I$(src)/../include/include -DCONFIG_MTK_EMI=1 -DCONFIG_MTK_SMI=1 -DCONFIG_MTK_SMI_BWC=1 -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src) -I$(src)/../include/drivers/iommu -I$(src)/../include/drivers/clk/mediatek 
ccflags-y += -I$(src)/../include

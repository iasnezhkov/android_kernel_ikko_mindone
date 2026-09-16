# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += mtk_iommu.o
ccflags-y += -DCONFIG_ARCH_DMA_ADDR_T_64BIT=1 -DCONFIG_MTK_IOMMU=1 -DCONFIG_MTK_IOMMU_ARM32=1 -DCONFIG_MTK_IOMMU_MISC_DBG=1 -DCONFIG_MTK_IOMMU_MISC_SECURE=1 -DCONFIG_MTK_SMI=1 -DMTK_M4U_DOM_NR_MAX=16 -DMTK_M4U_TAB_NR_MAX=3 -D'MTK_M4U_TO_DOM(id)=(((id) >> 16) & 0xf)' -D'MTK_M4U_TO_TAB(id)=(((id) >> 20) & 0x3)' -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src) 
# revive (temporary reference-tree include paths)
ccflags-y +=  -I$(src)/../include/drivers/leds  -I$(src)/../include/drivers/iommu 
ccflags-y += -I$(src)/../include

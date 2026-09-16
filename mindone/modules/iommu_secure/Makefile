# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += iommu_secure.o
ccflags-y += -DCONFIG_MTK_IOMMU_MISC_SECURE=1 -DCONFIG_MTK_SMI=1 -DMTK_M4U_DOM_NR_MAX=16 -DMTK_M4U_TAB_NR_MAX=3 -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src)  -I$(src)/../include/drivers/iommu 

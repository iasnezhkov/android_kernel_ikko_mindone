# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += iommu_debug.o
ccflags-y += -include $(srctree)/include/dt-bindings/memory/mt6789-memory-port-full.h -DCONFIG_MTK_SMI=1 -DMTK_M4U_DOM_NR_MAX=16 -DMTK_M4U_TAB_NR_MAX=3 -Wno-error -Wno-error=implicit-int -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src)  -I$(src)/../include/drivers/iommu 

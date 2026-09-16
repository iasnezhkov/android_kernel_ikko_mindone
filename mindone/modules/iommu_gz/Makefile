# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += iommu_gz.o
iommu_gz-objs := iommu_pseudo.o iommu_gz_sec.o

ccflags-y += -I$(src)
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/include/mt-plat
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/trusted_mem
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/geniezone/public
ccflags-y += -DCONFIG_MTK_ENABLE_GENIEZONE=1
ccflags-y += -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion
ccflags-y += -DCONFIG_MTK_SMI=1
ccflags-y += -I$(src)/../include

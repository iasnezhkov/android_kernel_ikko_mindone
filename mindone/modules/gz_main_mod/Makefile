# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += gz_main_mod.o
gz_main_mod-objs := gz_main.o mtee_ut/gz_ut.o mtee_ut/gz_chmem_ut.o mtee_ut/gz_shmem_ut.o mtee_ut/gz_vreg_ut.o

ccflags-y += -I$(src)
ccflags-y += -I$(src)/../include/drivers/dma-buf/heaps -I$(src)/../include/drivers/misc/mediatek/geniezone/include
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/geniezone/public -I$(src)/../include/drivers/misc/mediatek/trusted_mem/ssmr
ccflags-y += -I$(src)/../include/drivers/dma-buf/heaps -I$(src)/../include/drivers/misc/mediatek/geniezone/include
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/geniezone/public -I$(src)/../include/drivers/misc/mediatek/trusted_mem/ssmr
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/trusted_mem/public
ccflags-y += -DCONFIG_MTK_ENABLE_GENIEZONE=1
ccflags-y += -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion

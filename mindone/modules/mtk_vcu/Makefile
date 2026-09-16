# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += mtk-vcu.o
mtk-vcu-y += mtk_vcu.o mtk_vcodec_mem.o
# 🔴 MINDONE-VCU-IOMMU (F3270): without this symbol ALL SEVEN IOMMU blocks are compiled
# out of the module, including the io_domain lookup and the 34-bit DMA mask setup.
# The vcodec domain on mt6789 is [4 GB, 8 GB) (mtk_iommu.c, mt6789_multi_dom[1]); with the
# block compiled out the device is left with a 32-bit mask -> no overlap -> ANY IOVA
# allocation fails (log: dma alloc of size 4096 failed).
# The kernel config has `# CONFIG_MTK_IOMMU is not set`, and mtk_iommu is built as an
# external module, so the symbol has to be defined here.
ccflags-y += -DCONFIG_MTK_IOMMU=1 -DCONFIG_MTK_CMDQ_MBOX_EXT=1 -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src) -I$(src)/../include/drivers/misc/mediatek/smi -I$(src)/../include/drivers/misc/mediatek/cmdq -I$(src)/../include/arch/arm -I$(src)/../include/drivers/misc/mediatek/include/mt-plat -I$(src)/../include/drivers/misc/mediatek/vcp/rv -I$(src)/../include/drivers/misc/mediatek/vcp/include -I$(src)/../include/drivers/misc/mediatek/iommu -I$(src)/../include/drivers/dma-buf/heaps -I$(src)/../include/drivers/misc/mediatek/trusted_mem
ccflags-y += -I$(src)/../include
ccflags-y += -I$(src)/../include/include

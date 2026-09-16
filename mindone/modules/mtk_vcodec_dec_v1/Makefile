# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += mtk_vcodec_dec_v1.o
mtk_vcodec_dec_v1-objs := mtk_vcodec_dec_pm_plat.o vcodec_dvfs.o mtk-vcodec-dec.o mtk_vcodec_dec_drv.o vdec_drv_if.o mtk_vcodec_dec_pm.o vdec/vdec_common_if.o vdec/vdec_vcu_if.o vdec/vdec_vcp_if.o
# 🔴 MINDONE-VCODEC-IOMMU (F3270): without this symbol the io_domain lookup and the
# 34-bit DMA mask setup get compiled out of the module. The vcodec domain on mt6789 is
# [4 GB, 8 GB); with a 32-bit mask there is no overlap, so IOVA allocation always fails.
ccflags-y += -DCONFIG_MTK_IOMMU=1
ccflags-y += -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src) -DCONFIG_MTK_HANG_DETECT=1 -I$(src)/../include/drivers/media/platform/mtk-vcu -I$(src)/../include/drivers/media/platform/mtk-vcodec -I$(src)/../include/drivers/media/platform/mtk-videobuf -I$(src)/../include/drivers/misc/mediatek/slbc -I$(src)/../include/drivers/misc/mediatek/include/mt-plat -I$(src)/../include/drivers/misc/mediatek/iommu -I$(src)/../include/drivers/iommu -I$(src)/../include/arch/arm  -I$(src)/../include/drivers/misc/mediatek/mtk-interconnect -I$(src)/../include/drivers/misc/mediatek/vcp/rv -I$(src)/../include/drivers/misc/mediatek/vcp/include -I$(src)/../include/drivers/dma-buf/heaps -I$(src)/../include/drivers/misc/mediatek/trusted_mem -DCONFIG_64BIT=1 -DCONFIG_MTK_AEE_FEATURE=1 -DCONFIG_MTK_TINYSYS_VCP_SUPPORT=1 -DCONFIG_VIDEO_MEDIATEK_VCU=1
ccflags-y += -I$(src)/../include
ccflags-y += -I$(src)/../include/include

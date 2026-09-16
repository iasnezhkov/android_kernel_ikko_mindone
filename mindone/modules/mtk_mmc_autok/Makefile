# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += mtk_mmc_autok.o
ccflags-y += -DCONFIG_FPGA_EARLY_PORTING=1 -DCONFIG_LEDS_CLASS=1 -DCONFIG_MMC_DEBUG=1 -DCONFIG_MMC_MTK_SW_CQHCI=1 -DCONFIG_MMC_MTK_SW_CQHCI_DEBUG=1 -DCONFIG_MMC_SDHCI_EXTERNAL_DMA=1 -DCONFIG_MTK_SPM_V4=1 -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src)  -I$(src)/../include/drivers/mmc/host


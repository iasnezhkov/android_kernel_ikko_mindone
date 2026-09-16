# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += mtk_vow.o
mtk_vow-objs := vow.o vow_scp.o
ccflags-y += -DMINDONE_NO_COMPAT_IOCTL=1 -DCONFIG_COMPAT=1 -DCONFIG_MTK_TINYSYS_SCP_SUPPORT=1 -DCONFIG_MTK_VOW_AMAZON_SUPPORT=1 -DCONFIG_MTK_VOW_DUAL_MIC_SUPPORT=1 -DCONFIG_MTK_VOW_GVA_SUPPORT=1 -DCONFIG_OF=1 -DCONFIG_PM=1 -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src) -I$(src)/../include/drivers/misc/mediatek/scp -I$(src)/../include/drivers/misc/mediatek/vow -DCONFIG_MTK_TINYSYS_SCP_LOGGER_SUPPORT=1
ccflags-y += -I$(src)/../include/include

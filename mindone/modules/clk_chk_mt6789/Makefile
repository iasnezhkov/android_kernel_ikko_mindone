# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += clk_chk_mt6789.o
clk_chk_mt6789-objs := clkchk-mt6789.o
ccflags-y += -DCONFIG_COMMON_CLK_MTK_FREQ_HOPPING_DEBUG=1 -DCONFIG_MTK_AEE_FEATURE=1 -DCONFIG_MTK_AEE_IPANIC=1 -DCONFIG_MTK_DEVAPC=1 -DCONFIG_MTK_DVFSRC_HELPER=1 -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src) -I$(src)/../include/drivers/misc/mediatek/include -I$(src)/../include/include/linux/soc/mediatek  -I$(src)/../include

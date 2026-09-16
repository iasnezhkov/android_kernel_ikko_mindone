# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
MTK_PLATFORM := mt6789
GPS_SRC_FOLDER := $(src)
MODULE_NAME := gps_drv_stp
obj-m += $(MODULE_NAME).o
GPS_DRV_CONTROL_LNA := y
ccflags-y += -DMTK_GENERIC_HAL -DCONFIG_GPSL5_SUPPORT -DCONFIG_MTK_GPS_EMI
ccflags-y += -DSOC_CO_CLOCK_FLAG=1 -DWMT_CREATE_NODE_DYNAMIC=1 -DREMOVE_MK_NODE=0
ccflags-y += -D_CONFIG_MTK_WCN_ARM64_ -DCONFIG_MTK_WCN_ARM64 -DWMT_IDC_SUPPORT=0 -DMTK_WCN_WMT_STP_EXP_SYMBOL_ABSTRACT
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/connectivity/common/common_main/include -I$(src)/../include/drivers/misc/mediatek/include/mt-plat
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/connectivity/common -I$(src)/../include/drivers/misc/mediatek/connectivity/common/common_main/linux/include
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/connectivity/common/common_main/include -I$(src)/../include/drivers/misc/mediatek/include/mt-plat
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/connectivity/common -I$(src)/../include/drivers/misc/mediatek/connectivity/common/common_main/linux/include
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/connectivity/common/common_main/core/include
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/connectivity/common/common_main/platform/include
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/connectivity/common/debug_utility
ccflags-y += -I$(GPS_SRC_FOLDER)/lna_ctrl/inc
ccflags-y += -I$(GPS_SRC_FOLDER)/
$(MODULE_NAME)-objs += stp_chrdev_gps.o
$(MODULE_NAME)-objs += stp_chrdev_gps2.o
$(MODULE_NAME)-objs += lna_ctrl/src/gps_lna_drv.o
$(MODULE_NAME)-objs += gps_emi.o
$(MODULE_NAME)-objs += fw_log_gps.o
ccflags-y += -I$(src)/../include

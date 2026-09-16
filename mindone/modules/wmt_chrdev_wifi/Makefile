# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
MODULE_NAME := wmt_chrdev_wifi
ccflags-y += -Wno-error -DCONFIG_MTK_CONNSYS_DEDICATED_LOG_PATH -DWMT_IDC_SUPPORT=0 -DCFG_ANDORID_CONNINFRA_SUPPORT=0 -DCFG_ANDORID_CONNINFRA_COREDUMP_SUPPORT=0
ccflags-y += -D MTK_WCN_WMT_STP_EXP_SYMBOL_ABSTRACT -D CREATE_NODE_DYNAMIC=1
ccflags-y += -I$(src) -I$(src)/include
ccflags-y += -I$(src)/../include/include
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/include/mt-plat
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/connectivity/common/common_main/include
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/connectivity/common/common_main/linux/include
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/connectivity/common/debug_utility
ccflags-y += 

obj-m += $(MODULE_NAME).o
$(MODULE_NAME)-objs += wmt_cdev_wifi.o
$(MODULE_NAME)-objs += fw_log_wifi.o
$(MODULE_NAME)-objs += fw_log_ics.o
$(MODULE_NAME)-objs += wlan_ring.o
ccflags-y += -I$(src)/../include

# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
BT_PLATFORM := connac1x
ccflags-y += -Wno-error -D CONNAC1X_CHIPID=connac1x -D FW_LOG_DEFAULT_ON=1 -D ENABLE_LOW_POWER_DEBUG=0
ccflags-y += -D MTK_WCN_WMT_STP_EXP_SYMBOL_ABSTRACT -D WMT_IDC_SUPPORT=0 -DCONFIG_MTK_CONNSYS_DEDICATED_LOG_PATH -D CREATE_NODE_DYNAMIC=1
ccflags-y += -I$(src)
ccflags-y += 
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/include/mt-plat
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/connectivity/common/common_main/include
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/connectivity/common/common_main/linux/include
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/connectivity/common/debug_utility
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/connectivity/bt/mt66xx/include/btif

MODULE_NAME := bt_drv_connac1x
obj-m += $(MODULE_NAME).o
$(MODULE_NAME)-objs += stp_chrdev_bt.o
$(MODULE_NAME)-objs += btmtk_dbg.o btmtk_dbg_tp_evt_if.o
$(MODULE_NAME)-objs += fw_log_bt.o

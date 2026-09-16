# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += mtk_chg_type_det.o
ccflags-y += -Wno-error -I$(src)  -I$(src)/../include/drivers/misc/mediatek/typec/tcpc/inc  
ccflags-y += -I$(src)/../include
# (12.09.2026): without CONFIG_TCPC_CLASS, tcpm.h substitutes an inline stub for
# tcpc_dev_get_by_name()->NULL -- "get tcpc dev fail", PD/PE fail to negotiate (500 mA
# current). Flags match tcpc_class.
ccflags-y += -DCONFIG_TCPC_CLASS=1 -DCONFIG_USB_POWER_DELIVERY=1

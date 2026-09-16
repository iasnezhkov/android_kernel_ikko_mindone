# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += mtk_charger_framework.o
mtk_charger_framework-objs := mtk_charger.o mtk_charger_intf.o mtk_basic_charger.o mtk_pulse_charger.o mtk_pe.o mtk_pe_hal.o pd_policy_manager.o pd_pe_policy.o mt5725_stub.o
ccflags-y += -DCONFIG_BATTERY_CW2217=1 -DCONFIG_DRM_MEDIATEK=1 -DCONFIG_MTK_DUAL_CHARGER_SUPPORT=1 -DCONFIG_POWER_EXT=1 -DCONFIG_PRIZE_CHARGE_CTRL_POLICY=1 -DCONFIG_PRIZE_GIGASET_CHARGE_RESTRICTION=1 -DCONFIG_PRIZE_HARDWARE_INFO=1 -DCONFIG_PRIZE_ONLY5W_DISPLAY_15W=1 -DCONFIG_PRIZE_REVERE_CHARGING_MODE=1 -DCONFIG_USBIF_COMPLIANCE=1 -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src) -I$(src)/../include/drivers/misc/mediatek/usb20 -I$(src)/../include/drivers/misc/mediatek/typec/tcpc/inc -I$(src)/../include/drivers/misc/mediatek/power_throttling -I$(src)/../include/drivers/misc/mediatek/include -I$(src)/../include/drivers/gpu/drm/mediatek/mediatek_v2 -I$(src)/../include/drivers/gpu/drm/mediatek/mediatek_v2 -I$(src)/../include/drivers/power/supply -I$(src)/../include/include/uapi
ccflags-y += -I$(src)/../include
# (12.09.2026): without CONFIG_TCPC_CLASS, tcpm.h substitutes an inline stub for
# tcpc_dev_get_by_name()->NULL -- "get tcpc dev fail", PD/PE fail to negotiate (500 mA
# current). Flags match tcpc_class.
ccflags-y += -DCONFIG_TCPC_CLASS=1 -DCONFIG_USB_POWER_DELIVERY=1

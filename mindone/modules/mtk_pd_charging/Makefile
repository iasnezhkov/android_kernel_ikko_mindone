obj-m += mtk_pd_charging.o
mtk_pd_charging-objs := mtk_pd.o mtk_pd_hal.o
ccflags-y += -Wno-error -I$(src)
ccflags-y += -I$(src)/../include
# (12.09.2026): without CONFIG_TCPC_CLASS, tcpm.h substitutes an inline stub for
# tcpc_dev_get_by_name()->NULL -- "get tcpc dev fail", PD/PE fail to negotiate (500 mA
# current). Flags match tcpc_class.
ccflags-y += -DCONFIG_TCPC_CLASS=1 -DCONFIG_USB_POWER_DELIVERY=1

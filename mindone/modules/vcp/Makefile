# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += vcp.o
vcp-objs := vcp_helper.o vcp_excep.o vcp_irq.o vcp_logger.o vcp_l1c.o vcp_vcpctl.o vcp_feature_table.o vcp_awake.o
ccflags-y += -DCONFIG_MTK_HANG_DETECT=1 -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src) -I$(src)/../include/drivers/misc/mediatek/vcp/include -I$(src)/../include/drivers/misc/mediatek/include -I$(src)/../include/drivers/misc/mediatek/vcp/rv -I$(src)/../include/drivers/misc/mediatek/include/mt-plat -I$(src)/../include/drivers/clk/mediatek
ccflags-y += -I$(src)/../include
ccflags-y += -I$(src)/../include/include

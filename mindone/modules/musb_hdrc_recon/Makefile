# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += musb_hdrc.o
musb_hdrc-objs := musb_glue_mt6789.o musb_core.o musb_virthub.o musb_host.o musb_gadget_ep0.o musb_gadget.o musb_trace.o musb_dr.o musb_debugfs.o mtk_qmu.o musb_qmu.o musbhsdma.o usb20_host.o
# MINDONE (01.09.2026, F3339 reconstruction): all -I paths below stay INSIDE our own
# (verified via the .o.d dependency files of the last successful build) were copied
# verbatim into vendor-headers/ (mt-plat/aee.h, spm/mtk_spm_resource_req.h) or are
# tcpci_config.h, tcpm_pd.h -- reused via the same pattern as extcon_mtk_usb).
ccflags-y += -DCONFIG_64BIT=1 -DCONFIG_DEBUG_FS=1 -DCONFIG_FOR_BRING_UP=1 -DCONFIG_MEDIATEK_SOLUTION=1 -DCONFIG_MTK_AEE_FEATURE=1 -DCONFIG_MTK_BASE_POWER=1 -DCONFIG_MTK_CHARGER=1 -DCONFIG_MTK_MUSB_DRV_36BIT=1 -DCONFIG_MTK_MUSB_DUAL_ROLE=1 -DCONFIG_MTK_MUSB_QMU_PURE_ZLP_SUPPORT=1 -DCONFIG_MTK_MUSB_QMU_SUPPORT=1 -DCONFIG_MTK_USB_TYPEC=1 -DCONFIG_OF=1 -DCONFIG_PHY_MTK_TPHY=1 -DCONFIG_PM=1 -DCONFIG_PRIZE_TYPEC_SM_NKJ=1 -DCONFIG_PROC_FS=1 -DCONFIG_SYSFS=1 -DCONFIG_TCPC_CLASS=1 -DCONFIG_U3_COMPLIANCE=1 -DCONFIG_USB_G_ANDROID=1 -DCONFIG_USB_MTK_HDRC=1 -DCONFIG_USB_MTK_OTG=1 -DCONFIG_USB_ROLE_SWITCH=1  -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src) -I$(src)/../include/drivers/misc/mediatek/include -I$(src)/../include/drivers/misc/mediatek/include/spm -I$(srctree)/drivers/usb/gadget/function
ccflags-y += -I$(src)/../include

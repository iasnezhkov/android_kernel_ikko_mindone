# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += ccci_md_all.o
ccci_md_all-objs := ccci_auxadc.o ccci_core.o ccci_bm.o ccci_modem.o modem_sys1.o udc/udc.o hif/ccci_hif.o port/port_cfg.o port/port_char.o port/port_ctlmsg.o port/port_udc.o port/port_ipc.o port/port_net.o port/port_poller.o port/port_proxy.o port/port_rpc.o port/port_smem.o port/port_sysmsg.o port/ccci_swtp.o fsm/ccci_fsm.o fsm/ccci_fsm_ioctl.o fsm/ccci_fsm_monitor.o fsm/ccci_fsm_poller.o fsm/ccci_fsm_ee.o fsm/ccci_fsm_sys.o fsm/mdee_dumper_v1.o fsm/mdee_dumper_v2.o fsm/mdee_dumper_v3.o fsm/mdee_dumper_v5.o platform/ccci_platform.o platform/ap_md_reg_dump.o platform/md_sys1_platform.o
ccflags-y += -DCONFIG_MTK_ECCCI_C2K_USB=1 -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -ferror-limit=0
ccflags-y += -I$(src) -I$(src)/inc -I$(src)/platform -I$(src)/hif -I$(src)/port -I$(src)/fsm -I$(src)/udc -I$(src)/udc/udc_lib_inc
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/ccmni  -I$(src)/../include/drivers/misc/mediatek/scp/include -I$(src)/../include/drivers/misc/mediatek/scp/rv  -I$(src)/../include/drivers/misc/mediatek/include -I$(src)/../include/drivers/misc/mediatek/mdpm -I$(src)/../include/drivers/misc/mediatek/pbm  -I$(src)/../include/include
ccflags-y += -DCCCI_KMODULE_ENABLE=1 -include $(src)/mtk_sip_svc_ext.h
obj-m += ccci_fsm_scp.o
ccci_fsm_scp-objs := fsm/ccci_fsm_scp.o
obj-m += ccci_cldma.o
ccci_cldma-objs := hif/ccci_cldma_plat.o hif/ccci_hif_cldma.o
ccflags-y += -DCONFIG_MTK_AEE_IPANIC=1 -DCONFIG_MTK_EMI=1
ccflags-y += -DCONFIG_MTK_TINYSYS_SCP_SUPPORT=1  # MINDONE-SCP 30.08 (F3186): FEATURE_SCP_CCCI_SUPPORT matches the reference platform; do NOT set CM4_SUPPORT (reference path uses mtk_ipi_*)
ccflags-y += -DCONFIG_MTK_PBM=1 -DCONFIG_MTK_IRQ_DBG=1  # MINDONE-PARITY 30.08 (F3186/F3189): remaining reference-platform integrations; providers on K6: mtk_pbm.ko aee_aed.ko irq-dbg.ko
# MINDONE-MD3 02.09 (F3501/F3503): the shipped stock ccci_md_all.ko carries "clear MODEM_CAP_SGIO flag for IRAT enable"
# (port_net.c, only under CONFIG_MTK_MD3_SUPPORT), so stock brings ccmni up with md_ab=0x620002 while ours had 0x620006:
# MODEM_CAP_SGIO left set => NETIF_F_SG on ccmni => every TCP payload skb is fragmented on the UL path. Stock: 0 modem
# exceptions with live data; ours: an exception 0.7 s after every ccmni open. Match the shipped build.
ccflags-y += -DCONFIG_MTK_MD3_SUPPORT=1
ccflags-y += -I$(src)/../include

# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += mediatek_drm.o
mediatek_drm-y := mtk_drm_drv.o mtk_disp_tdshp.o mtk_disp_color.o mtk_disp_ccorr.o mtk_disp_c3d.o mtk_disp_gamma.o mtk_disp_aal.o mtk_disp_dither.o mtk_disp_chist.o mtk_disp_ovl.o mtk_disp_rdma.o mtk_disp_rsz.o mtk_disp_wdma.o mtk_drm_crtc.o mtk_drm_ddp.o mtk_drm_ddp_addon.o mtk_drm_ddp_comp.o mtk_drm_fb.o mtk_drm_gem.o mtk_drm_plane.o mtk_dsi.o mtk_mipi_tx.o mtk_writeback.o mtk_fence.o mtk_drm_session.o mtk_dump.o mtk_debug.o mtk_layering_rule.o mtk_layering_rule_base.o mtk_rect.o mtk_drm_mmp.o mtk_drm_trace.o mtk_drm_helper.o mtk_drm_lowpower.o mtk_disp_postmask.o mtk_drm_assert.o mtk_drm_fbconsole.o mtk_disp_recovery.o mtk_disp_pmqos.o mtk_disp_dsc.o mtk_disp_cm.o mtk_disp_spr.o mtk_drm_arr.o mtk_disp_merge.o mtk_dmdp_aal.o mtk_fbconfig_kdebug.o mtk_disp_y2r0.o mtk_disp_dlo_async.o mtk_disp_dli_async.o mtk_disp_inlinerotate.o mtk_mmlsys_bypass_mode.o platform/mtk_drm_6789.o mtk_dp_intf.o mtk_dp.o mtk_dp_hal.o mtk_dp_debug.o mtk_disp_bdg.o
ccflags-y += -DCONFIG_MTK_SLBC_MODULE=1 -DCONFIG_INTERCONNECT_MTK_EXTENSION=1 -DCONFIG_DRM_MEDIATEK=1
ccflags-y += -DCONFIG_INTERCONNECT_MTK_MMQOS_COMMON=1
ccflags-y += -DCONFIG_MTK_DVFSRC=1 -DCONFIG_MTK_AEE_IPANIC=1 -DCONFIG_MMPROFILE=1
ccflags-y += -I$(src)/local_include
ccflags-y += -DCONFIG_MTK_CMDQ_MBOX_EXT=1

# MINDONE: tell the display driver that the bootloader did NOT light the panel.
# Without this the driver simply asserts it did - mtk_drm_is_enable_from_lk() is a
# hardcoded "return true" carrying the vendor's own comment "TODO: check if target
# CRTC has been turn on in LK" - and mtk_dsi_probe() then sets dsi->output_en = true
# unconditionally. mtk_output_dsi_enable() sees that flag, logs "dsi is initialized"
# and returns without ever calling mtk_dsi_enable(), so DSI_EN is never set. Measured
# consequence: DSI_CON_CTRL stays 0x9000000 (DSI_EN clear) even across screen wakes,
# DSI_BUSY latches in INTSTA (0x800007b0), mtk_dsi_poll_for_idle times out, the CMDQ
# thread parks on the GPR_TIMER_R4 event (998), the ESD check fails 20 times and the
# driver gives up - the panel stays black while composition keeps producing correct
# frames. This switch is the vendor's own, left commented out in mtk_drm_drv.h:24;
# set here in the Makefile so every translation unit sees it consistently.
ccflags-y += -DCONFIG_MTK_DISP_NO_LK=1
ccflags-y += -I$(src)/../include/include/uapi
ccflags-y += -I$(src)/../include/include

ccflags-y += -Wno-error -Wno-error=strict-prototypes -Werror=incompatible-pointer-types -Wno-error=int-conversion -I$(src)
ccflags-y += -DSHARE_WROT_SRAM -DNOT_SUPPORT_CABC_HW -DDISP_MDP_COLOR_ON -DCCORR_SUPPORT
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/include
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/include/mt-plat
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/smi
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/mmp
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/spi_slave_drv
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/mmdvfs
ccflags-y += -I$(srctree)/drivers/gpu/drm
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/cmdq/mailbox
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/iommu
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/mtk-interconnect
ccflags-y += -I$(src)/../include/drivers/gpu/drm/mediatek
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/slbc
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/clkbuf/v1/inc
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/cmdq/bridge
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/mdp
# mind_one 09.09.2026: flag DELIBERATELY REMOVED.
# Under it, the mtk_disp_set_hrt_bw() branch only serves MT6768/MT6761/MT6765, and for our
# MT6789 it does NOTHING, so the guaranteed display bandwidth (HRT) is never reported to
# the bus and DVFSRC keeps memory at the 800 MHz floor out of 4266 (F4082/F4087).
# The #else branch is exactly what this device needs, and it matches the device tree:
# dispsys_config has the interconnect-name 'disp_hrt_qos' and has NEITHER 'disp-perf-bw'
# NOR 'required-opps', which the branch under the flag requires.
# ccflags-y += -DCONFIG_MTK_FB_MMDVFS_SUPPORT=1
ccflags-y += -DCONFIG_MTK_SMI=1
ccflags-y += -I$(src)/../include
ccflags-y += -I$(src)/../include

# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += mtk_cm_mgr.o
ccflags-y += -DCONFIG_MTK_CM_IPI=1  # MINDONE-F3086: match stock — cm_mgr imports cm_mgr_to_sspm_command from mtk_cm_ipi, does not redefine it
mtk_cm_mgr-objs := mtk_cm_mgr_common.o
ccflags-y += -Wno-error -I$(src)  -I$(src)/../include/drivers/gpu/drm/mediatek/mediatek_v2 

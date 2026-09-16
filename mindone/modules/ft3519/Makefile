# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += ft3519.o
ft3519-y := focaltech_core.o focaltech_ex_fun.o focaltech_ex_mode.o focaltech_gesture.o focaltech_esdcheck.o focaltech_point_report_check.o focaltech_flash.o focaltech_i2c.o focaltech_upgrade_ft8201.o focaltech_upgrade_ft8201ab.o focaltech_upgrade_ft8205.o
ccflags-y += -Wno-error -I$(src) -DCONFIG_DRM_MEDIATEK_V2=1
ccflags-y += -I$(src)/../include/drivers/gpu/drm/mediatek/mediatek_v2
ccflags-y += -I$(src)/../include	# mindone/compat.h (6.12 wrappers)

# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += mtk_fpsgo.o
mtk_fpsgo-objs := fpsgo_main.o base/src/fpsgo_base.o base/src/fpsgo_sysfs.o base/src/fpsgo_cpu_policy.o composer/src/fps_composer.o fbt/src/fbt_cpu.o fbt/src/fbt_cpu_ctrl.o fbt/src/xgf.o fbt/src/uboost.o fbt/src/thermal_aware.o fbt/src/mini_top.o fbt/platform/fbt_cpu_platform.o fstb/fstb.o gbe/gbe1_main.o gbe/gbe2_main.o gbe/gbe_common.o gbe/gbe_sysfs.o
ccflags-y += -Wno-error -Wno-error=implicit-int -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -ferror-limit=0 -include linux/sched/cputime.h -I$(src)/../include
ccflags-y += -I$(src) -I$(src)/base/include -I$(src)/composer/include -I$(src)/fbt/include -I$(src)/fstb -I$(src)/gbe -I$(src)/mt-plat -I$(src)/utch
ccflags-y +=  -I$(src)/../include/drivers/gpu/drm/mediatek/mediatek_v2 -I$(src)/../include/drivers/misc/mediatek/include/mt-plat  -I$(src)/../include/drivers/misc/mediatek/sched -I$(srctree)/kernel -I$(src)/../include/drivers/misc/mediatek/performance/perf_ioctl
ccflags-y += -DCONFIG_MTK_FPSGO_V3=1 -DNR_FREQ_CPU=32

# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += mtk_pwm.o
mtk_pwm-y := mtk_pwm_main.o mtk_pwm_hal.o
ccflags-y += -Wno-error -I$(src) -I$(src)/pwm_v2_include
# revive (temporary reference-tree include paths)
ccflags-y +=  -I$(src)/../include/drivers/misc/mediatek/include 

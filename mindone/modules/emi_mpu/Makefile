# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += emi_mpu.o
emi_mpu-objs := emi_mpu_main.o emi_ctrl.o emi_mbw.o emi_elm.o
ccflags-y += -DCONFIG_MTK_AEE_IPANIC_MODULE=1 -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src)
ccflags-y += -DCONFIG_MTK_AEE_IPANIC=1
# revive (temporary reference-tree include paths)
ccflags-y +=  -I$(src)/../include/drivers/misc/mediatek/include 
ccflags-y += -I$(src)/../include

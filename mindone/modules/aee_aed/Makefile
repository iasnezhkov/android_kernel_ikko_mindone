obj-m += aee_aed.o
aee_aed-objs := aed-main.o aed-debug.o aee-common.o slog.o reboot-reason.o
ccflags-y += -DCONFIG_MTK_HANG_DETECT=1 -DCONFIG_MTK_AEE_AED=1 -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src) 
ccflags-y += -I$(src)/../include

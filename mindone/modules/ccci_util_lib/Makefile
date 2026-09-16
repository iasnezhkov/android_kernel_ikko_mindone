# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += ccci_util_lib.o
ccci_util_lib-objs := ccci_util_lib_fo.o ccci_util_lib_load_img.o ccci_util_lib_sys.o ccci_private_log.o ccci_util_lib_time.o ccci_util_lib_main.o ccci_util_ld_md_errno.o ccci_util_broadcast.o ccci_util_pin_broadcast.o ccci_util_sib.o ccci_util_lib_reserved_mem.o ccci_util_md_rat.o
ccflags-y += -I$(src) -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/include
ccflags-y += -DCONFIG_MTK_AEE_IPANIC=1
ccflags-y += -I$(src)/../include

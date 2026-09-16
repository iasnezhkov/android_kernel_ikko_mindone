# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += sec.o
sec-objs := sec_boot_core.o sec_mod_core.o sec_osal.o sec_mod.o sec_clk.o sec_lib.o sec_ao.o
sec-objs += hacc_lib.o hacc_export.o hacc_hk.o hacc_sk.o
ccflags-y += -I$(src) -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/masp/asfv3/inc
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/include

ccflags-y += -I$(src)/../include	# mindone/compat.h (F3784)

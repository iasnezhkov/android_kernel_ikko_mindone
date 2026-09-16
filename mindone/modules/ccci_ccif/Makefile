# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += ccci_ccif.o
ccci_ccif-objs := ccci_ringbuf.o ccci_hif_ccif.o
ccflags-y += -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src) -I$(src)/../include/drivers/misc/mediatek/eccci/inc -I$(src)/../include/drivers/misc/mediatek/eccci/platform  -I$(src)/../include/drivers/misc/mediatek/include -I$(src)/../include/drivers/misc/mediatek/eccci/hif  -I$(src)/../include/drivers/misc/mediatek/eccci/hif 

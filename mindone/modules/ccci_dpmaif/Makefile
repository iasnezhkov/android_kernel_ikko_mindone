# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += ccci_dpmaif.o
ccci_dpmaif-objs := ccci_hif_dpmaif.o dpmaif_drv.o ccci_hif_dpmaif_comm.o ccci_hif_dpmaif_v3.o dpmaif_drv_v3.o dpmaif_bat.o dpmaif_bat_v3.o dpmaif_debug.o net_speed.o ccci_hif_dpmaif_v1.o dpmaif_drv_v1.o net_pool.o md_spd_dvfs_fn.o md_spd_dvfs_method.o
ccflags-y += -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src) -I$(src)/../include/drivers/misc/mediatek/eccci/inc -I$(src)/../include/drivers/misc/mediatek/eccci/platform  -I$(src)/../include/drivers/misc/mediatek/include -I$(src)/../include/drivers/misc/mediatek/eccci/hif  -I$(src)/../include/drivers/misc/mediatek/eccci/hif 
ccflags-y += -I$(src)/../include

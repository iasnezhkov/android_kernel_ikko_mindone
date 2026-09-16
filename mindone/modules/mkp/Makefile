# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += mkp.o
mkp-objs := mkp_main.o policy.o mkp_hvc.o mkp_api.o ksym.o mkp_demo.o mkp_rbtree.o helper.o mkp_module.o mkp_pcpu.o
ccflags-y += -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src)  -I$(src)/../include/drivers/misc/mediatek/mkp 
ccflags-y += -I$(src)/../include

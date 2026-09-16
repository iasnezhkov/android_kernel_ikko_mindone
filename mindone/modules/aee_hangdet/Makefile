# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += aee_hangdet.o
ccflags-y += -DCONFIG_MTK_AEE_IPANIC_MODULE=1 -Wno-error -Wno-error=implicit-int -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src) -I$(src)/../include/drivers/misc/mediatek/aee/mrdump  -I$(src)/../include/drivers/misc/mediatek/include -I$(srctree)/kernel/ -include linux/sched/cputime.h
ccflags-y += -DCONFIG_MTK_AEE_IPANIC=1

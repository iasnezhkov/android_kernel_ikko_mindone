# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += mtk_tee_gpapi.o
mtk_tee_gpapi-objs := main.o
ccflags-y += -Wno-error -I$(src) -I$(src)/../include/drivers/tee/gpapi/public -I$(src)/../include/drivers/tee/gpapi/public/impl -I$(src)/../include/drivers/tee/gpapi/public/client

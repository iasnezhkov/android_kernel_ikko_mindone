# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += adsp.o
adsp-objs := adsp_core.o adsp_fs.o adsp_semaphore.o adsp_logger.o adsp_feature_table.o adsp_timesync.o adsp_awake.o adsp_reserved_mem.o adsp_platform.o adsp_mbox.o adsp_platform_driver.o adsp_excep.o
ccflags-y += -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src)  -I$(src)/../include/drivers/misc/mediatek/slbc -I$(src)/../include
ccflags-y += -I$(src)/../include/include
include $(src)/../include/mindone/flags-audio.mk

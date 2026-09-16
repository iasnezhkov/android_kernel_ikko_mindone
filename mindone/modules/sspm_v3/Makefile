# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += sspm_v3.o
sspm_v3-objs := sspm_sysfs.o sspm_helper.o sspm_plt.o sspm_logger_impl.o sspm_timesync.o sspm_reservedmem.o sspm_emi_mpu.o sspm_ipi_mbox.o sspm_mbox.o
ccflags-y += -Wno-error -I$(src) -I$(src)/../include/drivers/misc/mediatek/sspm -I$(src)/../include/drivers/misc/mediatek/tinysys_scmi -I$(src)/../include/drivers/misc/mediatek/include
ccflags-y += -I$(src)/../include/include

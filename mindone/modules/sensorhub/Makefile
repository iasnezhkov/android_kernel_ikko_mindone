# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += sensorhub.o
sensorhub-objs := tiny_crc8.o ready.o ipi_comm.o sensor_comm.o sensor_list.o transceiver.o share_memory.o timesync.o debug.o custom_cmd.o
ccflags-y += -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src) -I$(src)/../include/drivers/misc/mediatek/sensor/2.0/core -I$(src)/../include/drivers/misc/mediatek/sensor/2.0/sensorhub -I$(src)/../include/drivers/misc/mediatek/scp/include
ccflags-y += -I$(src)/../include/include

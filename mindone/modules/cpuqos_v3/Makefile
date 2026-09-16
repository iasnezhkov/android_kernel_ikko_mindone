obj-m += cpuqos_v3.o
cpuqos_v3-objs := mpam_proto.o cpuqos_sys_common.o
ccflags-y += -Wno-error -Wno-error=implicit-int -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src)  -I$(srctree)/kernel/  -include linux/sched/cputime.h
ccflags-y += -I$(src)/../include

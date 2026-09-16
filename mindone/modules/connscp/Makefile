# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += connscp.o
connscp-objs := conn_scp.o conap_scp/conap_scp_core.o conap_scp/conap_scp_ipi.o conap_scp/conap_platform_data.o conap_scp/msg_thread.o
ccflags-y += -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src) -I$(src)/conap_scp -I$(src)/../include/include -I$(src)/../include

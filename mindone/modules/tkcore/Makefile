obj-m += tkcore.o
tkcore-objs := tee_core.o tee_context.o tee_session.o tee_shm.o tee_supp_com.o tee_sysfs.o tee_kernel_api.o tee_wait_queue.o tee_procfs.o tee_ta_mgmt.o peridev.o
ccflags-y += -DCONFIG_ARM64=1 -DCONFIG_TRUSTKERNEL_TEE_FP_SUPPORT=1 -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src) 

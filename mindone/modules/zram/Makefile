obj-m += zram.o
zram-objs := zcomp.o zram_drv.o
ccflags-y += -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src)  -DCONFIG_CDROM=1 -DCONFIG_CRYPTO_842=1 -DCONFIG_CRYPTO_LZ4=1 -DCONFIG_CRYPTO_LZ4HC=1 -DCONFIG_CRYPTO_ZSTD=1 -DCONFIG_SMP=1

obj-m += snd_soc_aw87xxx.o
snd_soc_aw87xxx-y := aw87xxx.o aw87xxx_device.o aw87xxx_monitor.o aw87xxx_bin_parse.o aw87xxx_dsp.o aw87xxx_acf_bin.o
ccflags-y += -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src)
ccflags-y += -I$(src)/../include
include $(src)/../include/mindone/flags-audio.mk

# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += clkbuf.o
clkbuf-y := mtk_clkbuf_common.o mtk_clkbuf_ctl.o mtk-clkbuf-pmif.o mtk-clkbuf-dcxo.o mtk-clkbuf-dcxo-6359p.o mtk-clkbuf-dcxo-6685.o mtk-clkbuf-dcxo-6366.o mtk-clkbuf-dcxo-6358.o mtk-clkbuf-dcxo-6357.o mtk-srclken-rc-hw.o mtk-srclken-rc-hw-v1.o
ccflags-y += -Wno-error -I$(src)
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/clkbuf/v1/inc

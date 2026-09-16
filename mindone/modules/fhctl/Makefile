obj-m += fhctl.o
fhctl-y += clk-fhctl.o clk-fhctl-ap.o clk-fhctl-pll.o clk-fhctl-debug.o
# The device tree selects method "fhctl-mcupm" for the whole `top` domain (all 12 PLLs, mfgpll
# included), and clk-fhctl.c wires fhctl_mcupm_init into subsys_init[] only under USE_FHCTL_MCUPM.
# Without this object the probe ends in "hdlr is NULL" and mtk_fh_set_rate stays NULL, so GPU DVFS
# falls back to raw MFGPLL_CON1 writes. Runtime deps: mcupm.ko (get_mcupm_ipidev) and
# mtk_tinysys_ipi.ko, both loaded before fhctl in the second stage.
fhctl-y += clk-fhctl-mcupm.o
ccflags-y += -DUSE_FHCTL_MCUPM -I$(src)/../include/drivers/misc/mediatek/mcupm
ccflags-y += -Wno-error -I$(src) -I$(srctree)/drivers/clk/mediatek -I$(src)/../include
ccflags-y += -I$(src)/../include/include

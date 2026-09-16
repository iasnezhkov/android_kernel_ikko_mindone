# F1445: clk-fmeter.o REMOVED - its symbol fmeter_set_ops already belongs to
#        the loaded clk_fmeter_mt6789.ko (live system: "exports duplicate symbol").
# F1444: ONLY vendor platform objects. The first seven from the vendor list
# (clk-mtk, clk-pll, clk-gate, clk-apmixed, clk-cpumux, reset, clk-mux)
# are already BUILT IN to our kernel (CONFIG_COMMON_CLK_MEDIATEK=y) - do not enable
# them here, that would duplicate the built-in driver (gate class 4, rule F408).
obj-m += clk_common.o
clk_common-objs := clkchk.o mtk-pd-chk.o clkdbg.o

# local_include FIRST: it holds our patched aee.h, where the fallback branch
# monitor_hang_regist_ldt is made static inline (in the vendor platform header it is a
# plain definition, which causes duplicate symbols at link time).
ccflags-y += -I$(src)/local_include -I$(src)/../include
ccflags-y += -I$(src)
ccflags-y += -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion

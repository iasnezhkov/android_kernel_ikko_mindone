obj-m += connfem.o
connfem-objs := connfem_module.o connfem_api.o connfem_container.o connfem_dt_parser.o connfem_epaelna.o connfem_subsys_bt.o connfem_subsys_wifi.o
ccflags-y += -I$(src) -I$(src)/include -D CONNFEM_DBG=1 -D CONNFEM_TEST_ENABLED=0
ccflags-y += -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion
ccflags-y += -I$(src)/../include

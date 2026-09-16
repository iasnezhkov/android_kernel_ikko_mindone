obj-m += mcDrvModule.o
mcDrvModule-objs := admin.o client.o clientlib.o clock.o fastcall.o iwp.o logging.o main.o mcp.o mmu.o nq.o protocol.o session.o teeclientapi.o user.o xen_be.o xen_common.o xen_fe.o
ccflags-y += -DCONFIG_ARM64=1 -DCONFIG_OF=1 -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src) 
ccflags-y += -I$(src)/../include
ccflags-y += -I$(src)/../include

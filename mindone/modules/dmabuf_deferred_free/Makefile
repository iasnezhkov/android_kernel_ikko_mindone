# DMA-BUF heap deferred-free helper (drivers/dma-buf/heaps/deferred-free-helper.c of the 6.1 kernel):
# built into the shipping 6.1 kernel, absent from 6.12 — provided as a module for the 6.12 set only.
obj-m += dmabuf_deferred_free.o
dmabuf_deferred_free-objs := deferred-free-helper.o
ccflags-y += -I$(src)/../include

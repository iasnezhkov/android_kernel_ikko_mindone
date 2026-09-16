# DMA-BUF heap page pool (drivers/dma-buf/heaps/page_pool.c of the 6.1 kernel): built into the
# shipping 6.1 kernel, absent from 6.12 — provided as a module for the 6.12 set only.
obj-m += dmabuf_page_pool.o
dmabuf_page_pool-objs := page_pool.o
ccflags-y += -I$(src)/../include

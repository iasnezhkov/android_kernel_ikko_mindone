# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += imgsensor_isp6s.o
imgsensor_isp6s-objs := common/v1_1/imgsensor.o common/v1_1/imgsensor_hw.o common/v1_1/imgsensor_i2c.o common/v1_1/imgsensor_legacy.o common/v1_1/imgsensor_proc.o common/v1_1/imgsensor_pwr_seq.o common/v1_1/imgsensor_sensor_list.o common/v1_1/imx766_mipi_raw/imx766mipiraw_Sensor.o common/v1_1/imx766_mipi_raw/imx766_eeprom.o common/v1_1/seninf_clk.o common/v1_1/seninf.o common/v1_1/camera_hw/imgsensor_cfg_table.o common/v1_1/camera_hw/regulator/regulator.o common/v1_1/camera_hw/gpio/gpio.o common/v1_1/camera_hw/mclk/mclk.o isp6s/seninf/seninf_impl.o common/v1_1/n3d_fsync/n3d.o common/v1_1/n3d_fsync/n3d_clk.o common/v1_1/n3d_fsync/n3d_hw.o common/v1_1/n3d_fsync/vsync_recorder.o common/v1_1/n3d_fsync/frame-sync/frame_sync.o common/v1_1/n3d_fsync/frame-sync/frame_sync_algo.o common/v1_1/n3d_fsync/frame-sync/frame_monitor.o

ccflags-y += -I$(src)
ccflags-y += -I$(src)/common/v1_1
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/imgsensor/inc
ccflags-y += -I$(src)/common/v1_1/camera_hw
ccflags-y += -I$(src)/isp6s/seninf
ccflags-y += -I$(src)/common/v1_1/n3d_fsync
ccflags-y += -I$(src)/common/v1_1/n3d_fsync/frame-sync
ccflags-y += -I$(src)/common/v1_1/imx766_mipi_raw
ccflags-y += -DNO_I2C_MTK -DNO_CLK_METER -DSENINF_USE_RPM -DDFS_CTRL_BY_OPP -DSENINF_N3D_SUPPORT -DIMX766_MIPI_RAW
ccflags-y += -Wno-error=format -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion
ccflags-y += -I$(src)/../include

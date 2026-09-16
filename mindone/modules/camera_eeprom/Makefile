# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += camera_eeprom.o
camera_eeprom-objs := eeprom_driver.o eeprom_i2c_common_driver.o eeprom_i2c_custom_driver.o cam_cal_list.o eeprom_i2c_dev.o

ccflags-y += -I$(src)
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/imgsensor/inc
ccflags-y += -I$(src)/../include/drivers/misc/mediatek/cam_cal/inc
ccflags-y += -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion
ccflags-y += -I$(src)/../include

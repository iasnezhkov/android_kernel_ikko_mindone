# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += cmdq_test.o
cmdq_test-objs := cmdq_test_main.o cmdq_record.o cmdq_helper_ext.o mdp_common.o cmdq_virtual.o cmdq_device.o mdp_event_common.o cmdq_subsys_common.o cmdq_mmp.o mdp_mt6789.o
ccflags-y += -DCONFIG_MTK_CMDQ_MBOX_EXT=1 -DCONFIG_MTK_SMI=1 -DCMDQ_MDP_COLOR -DMET_USER_EVENT_SUPPORT -DCMDQ_AEE_READY -DCMDQ_LARGE_MAX_FIRSTERROR_BUFFER -DCONFIG_MMPROFILE=1 -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src) -I$(src)/mach_stub  -I$(src)/../include/drivers/misc/mediatek/mtk-interconnect  -I$(src)/../include/drivers/misc/mediatek/mmp  -I$(src)/../include/drivers/misc/mediatek/cmdq/mailbox 
ccflags-y += -I$(src)/../include/include
ccflags-y += -I$(src)/../include

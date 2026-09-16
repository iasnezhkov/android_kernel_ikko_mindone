# ../include/: MediaTek platform headers mirrored at their kernel-tree paths (shared by all modules)
obj-m += snd_soc_mt6789_afe.o
snd_soc_mt6789_afe-objs := mt6789-afe-pcm.o mt6789-afe-clk.o mt6789-afe-gpio.o mt6789-dai-adda.o mt6789-afe-control.o mt6789-dai-i2s.o mt6789-dai-hw-gain.o mt6789-dai-src.o mt6789-dai-pcm.o mt6789-dai-hostless.o mt6789-misc-control.o
# 🔴 F3290/F3291/F3310: -DCONFIG_MTK_SCP_AUDIO=1 crashed the boot into PRELOADER here
# twice (struct mtk_base_afe_memif size mismatch vs snd_soc_mtk_common, which lacked the
# field). REMOVED 31.08 evening - now matches the actually flashed final73/74/75 (also
# without it) and mtk_scp_audio (F3310, same day, flag removed there too). DO NOT
# re-add without rebuilding/gating/flashing ALL THREE modules together (mtk_scp_audio,
# snd_soc_mtk_common, snd_soc_mt6789_afe) - see plan §0.35.
#
# 🔴 MINDONE-AFE-DSPAFE (F3312, 31.08 evening): mt6789-afe-pcm.c never called
# audio_set_dsp_afe() - other MediaTek SoC platforms call it right after AFE probe;
# ours simply never got it ported (class F3284, not a struct-layout mine). Without it
# local_dsp_afe in snd_soc_audiodsp_common stays NULL and the DSP audio path always
# fails (get_afe_base()==NULL, get_taskid_by_afe_daiid()==-1, afe_pcm_ipi_to_dsp()
# fails on entry for DL3/deep_buffer - reproduced 3/3 live tests). Call is gated behind
# module param mindone_dsp_afe (default 0, verified live unbind/bind, no flash) - see
# MINDONE-MODULES-NOTES-0901 for the full risk-class writeup.
ccflags-y += -I$(src)/../include/sound/soc/mediatek/mt6789
ccflags-y += -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src)  -I$(src)/../include/drivers/misc/mediatek/audio_ipi/include  -I$(src)/../include/sound/soc/mediatek/mt6789 -DCONFIG_MTK_HANG_DETECT=1
ccflags-y += -I$(src)/../include
include $(src)/../include/mindone/flags-audio.mk

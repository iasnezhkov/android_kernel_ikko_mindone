# Shared -DCONFIG_* for the "audio" group, which shares struct mtk_base_afe_memif
# (include/sound/soc/mediatek/common/mtk-base-afe.h) between modules:
# snd_soc_mtk_common, snd_soc_audiodsp_common, snd_soc_mt6789_afe, mt6789_mt6366,
# snd_soc_mt6366, snd_soc_aw87xxx, mtk_sp_spk_amp, mtk_btcvsd, mtk_afe_external,
# mtk_scp_audio, mtk_scp_audiocommon, audio_ipi, adsp, snd_soc_mtk_scp_ultra (14).
#
# This structure has EXACTLY one field gated by #if IS_ENABLED(CONFIG_MTK_SCP_AUDIO)
# (use_scp_share_mem, mtk-base-afe.h:220) - if one consumer module sees the structure
# WITH this field and another WITHOUT it, both compute a DIFFERENT sizeof() and
# DIFFERENT offsets for subsequent accesses to the SAME live memory (class F3460:
# afe_pcm_ipi_to_dsp[snd_soc_audiodsp_common] <- mtk_afe_fe_hw_params[snd_soc_mtk_common]).
#
# The value here MUST match across all Makefile consumers of this include.
# Currently - disabled (none of the fourteen actually has this flag; on 31.08 there was
# already an incident where it was enabled in only some of the modules, see the history
# of snd_soc_mt6789_afe/Makefile).
# Only enable together with an implementation of use_scp_share_mem (currently ported
# nowhere) and a synchronized edit of ALL fourteen Makefiles at once under this same gate.
MINDONE_AUDIO_SCP_AUDIO_FLAG :=
ccflags-y += $(MINDONE_AUDIO_SCP_AUDIO_FLAG)

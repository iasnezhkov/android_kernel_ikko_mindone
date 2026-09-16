obj-m += tkcore_drv.o
tkcore_drv-objs := tee_tz_drv.o tee_smc_xfer.o tee_mem.o handle.o
# 🔴 MINDONE-TEE-FPSUPPORT (F3281): without this symbol tee_smc_xfer.c compiles out the
# ONLY branch that handles the trusted-execution-environment non-preemptible callback
# (case T6SMC_RPC_CLKMGR_CMD -> tee_clkmgr_handle). Request 0xffff0006, which decodes
# exactly to it (function 6 = non-preemptible, GET_FUNC = 0 = CLKMGR), then falls into
# default and prints 'Unknown non-preempt rpc cmd' in a tight loop, followed by a cold
# reset (F3280). The neighboring tkcore module DEFINES this symbol - a mismatch within
# one build. tee_clkmgr_handle's provider is tkcore, which loads earlier
# (modules.load: 81 vs 82), so the import is safe.
ccflags-y += -DCONFIG_TRUSTKERNEL_TEE_FP_SUPPORT=1
# MINDONE-TEE-RPMBSUPPORT (P80/P45, 12.09): without this symbol tee_tz_drv.c compiles
# out the real handle_rpmb_cmd() (case TEE_RPC_RPMB_CMD) and uses a stub that just sets
# arg32->ret = TEEC_ERROR_NOT_IMPLEMENTED for every RPMB RPC from the secure world - this
# matches the observed tee_rpmb_init/internal_read_meta_file failures exactly (secure
# world asks the REE to relay an RPMB frame, REE refuses before even reaching teed). The
# real path only forwards the request to the userspace supplicant (tee_supp_cmd(...,
# TEE_RPC_ICMD_INVOKE, ...) -> teed, which is already running and waiting in
# tee_supp_read per F2605); it does not touch struct ufs_hba or ufshcd.c at all. The one
# extra include this branch needs (linux/tee_rpmb.h) is copied verbatim from the sibling
# tkcore module below - its only real content is gated on IN_KERNEL_RPMB_SUPPORT, which
# neither module defines, so the header compiles to nothing on its own.
# MINDONE (P126, F4231/F4240/F4254/F4270): the RPMB RPC path (TEE_RPC_RPMB_CMD) was disabled on
# 12.09 afternoon because it crashed teed startup (list_add corruption / NULL deref in
# __tee_shm_dma_buf_release: B16e 1 of 4 boots, B16f every boot). Root cause was not the list
# operator in tee_shm_realloc_from_rpc() but two independent owners of an RPC shm without
# coordination: teed's fd/dma-buf (released via tee_shm_free_io()) and the secure-world round
# (handle_rpmb_cmd(), released via tee_shm_realloc_from_rpc()/tee_shm_free_from_rpc()) - whichever
# finished second touched memory the first had already freed or re-listed. Fixed in
# tkcore/tee_shm.c + tee_core.h (all three mirrored copies of struct tee_shm) with independent
# rpc_round_owed / rpc_dmabuf_refs claims; the object is recycled or freed only when both are idle.
# RPMB RPC is enabled again (B23 4/4, B33 4/4 with 0 WARN). Transport (rpmb_mtk/ufs_mtk_rpmb) unchanged;
# the remaining "MAC mismatched" is the RPMB key question (P80).
ccflags-y += -DCONFIG_TRUSTKERNEL_TEE_RPMB_SUPPORT=1
ccflags-y += -Wno-error -Wno-error=strict-prototypes -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -I$(src)

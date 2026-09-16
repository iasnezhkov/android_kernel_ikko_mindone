/*
 * Copyright (c) 2015-2018 TrustKernel Incorporated
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef __TEE_CORE_DRV_H__
#define __TEE_CORE_DRV_H__

#include <linux/klist.h>
#include <linux/device.h>
#include <linux/file.h>
#include <linux/cdev.h>
#include <linux/miscdevice.h>
#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/scatterlist.h>

#include <linux/types.h>
#include <linux/tee_client_api.h>

#include <linux/err.h>
#include <linux/sched.h>

struct tee_cmd_io;
struct tee_shm_io;
struct tee_rpc;

enum tee_state {
	TEE_OFFLINE = 0,
	TEE_ONLINE = 1,
	TEE_SUSPENDED = 2,
	TEE_RUNNING = 3,
	TEE_CRASHED = 4,
	TEE_LAST = 5,
};

#define TEE_CONF_TEST_MODE		0x01000000
#define TEE_CONF_FW_NOT_CAPABLE		0x00000001

struct tee_stats_entry {
	int count;
	int max;
};

#define TEE_STATS_CONTEXT_IDX	0
#define TEE_STATS_SESSION_IDX	1
#define TEE_STATS_SHM_IDX		2

struct tee_version {
	uint32_t maj;
	uint32_t mid;
	uint32_t min;
};

struct tee_log {
	void *buffer;
	size_t length;
	int irq;
};

#define TEE_MAX_TEE_DEV_NAME (64)
struct tee {
	struct klist_node node;
	char name[TEE_MAX_TEE_DEV_NAME];
	int id;

	struct tee_version version;
	struct tee_log log;

	void *priv;
	const struct tee_ops *ops;
	struct device *dev;
	struct miscdevice miscdev;
	struct tee_rpc *rpc;

	atomic_t refcount;
	int max_refcount;
	struct tee_stats_entry stats[3];

	struct list_head list_ctx;
	struct list_head list_rpc_shm;
	struct mutex lock;

	unsigned int state;
	uint32_t shm_flags;	/* supported flags for shm allocation */
	uint32_t conf;
};

#define _DEV(tee) (tee->miscdev.this_device)

#define TEE_MAX_CLIENT_NAME (128)

/**
 * struct tee_context - internal structure to store a TEE context.
 *
 * @tee: tee attached to the tee_context
 * @usr_client: flag to known if the client is user side client
 * @entry: list of tee_context
 * @list_sess: list of tee_session that denotes all tee_session attached
 * @list_shm: list of tee_shm that denotes all tee_shm attached
 * @refcount: number of objects which reference it (including itself)
 */
struct tee_context {
	struct tee *tee;
	char name[TEE_MAX_CLIENT_NAME];
	int tgid;
	int usr_client;
	struct list_head entry;
	struct list_head list_sess;
	struct list_head list_shm;
	struct kref refcount;
};

/**
 * struct tee_session - internal structure to store a TEE session.
 *
 * @entry: list of tee_context
 * @ctx: tee_context attached to the tee_session
 * @sessid: session ID returned by the secure world
 * @priv: exporter specific private data for this buffer object
 */
struct tee_session {
	struct list_head entry;
	struct tee_context *ctx;
	uint32_t sessid;
	void *priv;
};

struct tee_shm_dma_buf {
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	bool tee_allocated;
};

struct tee_shm_resv {
	void *kaddr;
	dma_addr_t paddr;
	struct sg_table sgt;
	struct tee_shm_dma_buf *sdb;
};

struct tee_shm_ns {
	uint32_t token;
	struct page **pages;
	size_t nr_pages;
	atomic_t ref;
};

/**
 * struct tee_shm - internal structure to store a shm object.
 *
 * @ctx: tee_context attached to the buffer.
 * @tee: tee attached to the buffer.
 * @dev: device attached to the buffer.
 * @size_req: requested size for the buffer
 * @size_alloc: effective size of the buffer
 * @kaddr: kernel address if mapped kernel side
 * @paddr: physical address
 * @flags: flags which denote the type of the buffer
 * @entry: list of tee_shm
 */
struct tee_shm {
	struct list_head entry;

	struct tee_context *ctx;
	struct tee *tee;
	struct device *dev;

	size_t size_req;
	size_t size_alloc;

	uint32_t flags;

	/*
	 * MINDONE-TEE-SHMLIFE (follow-up B31): two INDEPENDENT counters of
	 * outstanding work on a TEE_SHM_FROM_RPC shm - meaningful for its entire
	 * lifetime, not just while checked out. tee_shm_rpc_maybe_finalize() in
	 * tee_shm.c (the only place that acts on them) recycles the object into
	 * tee->list_rpc_shm, or actually frees it, once BOTH are clear:
	 *
	 *   - rpc_round_owed: a secure-world round is currently using this
	 *     buffer. Armed by tee_shm_from_paddr() (handle_rpmb_cmd()'s lookup,
	 *     its only caller) at the moment the round starts; cleared by that
	 *     round's own tee_shm_realloc_from_rpc()/tee_shm_free_from_rpc()
	 *     call once it ends.
	 *   - rpc_dmabuf_refs: how many dma-bufs/fds teed currently holds for
	 *     this buffer. Incremented by tee_shm_fd_for_rpc() on export,
	 *     decremented by tee_shm_free_io() on release. Teed getting an fd
	 *     to service the round that's CURRENTLY using this buffer is the
	 *     normal, required case (handle_rpmb_cmd() -> tee_supp_cmd() ->
	 *     teed -> this ioctl -> teed mmaps and replies -> tee_supp_cmd()
	 *     returns -> tee_shm_realloc_from_rpc()) - so this is tracked fully
	 *     independently of rpc_round_owed, and tee_shm_fd_for_rpc() never
	 *     consults rpc_round_owed at all.
	 *
	 * The vendor code had no accounting at all: both sides unlinked/freed the shm
	 * unconditionally, so whichever ran second touched memory the other had
	 * already freed or relisted (F4231, F4232). Two earlier attempts at
	 * fixing that both got the split wrong:
	 *   - B23: a single shared `int rpc_claims`, armed to 1 at
	 *     alloc/recycle and incremented to 2 by tee_shm_fd_for_rpc(). This
	 *     conflated "the round is done" and "teed's fd is closed" into one
	 *     number with no way to tell which side a given decrement belonged
	 *     to; live RPMB-RPC traffic reproduced
	 *     WARN_ON(shm->rpc_claims <= 0) (harmless: the guard returned
	 *     early) from a decrement whose matching increment had gone to the
	 *     wrong logical side.
	 *   - B29: tried to close that gap with a `rpc_round_pending` flag that
	 *     tee_shm_fd_for_rpc() checked and REFUSED (-EBUSY) to check out a
	 *     buffer while set - on the wrong assumption that a concurrent
	 *     fd_for_rpc() during a round was an intruder racing in, rather
	 *     than that round's own necessary step. Since fd_for_rpc() is
	 *     needed to service every single round, this refused (and thereby
	 *     silently broke) EVERY RPMB round outright (B31: teed never got
	 *     rpmb_ioctl_tk_frames at all; tee_rpmb_get_dev_info's very first
	 *     round failed with TEE_ERROR_BAD_PARAMETERS).
	 * Keeping the round's claim and the fd's claim(s) as two separate
	 * fields - one whichever side finishes last checks the other and only
	 * acts once both are clear, and neither field's mutation can ever be
	 * mistaken for the other's - is what actually matches the protocol:
	 * fd_for_rpc() is unconditional and always succeeds when the buffer is
	 * findable at all; finalization waits on both without either side
	 * needing to know about or gate the other.
	 *
	 * Mirrored into ../tee_core.h's copy of this struct (kept identical - see
	 * tee_supp_com.c/tee_ta_mgmt.c, which #include "tee_core.h" instead of this
	 * file but only ever pass struct tee_shm * around opaquely) AND into
	 * ../../tkcore_drv/linux/tee_core.h's copy - tz_alloc() there
	 * (tee_tz_drv.c) does the actual devm_kzalloc(sizeof(struct tee_shm)) and
	 * writes shm->resv.kaddr/paddr for every shm object tkcore.ko later reads,
	 * so a layout skew between that copy and this one is not cosmetic: it
	 * shifted tkcore_drv's union offset 8 bytes short of tkcore.ko's, so
	 * tee_context_alloc_shm_tmp() read back resv.paddr (a physical carve-out
	 * address, e.g. 0xbe000000) as if it were resv.kaddr and fed it to
	 * copy_from_user() as the kernel destination - Oops in
	 * __arch_copy_from_user on the very first TEE session open, independent of
	 * CONFIG_TRUSTKERNEL_TEE_RPMB_SUPPORT (B19, fixed same day). All three
	 * copies of struct tee_shm MUST diff byte-identical after any edit here.
	 */
	bool rpc_round_owed;
	int rpc_dmabuf_refs;
	bool rpc_want_free;

	union {
		struct tee_shm_resv resv;
		struct tee_shm_ns ns;
	};
};

#define TEE_SHM_MAPPED			0x01000000
#define TEE_SHM_TEMP			0x02000000
#define TEE_SHM_FROM_RPC		0x04000000
#define TEE_SHM_REGISTERED		0x08000000
#define TEE_SHM_MEMREF			0x10000000
#define TEE_SHM_CACHED			0x20000000

#define TEE_SHM_DRV_PRIV_MASK		0xFF000000

struct tee_data {
	uint32_t type;
	uint32_t type_original;
	struct TEEC_SharedMemory c_shm[TEEC_CONFIG_PAYLOAD_REF_COUNT];
	union {
		struct tee_shm *shm;
		struct TEEC_Value value;
	} params[TEEC_CONFIG_PAYLOAD_REF_COUNT];
};

struct tee_cmd {
	TEEC_Result err;
	uint32_t origin;
	uint32_t cmd;
	struct tee_shm *uuid;
	struct tee_shm *ta;
	struct tee_data param;
};

void *tee_map_cached_shm(unsigned long pa, size_t len);
void tee_unmap_cached_shm(void *va);

struct tee_shm *tee_shm_alloc_from_rpc(struct tee *tee,
	size_t size, uint32_t extra_flags);
void tee_shm_realloc_from_rpc(struct tee *tee,
	struct tee_shm *shm);
void tee_shm_free_from_rpc(struct tee_shm *shm);

int tee_core_add(struct tee *tee);
int tee_core_del(struct tee *tee);

int __tee_get(struct tee *tee);

struct tee *tee_core_alloc(struct device *dev, char *name, int id,
			const struct tee_ops *ops, size_t len);
int tee_core_free(struct tee *tee);

#include <linux/tee_kernel_lowlevel_api.h>

struct tee_ops {
	struct module *owner;
	const char *type;

	int (*start)(struct tee *tee);
	int (*stop)(struct tee *tee);
	int (*open)(struct tee_session *sess, struct tee_cmd *cmd);
	int (*close)(struct tee_session *sess);
	int (*invoke)(struct tee_session *sess, struct tee_cmd *cmd);
	int (*cancel)(struct tee_session *sess, struct tee_cmd *cmd);
	struct tee_shm *(*alloc)(struct tee *tee, size_t size,
				  uint32_t flags);
	void (*free)(struct tee_shm *shm);
	int (*shm_inc_ref)(struct tee_shm *shm);
	void (*call_tee)(struct smc_param *p);
};


#endif /* __TEE_CORE_DRV_H__ */

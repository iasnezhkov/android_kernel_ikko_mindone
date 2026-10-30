/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_KINSTR_JM_H_
#define _KBASE_KINSTR_JM_H_

#include <uapi/gpu/arm/midgard/mali_kbase_kinstr_jm_reader.h>

#ifdef __KERNEL__
#include <linux/version.h>
#include <linux/static_key.h>
#else

#define static_branch_unlikely(key) (1)
#endif

struct kbase_context;
struct kbase_kinstr_jm;
struct kbase_jd_atom;
union kbase_kinstr_jm_fd;

int kbase_kinstr_jm_init(struct kbase_kinstr_jm **ctx);

void kbase_kinstr_jm_term(struct kbase_kinstr_jm *ctx);

int kbase_kinstr_jm_get_fd(struct kbase_kinstr_jm *const ctx, union kbase_kinstr_jm_fd *jm_fd_arg);

void kbasep_kinstr_jm_atom_state(struct kbase_jd_atom *const atom,
				 const enum kbase_kinstr_jm_reader_atom_state state);

extern struct static_key_false basep_kinstr_jm_reader_static_key;

static inline void kbase_kinstr_jm_atom_state(struct kbase_jd_atom *const atom,
					      const enum kbase_kinstr_jm_reader_atom_state state)
{
	if (static_branch_unlikely(&basep_kinstr_jm_reader_static_key))
		kbasep_kinstr_jm_atom_state(atom, state);
}

static inline void kbase_kinstr_jm_atom_state_queue(struct kbase_jd_atom *const atom)
{
	kbase_kinstr_jm_atom_state(atom, KBASE_KINSTR_JM_READER_ATOM_STATE_QUEUE);
}

static inline void kbase_kinstr_jm_atom_state_start(struct kbase_jd_atom *const atom)
{
	kbase_kinstr_jm_atom_state(atom, KBASE_KINSTR_JM_READER_ATOM_STATE_START);
}

static inline void kbase_kinstr_jm_atom_state_stop(struct kbase_jd_atom *const atom)
{
	kbase_kinstr_jm_atom_state(atom, KBASE_KINSTR_JM_READER_ATOM_STATE_STOP);
}

static inline void kbase_kinstr_jm_atom_state_complete(struct kbase_jd_atom *const atom)
{
	kbase_kinstr_jm_atom_state(atom, KBASE_KINSTR_JM_READER_ATOM_STATE_COMPLETE);
}

static inline void kbase_kinstr_jm_atom_queue(struct kbase_jd_atom *const atom)
{
	kbase_kinstr_jm_atom_state_queue(atom);
}

static inline void kbase_kinstr_jm_atom_complete(struct kbase_jd_atom *const atom)
{
	kbase_kinstr_jm_atom_state_complete(atom);
}

static inline void kbase_kinstr_jm_atom_sw_start(struct kbase_jd_atom *const atom)
{
	kbase_kinstr_jm_atom_state_start(atom);
}

static inline void kbase_kinstr_jm_atom_sw_stop(struct kbase_jd_atom *const atom)
{
	kbase_kinstr_jm_atom_state_stop(atom);
}

void kbasep_kinstr_jm_atom_hw_submit(struct kbase_jd_atom *const atom);

static inline void kbase_kinstr_jm_atom_hw_submit(struct kbase_jd_atom *const atom)
{
	if (static_branch_unlikely(&basep_kinstr_jm_reader_static_key))
		kbasep_kinstr_jm_atom_hw_submit(atom);
}

void kbasep_kinstr_jm_atom_hw_release(struct kbase_jd_atom *const atom);

static inline void kbase_kinstr_jm_atom_hw_release(struct kbase_jd_atom *const atom)
{
	if (static_branch_unlikely(&basep_kinstr_jm_reader_static_key))
		kbasep_kinstr_jm_atom_hw_release(atom);
}

#endif

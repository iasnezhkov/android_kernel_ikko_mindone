/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KBASE_DUMMY_JOB_WORKAROUND_
#define _KBASE_DUMMY_JOB_WORKAROUND_

#define KBASE_DUMMY_JOB_WA_FLAG_SERIALIZE (1ull << 0)
#define KBASE_DUMMY_JOB_WA_FLAG_WAIT_POWERUP (1ull << 1)
#define KBASE_DUMMY_JOB_WA_FLAG_LOGICAL_SHADER_POWER (1ull << 2)

#define KBASE_DUMMY_JOB_WA_FLAGS                                                    \
	(KBASE_DUMMY_JOB_WA_FLAG_SERIALIZE | KBASE_DUMMY_JOB_WA_FLAG_WAIT_POWERUP | \
	 KBASE_DUMMY_JOB_WA_FLAG_LOGICAL_SHADER_POWER)

#if MALI_USE_CSF

static inline int kbase_dummy_job_wa_load(struct kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
	return 0;
}

static inline void kbase_dummy_job_wa_cleanup(struct kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
}

static inline int kbase_dummy_job_wa_execute(struct kbase_device *kbdev, u64 cores)
{
	CSTD_UNUSED(kbdev);
	CSTD_UNUSED(cores);
	return 0;
}

static inline bool kbase_dummy_job_wa_enabled(struct kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
	return false;
}

#else

int kbase_dummy_job_wa_load(struct kbase_device *kbdev);
void kbase_dummy_job_wa_cleanup(struct kbase_device *kbdev);
int kbase_dummy_job_wa_execute(struct kbase_device *kbdev, u64 cores);

static inline bool kbase_dummy_job_wa_enabled(struct kbase_device *kbdev)
{
	return (kbdev->dummy_job_wa.kctx != NULL);
}

#endif

#endif

// SPDX-License-Identifier: GPL-2.0
#include <mali_kbase.h>

void kbase_disjoint_init(struct kbase_device *kbdev)
{
	KBASE_DEBUG_ASSERT(kbdev != NULL);

}

void kbase_disjoint_event(struct kbase_device *kbdev)
{
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	atomic_inc(&kbdev->disjoint_event.count);
}

void kbase_disjoint_state_up(struct kbase_device *kbdev)
{
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	atomic_inc(&kbdev->disjoint_event.state);

	kbase_disjoint_event(kbdev);
}

void kbase_disjoint_state_down(struct kbase_device *kbdev)
{
	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(atomic_read(&kbdev->disjoint_event.state) > 0);

	kbase_disjoint_event(kbdev);

	atomic_dec(&kbdev->disjoint_event.state);
}

void kbase_disjoint_event_potential(struct kbase_device *kbdev)
{
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	if (atomic_read(&kbdev->disjoint_event.state))
		kbase_disjoint_event(kbdev);
}

u32 kbase_disjoint_event_get(struct kbase_device *kbdev)
{
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	return (u32)atomic_read(&kbdev->disjoint_event.count);
}
KBASE_EXPORT_TEST_API(kbase_disjoint_event_get);

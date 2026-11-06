// SPDX-License-Identifier: GPL-2.0
#include <mali_kbase.h>
#include <mali_kbase_debug.h>
#include <tl/mali_kbase_tracepoints.h>
#include <mali_linux_trace.h>

static struct base_jd_udata kbase_event_process(struct kbase_context *kctx,
						struct kbase_jd_atom *katom)
{
	struct base_jd_udata data;
	struct kbase_device *kbdev;

	lockdep_assert_held(&kctx->jctx.lock);

	KBASE_DEBUG_ASSERT(kctx != NULL);
	KBASE_DEBUG_ASSERT(katom != NULL);
	KBASE_DEBUG_ASSERT(katom->status == KBASE_JD_ATOM_STATE_COMPLETED);

	kbdev = kctx->kbdev;
	data = katom->udata;

	KBASE_TLSTREAM_TL_NRET_ATOM_CTX(kbdev, katom, kctx);
	KBASE_TLSTREAM_TL_DEL_ATOM(kbdev, katom);

	katom->status = KBASE_JD_ATOM_STATE_UNUSED;
	dev_dbg(kbdev->dev, "Atom %pK status to unused\n", (void *)katom);
	wake_up(&katom->completed);

	return data;
}

int kbase_event_dequeue(struct kbase_context *kctx, struct base_jd_event_v2 *uevent)
{
	struct kbase_jd_atom *atom;

	KBASE_DEBUG_ASSERT(kctx);

	mutex_lock(&kctx->event_mutex);

	if (list_empty(&kctx->event_list)) {
		if (!atomic_read(&kctx->event_closed)) {
			mutex_unlock(&kctx->event_mutex);
			return -1;
		}

		mutex_unlock(&kctx->event_mutex);
		uevent->event_code = BASE_JD_EVENT_DRV_TERMINATED;
		memset(&uevent->udata, 0, sizeof(uevent->udata));
		dev_dbg(kctx->kbdev->dev,
			"event system closed, returning BASE_JD_EVENT_DRV_TERMINATED(0x%X)\n",
			BASE_JD_EVENT_DRV_TERMINATED);
		return 0;
	}

	atomic_dec(&kctx->event_count);
	atom = list_entry(kctx->event_list.next, struct kbase_jd_atom, dep_item[0]);
	list_del(kctx->event_list.next);

	mutex_unlock(&kctx->event_mutex);

	dev_dbg(kctx->kbdev->dev, "event dequeuing %pK\n", (void *)atom);
	uevent->event_code = atom->event_code;

	uevent->atom_number = (atom - kctx->jctx.atoms);

	if (atom->core_req & BASE_JD_REQ_EXTERNAL_RESOURCES)
		kbase_jd_free_external_resources(atom);

	mutex_lock(&kctx->jctx.lock);
	uevent->udata = kbase_event_process(kctx, atom);
	mutex_unlock(&kctx->jctx.lock);

	return 0;
}

KBASE_EXPORT_TEST_API(kbase_event_dequeue);

static void kbase_event_process_noreport_worker(struct work_struct *data)
{
	struct kbase_jd_atom *katom = container_of(data, struct kbase_jd_atom, work);
	struct kbase_context *kctx = katom->kctx;

	if (katom->core_req & BASE_JD_REQ_EXTERNAL_RESOURCES)
		kbase_jd_free_external_resources(katom);

	mutex_lock(&kctx->jctx.lock);
	kbase_event_process(kctx, katom);
	mutex_unlock(&kctx->jctx.lock);
}

static void kbase_event_process_noreport(struct kbase_context *kctx, struct kbase_jd_atom *katom)
{
	if (katom->core_req & BASE_JD_REQ_EXTERNAL_RESOURCES) {
		INIT_WORK(&katom->work, kbase_event_process_noreport_worker);
		queue_work(kctx->event_workq, &katom->work);
	} else {
		kbase_event_process(kctx, katom);
	}
}

static int kbase_event_coalesce(struct kbase_context *kctx)
{
	const int event_count = kctx->event_coalesce_count;

	list_splice_tail_init(&kctx->event_coalesce_list, &kctx->event_list);
	kctx->event_coalesce_count = 0;

	return event_count;
}

void kbase_event_post(struct kbase_context *kctx, struct kbase_jd_atom *atom)
{
	struct kbase_device *kbdev = kctx->kbdev;

	dev_dbg(kbdev->dev, "Posting event for atom %pK\n", (void *)atom);

	if (WARN_ON(atom->status != KBASE_JD_ATOM_STATE_COMPLETED)) {
		dev_warn(kbdev->dev, "%s: Atom %d (%pK) not completed (status %d)\n", __func__,
			 kbase_jd_atom_id(atom->kctx, atom), atom->kctx, atom->status);
		return;
	}

	if (atom->core_req & BASE_JD_REQ_EVENT_ONLY_ON_FAILURE) {
		if (atom->event_code == BASE_JD_EVENT_DONE) {
			dev_dbg(kbdev->dev, "Suppressing event (atom done)\n");
			kbase_event_process_noreport(kctx, atom);
			return;
		}
	}

	if (atom->core_req & BASEP_JD_REQ_EVENT_NEVER) {
		dev_dbg(kbdev->dev, "Suppressing event (never)\n");
		kbase_event_process_noreport(kctx, atom);
		return;
	}
	KBASE_TLSTREAM_TL_ATTRIB_ATOM_STATE(kbdev, atom, TL_ATOM_STATE_POSTED);
	if (atom->core_req & BASE_JD_REQ_EVENT_COALESCE) {

		dev_dbg(kbdev->dev, "Deferring event (coalesced)\n");
		mutex_lock(&kctx->event_mutex);
		list_add_tail(&atom->dep_item[0], &kctx->event_coalesce_list);
		++kctx->event_coalesce_count;
		mutex_unlock(&kctx->event_mutex);
	} else {

		int event_count = 1;

		mutex_lock(&kctx->event_mutex);
		event_count += kbase_event_coalesce(kctx);
		list_add_tail(&atom->dep_item[0], &kctx->event_list);
		atomic_add(event_count, &kctx->event_count);
		mutex_unlock(&kctx->event_mutex);
		dev_dbg(kbdev->dev, "Reporting %d events\n", event_count);

		kbase_event_wakeup(kctx);

		trace_sysgraph(SGR_POST, kctx->id, kbase_jd_atom_id(kctx, atom));
	}
}
KBASE_EXPORT_TEST_API(kbase_event_post);

void kbase_event_close(struct kbase_context *kctx)
{
	mutex_lock(&kctx->event_mutex);
	atomic_set(&kctx->event_closed, true);
	mutex_unlock(&kctx->event_mutex);
	kbase_event_wakeup(kctx);
}

int kbase_event_init(struct kbase_context *kctx)
{
	KBASE_DEBUG_ASSERT(kctx);

	INIT_LIST_HEAD(&kctx->event_list);
	INIT_LIST_HEAD(&kctx->event_coalesce_list);
	mutex_init(&kctx->event_mutex);
	kctx->event_workq = alloc_workqueue("kbase_event", WQ_MEM_RECLAIM, 1);

	if (kctx->event_workq == NULL)
		return -EINVAL;

	return 0;
}

KBASE_EXPORT_TEST_API(kbase_event_init);

void kbase_event_cleanup(struct kbase_context *kctx)
{
	int event_count;

	KBASE_DEBUG_ASSERT(kctx);
	KBASE_DEBUG_ASSERT(kctx->event_workq);

	flush_workqueue(kctx->event_workq);
	destroy_workqueue(kctx->event_workq);

	event_count = kbase_event_coalesce(kctx);
	atomic_add(event_count, &kctx->event_count);

	while (!list_empty(&kctx->event_list)) {
		struct base_jd_event_v2 event;

		kbase_event_dequeue(kctx, &event);
	}
}

KBASE_EXPORT_TEST_API(kbase_event_cleanup);

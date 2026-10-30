// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
static struct device fixture_device;
static struct md_cooling_device devices[3];
static struct thermal_cooling_device cooling[3];
static struct md_cooling_data data;
static int modem_state;
static int dispatch_result;
static unsigned int dispatches, queries, traces;
static unsigned int messages[64];
static char events[512];
static unsigned int event_count;
static bool observe_dispatch;
static unsigned long observed_targets[3], observed_aggregate;
static enum md_cooling_status observed_status;

static void event(char value)
{
	assert(event_count + 1 < sizeof(events));
	events[event_count++] = value;
	events[event_count] = '\0';
}

static char lock_event(struct mutex *lock)
{
	if (lock == &mdc_data_lock)
		return 'D';
	if (lock == &md_cdev_list_lock)
		return 'L';
	assert(lock == &send_tmc_msg_lock);
	return 'S';
}

static void mutex_lock(struct mutex *lock)
{
	assert(!lock->held);
	lock->held = 1;
	lock->acquisitions++;
	event(lock_event(lock));
}

static void mutex_unlock(struct mutex *lock)
{
	assert(lock->held);
	lock->held = 0;
	event(lock_event(lock) + ('a' - 'A'));
}

static int exec_ccci_kern_func_by_md_id(int md_id, unsigned int id, char *buffer, unsigned int length)
{
	if (id == ID_GET_MD_STATE) {
		assert(md_id == 0 && !buffer && length == 0);
		assert(!mdc_data_lock.held && !send_tmc_msg_lock.held);
		queries++;
		event('Q');
		return modem_state;
	}
	assert(id == ID_THROTTLING_CFG);
	assert(md_id == MD_SYS1 && length == 4 && buffer);
	assert(mdc_data_lock.held && send_tmc_msg_lock.held && !md_cdev_list_lock.held);
	assert(dispatches < sizeof(messages) / sizeof(messages[0]));
	memcpy(&messages[dispatches++], buffer, sizeof(unsigned int));
	event('T');
	if (observe_dispatch) {
		for (unsigned int i = 0; i < 3; i++) {
			unsigned long value = 99;
			assert(md_cooling_get_cur_state(&cooling[i], &value) == 0);
			assert(value == observed_targets[i]);
		}
		assert(mdc->mutt_state == observed_aggregate);
		assert(mdc->status == observed_status);
	}
	return dispatch_result;
}

static void trace_md_mutt_limit(struct md_cooling_device *device, enum md_cooling_status status)
{
	assert(device && !mdc_data_lock.held && !md_cdev_list_lock.held && !send_tmc_msg_lock.held);
	assert(status <= MD_OFF);
	traces++;
	event('R');
}

static void insert(struct md_cooling_device *device)
{
	device->node.next = &md_cdev_list;
	device->node.prev = md_cdev_list.prev;
	md_cdev_list.prev->next = &device->node;
	md_cdev_list.prev = &device->node;
}

static void reset(void)
{
	memset(devices, 0, sizeof(devices));
	memset(cooling, 0, sizeof(cooling));
	memset(&data, 0, sizeof(data));
	mdc = &data;
	data.pa_num = 2;
	data.pdata = &md_cooling_pdata;
	md_cdev_list.next = md_cdev_list.prev = &md_cdev_list;
	for (unsigned int i = 0; i < 3; i++) {
		devices[i].type = MD_COOLING_TYPE_MUTT;
		devices[i].pa_id = i;
		devices[i].max_state = 9;
		devices[i].throttle = mutt_throttle;
		devices[i].dev = &fixture_device;
		devices[i].cdev = &cooling[i];
		cooling[i].devdata = &devices[i];
		if (i < 2)
			insert(&devices[i]);
	}
	modem_state = MD_STATE_READY;
	dispatch_result = 0;
	dispatches = queries = traces = event_count = 0;
	events[0] = '\0';
	observe_dispatch = false;
	mdc_data_lock.held = md_cdev_list_lock.held = send_tmc_msg_lock.held = 0;
	mdc_data_lock.acquisitions = md_cdev_list_lock.acquisitions = send_tmc_msg_lock.acquisitions = 0;
}

static enum md_cooling_status expected_status(unsigned long state)
{
	if (state == 9)
		return MD_NO_IMS;
	if (state == 8)
		return MD_IMS_ONLY;
	return state ? MD_LV_THROTTLE_ENABLED : MD_LV_THROTTLE_DISABLED;
}

static void initial(unsigned long first, unsigned long second)
{
	devices[0].target_state = first;
	devices[1].target_state = second;
	data.mutt_state = first > second ? first : second;
	data.status = expected_status(data.mutt_state);
}

static void observe(void)
{
	for (unsigned int i = 0; i < 3; i++)
		observed_targets[i] = devices[i].target_state;
	observed_aggregate = data.mutt_state;
	observed_status = data.status;
	observe_dispatch = true;
}

static void settled(unsigned long first, unsigned long second, unsigned long aggregate)
{
	unsigned long value;
	assert(md_cooling_get_cur_state(&cooling[0], &value) == 0 && value == first);
	assert(md_cooling_get_cur_state(&cooling[1], &value) == 0 && value == second);
	assert(data.mutt_state == aggregate && data.status == expected_status(aggregate));
	assert(!mdc_data_lock.held && !md_cdev_list_lock.held && !send_tmc_msg_lock.held);
}

static unsigned int expected_message(unsigned int pa, unsigned long state)
{
	return state ? (0x00000003U | ((state - 1) << 16) | (pa << 24)) : 0x00000103U;
}

static void normal_levels(void)
{
	reset();
	for (unsigned long state = 1; state <= 9; state++) {
		assert(md_cooling_set_cur_state(&cooling[1], state) == 0);
		assert(messages[state - 1] == expected_message(1, state));
		settled(0, state, state);
	}
	assert(md_cooling_set_cur_state(&cooling[1], 0) == 0);
	assert(messages[9] == 0x103 && dispatches == 10 && queries == 10 && traces == 10);
	settled(0, 0, 0);
}

static void unchanged(void)
{
	reset();
	initial(0, 5);
	assert(md_cooling_set_cur_state(&cooling[1], 5) == 0);
	assert(!dispatches && !queries && !traces && !event_count);
	settled(0, 5, 5);
	reset();
	assert(md_cooling_set_cur_state(&cooling[1], 0) == 0);
	assert(!dispatches && !queries && !event_count);
}

static void equal_aggregate(void)
{
	reset();
	initial(8, 3);
	assert(md_cooling_set_cur_state(&cooling[1], 6) == 0);
	settled(8, 6, 8);
	assert(!dispatches && !traces && queries == 1);
	assert(strcmp(events, "QDdDLlLld") == 0);
	assert(md_cooling_set_cur_state(&cooling[0], 1) == 0);
	assert(dispatches == 1 && messages[0] == expected_message(0, 6));
	settled(1, 6, 6);
}

static void peer_aggregate(void)
{
	reset();
	initial(6, 2);
	assert(md_cooling_set_cur_state(&cooling[1], 8) == 0);
	settled(6, 8, 8);
	assert(md_cooling_set_cur_state(&cooling[0], 9) == 0);
	settled(9, 8, 9);
	assert(md_cooling_set_cur_state(&cooling[0], 0) == 0);
	settled(0, 8, 8);
	assert(md_cooling_set_cur_state(&cooling[1], 0) == 0);
	settled(0, 0, 0);
	assert(dispatches == 4);
	assert(messages[0] == expected_message(1, 8));
	assert(messages[1] == expected_message(0, 9));
	assert(messages[2] == expected_message(0, 8));
	assert(messages[3] == 0x103);
}

static void sparse_aggregate(void)
{
	reset();
	data.pa_num = 3;
	devices[1].type = MD_COOLING_TYPE_TX_PWR;
	devices[1].target_state = 9;
	assert(md_cooling_set_cur_state(&cooling[0], 4) == 0);
	assert(data.mutt_state == 4 && devices[0].target_state == 4);
	assert(dispatches == 1 && messages[0] == expected_message(0, 4));
	assert(md_cdev_list_lock.acquisitions == 3);
}

static void failure_state(void)
{
	reset();
	initial(4, 0);
	dispatch_result = -EIO;
	assert(md_cooling_set_cur_state(&cooling[1], 8) == -EIO);
	settled(4, 0, 4);
	assert(dispatches == 1 && !traces && messages[0] == expected_message(1, 8));
	assert(strcmp(events, "QDdDLlLlSTsd") == 0);
}

static void inflight_state(void)
{
	reset();
	initial(4, 0);
	observe();
	assert(md_cooling_set_cur_state(&cooling[1], 8) == 0);
	settled(4, 8, 8);
	assert(dispatches == 1 && traces == 1);
}

static void failure_retry(void)
{
	reset();
	dispatch_result = -CCCI_ERR_MD_NOT_READY;
	assert(md_cooling_set_cur_state(&cooling[1], 6) == -CCCI_ERR_MD_NOT_READY);
	dispatch_result = 0;
	assert(md_cooling_set_cur_state(&cooling[1], 6) == 0);
	assert(dispatches == 2 && queries == 2 && traces == 1);
	assert(messages[0] == expected_message(1, 6) && messages[1] == messages[0]);
	settled(0, 6, 6);
	assert(md_cooling_set_cur_state(&cooling[1], 6) == 0);
	assert(dispatches == 2 && queries == 2);
}

static void release_retry(void)
{
	reset();
	initial(0, 7);
	dispatch_result = -CCCI_ERR_HIF_NOT_POWER_ON;
	assert(md_cooling_set_cur_state(&cooling[1], 0) == -CCCI_ERR_HIF_NOT_POWER_ON);
	settled(0, 7, 7);
	dispatch_result = 0;
	assert(md_cooling_set_cur_state(&cooling[1], 0) == 0);
	settled(0, 0, 0);
	assert(dispatches == 2 && messages[0] == 0x103 && messages[1] == 0x103);
	assert(md_cooling_set_cur_state(&cooling[1], 0) == 0 && dispatches == 2);
}

static void peer_failure(void)
{
	reset();
	initial(7, 2);
	dispatch_result = -EAGAIN;
	assert(md_cooling_set_cur_state(&cooling[0], 1) == -EAGAIN);
	settled(7, 2, 7);
	assert(messages[0] == expected_message(0, 2));
	dispatch_result = 0;
	assert(md_cooling_set_cur_state(&cooling[1], 4) == 0);
	settled(7, 4, 7);
	assert(dispatches == 1);
	assert(md_cooling_set_cur_state(&cooling[0], 1) == 0);
	settled(1, 4, 4);
	assert(dispatches == 2 && messages[1] == expected_message(0, 4));
}

static void real_errors(void)
{
	const int errors[] = { -EIO, -ENOMEM, -EAGAIN, -ENODEV, -CCCI_ERR_MD_NOT_READY,
		-CCCI_ERR_HIF_NOT_POWER_ON, -CCCI_ERR_ALLOCATE_MEMORY_FAIL,
		-CCCI_ERR_INVALID_LOGIC_CHANNEL_ID, 7 };
	reset();
	initial(4, 0);
	for (unsigned int i = 0; i < sizeof(errors) / sizeof(errors[0]); i++) {
		dispatch_result = errors[i];
		observe();
		assert(md_cooling_set_cur_state(&cooling[1], 9) == errors[i]);
		settled(4, 0, 4);
		assert(dispatches == i + 1 && !traces);
	}
	dispatch_result = 0;
	assert(md_cooling_set_cur_state(&cooling[1], 9) == 0);
	settled(4, 9, 9);
	assert(dispatches == sizeof(errors) / sizeof(errors[0]) + 1 && traces == 1);
}

static void modem_off(void)
{
	const int states[] = { MD_STATE_INVALID, MD_STATE_EXCEPTION };
	for (unsigned int i = 0; i < sizeof(states) / sizeof(states[0]); i++) {
		reset();
		initial(2, 5);
		modem_state = states[i];
		assert(md_cooling_set_cur_state(&cooling[1], 7) == 0);
		assert(devices[0].target_state == 2 && devices[1].target_state == 0 && data.mutt_state == 0);
		assert(data.status == MD_LV_THROTTLE_ENABLED);
		assert(!dispatches && queries == 1 && traces == 1);
		assert(strcmp(events, "QDdR") == 0);
	}
}

static void scg_off(void)
{
	reset();
	initial(2, 3);
	data.status = MD_SCG_OFF;
	assert(md_cooling_set_cur_state(&cooling[1], 7) == 0);
	assert(devices[1].target_state == 3 && data.mutt_state == 3 && data.status == MD_SCG_OFF);
	assert(!dispatches && !traces && queries == 1 && strcmp(events, "QDd") == 0);
}

static void validation(void)
{
	unsigned long value = 99;
	reset();
	assert(md_cooling_get_max_state(&cooling[1], &value) == 0 && value == 9);
	assert(md_cooling_set_cur_state(&cooling[1], 10) == -EINVAL);
	assert(md_cooling_set_cur_state(&cooling[1], (unsigned long)-1) == -EINVAL);
	devices[1].throttle = NULL;
	assert(md_cooling_set_cur_state(&cooling[1], 0) == -EINVAL);
	assert(!dispatches && !queries && !event_count);
}

static void locking(void)
{
	reset();
	assert(md_cooling_set_cur_state(&cooling[1], 4) == 0);
	assert(strcmp(events, "QDdDLlLlSTsdR") == 0);
	assert(mdc_data_lock.acquisitions == 2 && md_cdev_list_lock.acquisitions == 2 && send_tmc_msg_lock.acquisitions == 1);
	settled(0, 4, 4);
}

int main(int argc, char **argv)
{
	assert(argc == 2);
	if (!strcmp(argv[1], "normal-levels")) normal_levels();
	else if (!strcmp(argv[1], "unchanged")) unchanged();
	else if (!strcmp(argv[1], "equal-aggregate")) equal_aggregate();
	else if (!strcmp(argv[1], "peer-aggregate")) peer_aggregate();
	else if (!strcmp(argv[1], "sparse-aggregate")) sparse_aggregate();
	else if (!strcmp(argv[1], "failure-state")) failure_state();
	else if (!strcmp(argv[1], "inflight-state")) inflight_state();
	else if (!strcmp(argv[1], "failure-retry")) failure_retry();
	else if (!strcmp(argv[1], "release-retry")) release_retry();
	else if (!strcmp(argv[1], "peer-failure")) peer_failure();
	else if (!strcmp(argv[1], "real-errors")) real_errors();
	else if (!strcmp(argv[1], "modem-off")) modem_off();
	else if (!strcmp(argv[1], "scg-off")) scg_off();
	else if (!strcmp(argv[1], "validation")) validation();
	else if (!strcmp(argv[1], "locking")) locking();
	else assert(false);
	printf("PASS %s\n", argv[1]);
	return 0;
}

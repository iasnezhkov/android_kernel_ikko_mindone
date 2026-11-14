/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
enum { EV_SET, EV_NOTIFY };

struct ev {
	int kind;
	unsigned long action;
	int pin;
	int state;
	const char *name;
	unsigned long ms;
	int idx;
};

static struct ev evs[128];
static int nevs;
static int set_calls;
static int fail_set_at = -1;
static struct IMGSENSOR_HW hw;
static struct IMGSENSOR_SENSOR_LIST imx766 = { "imx766_mipi_raw" };
static struct IMGSENSOR_SENSOR sensor0 = { { IMGSENSOR_SENSOR_IDX_MAIN, &imx766 } };
static struct IMGSENSOR_SENSOR sensor1 = { { IMGSENSOR_SENSOR_IDX_SUB, &imx766 } };
static struct IMGSENSOR_SENSOR *release_by;
static unsigned long release_at;
static unsigned long released_ms;
static bool reopen_after_release;
static int hook_ticks;

static enum IMGSENSOR_RETURN mock_set(void *pinstance, enum IMGSENSOR_SENSOR_IDX idx, enum IMGSENSOR_HW_PIN pin,
				      enum IMGSENSOR_HW_PIN_STATE state)
{
	assert(nevs < (int)(sizeof(evs) / sizeof(evs[0])));
	if (set_calls++ == fail_set_at)
		return IMGSENSOR_RETURN_ERROR;
	evs[nevs++] = (struct ev){ EV_SET, 0, pin, state, NULL, clock_ms, idx };
	return IMGSENSOR_RETURN_SUCCESS;
}

static struct IMGSENSOR_HW_DEVICE dev_mclk = { IMGSENSOR_HW_ID_MCLK, NULL, mock_set };
static struct IMGSENSOR_HW_DEVICE dev_reg = { IMGSENSOR_HW_ID_REGULATOR, NULL, mock_set };
static struct IMGSENSOR_HW_DEVICE dev_gpio = { IMGSENSOR_HW_ID_GPIO, NULL, mock_set };

static int listener(struct notifier_block *nb, unsigned long action, void *data)
{
	const struct imgsensor_power_event *event = data;

	assert(nevs < (int)(sizeof(evs) / sizeof(evs[0])));
	assert(event->sensor_idx == IMGSENSOR_SENSOR_IDX_MAIN || event->sensor_idx == IMGSENSOR_SENSOR_IDX_SUB);
	assert(hw.power_mutex.locked);
	evs[nevs++] = (struct ev){ EV_NOTIFY, action, -1, 0, event->pin, clock_ms, event->sensor_idx };
	return 0;
}

static struct notifier_block nb = { .notifier_call = listener };

static void owner_hook(void)
{
	assert(hw.power_wait.waiters == 1 && !hw.power_mutex.locked);
	hook_ticks++;
	if (!release_by || clock_ms != release_at)
		return;
	assert(imgsensor_hw_power(&hw, release_by, IMGSENSOR_HW_POWER_STATUS_OFF) == IMGSENSOR_RETURN_SUCCESS);
	if (reopen_after_release)
		assert(imgsensor_hw_power(&hw, release_by, IMGSENSOR_HW_POWER_STATUS_ON) == IMGSENSOR_RETURN_SUCCESS);
	released_ms = clock_ms;
}

static void setup(void)
{
	static const struct IMGSENSOR_HW_POWER_SEQ imx = {
		"imx766_mipi_raw",
		{
			{ IMGSENSOR_HW_PIN_PDN, IMGSENSOR_HW_PIN_STATE_LEVEL_0, 0, IMGSENSOR_HW_PIN_STATE_LEVEL_0, 0 },
			{ IMGSENSOR_HW_PIN_RST, IMGSENSOR_HW_PIN_STATE_LEVEL_0, 1, IMGSENSOR_HW_PIN_STATE_LEVEL_0, 0 },
			{ IMGSENSOR_HW_PIN_AVDD, IMGSENSOR_HW_PIN_STATE_LEVEL_2800, 3, IMGSENSOR_HW_PIN_STATE_LEVEL_0, 0 },
			{ IMGSENSOR_HW_PIN_AVDD1, IMGSENSOR_HW_PIN_STATE_LEVEL_1800, 0, IMGSENSOR_HW_PIN_STATE_LEVEL_0, 0 },
			{ IMGSENSOR_HW_PIN_AFVDD, IMGSENSOR_HW_PIN_STATE_LEVEL_2800, 3, IMGSENSOR_HW_PIN_STATE_LEVEL_0, 0 },
			{ IMGSENSOR_HW_PIN_DVDD, IMGSENSOR_HW_PIN_STATE_LEVEL_1100, 4, IMGSENSOR_HW_PIN_STATE_LEVEL_0, 0 },
			{ IMGSENSOR_HW_PIN_DOVDD, IMGSENSOR_HW_PIN_STATE_LEVEL_1800, 1, IMGSENSOR_HW_PIN_STATE_LEVEL_0, 0 },
			{ IMGSENSOR_HW_PIN_MCLK, IMGSENSOR_HW_PIN_STATE_LEVEL_HIGH, 6, IMGSENSOR_HW_PIN_STATE_LEVEL_0, 0 },
			{ IMGSENSOR_HW_PIN_PDN, IMGSENSOR_HW_PIN_STATE_LEVEL_HIGH, 0, IMGSENSOR_HW_PIN_STATE_LEVEL_0, 0 },
			{ IMGSENSOR_HW_PIN_RST, IMGSENSOR_HW_PIN_STATE_LEVEL_HIGH, 5, IMGSENSOR_HW_PIN_STATE_LEVEL_0, 0 },
		},
		0,
	};
	int idx, pin;

	memset(&hw, 0, sizeof(hw));
	init_waitqueue_head(&hw.power_wait);
	hw.pdev[IMGSENSOR_HW_ID_MCLK] = &dev_mclk;
	hw.pdev[IMGSENSOR_HW_ID_REGULATOR] = &dev_reg;
	hw.pdev[IMGSENSOR_HW_ID_GPIO] = &dev_gpio;
	hw.power_owner = IMGSENSOR_SENSOR_IDX_NONE;
	hw.g_platform_id = 0x6789;
	for (idx = 0; idx < IMGSENSOR_SENSOR_IDX_MAX_NUM; idx++)
		for (pin = 0; pin < IMGSENSOR_HW_PIN_MAX_NUM; pin++)
			hw.sensor_pwr[idx].id[pin] = IMGSENSOR_HW_ID_GPIO;
	for (idx = 0; idx < 2; idx++) {
		hw.sensor_pwr[idx].id[IMGSENSOR_HW_PIN_MCLK] = IMGSENSOR_HW_ID_MCLK;
		hw.sensor_pwr[idx].id[IMGSENSOR_HW_PIN_AVDD1] = IMGSENSOR_HW_ID_REGULATOR;
		hw.sensor_pwr[idx].id[IMGSENSOR_HW_PIN_MIPI_SWITCH_EN] = IMGSENSOR_HW_ID_MAX_NUM;
	}
	memset(platform_power_sequence, 0, sizeof(platform_power_sequence));
	memset(sensor_power_sequence, 0, sizeof(sensor_power_sequence));
	sensor_power_sequence[0] = imx;
	nevs = 0;
	set_calls = 0;
	fail_set_at = -1;
	clock_ms = 0;
	log_buf[0] = '\0';
	wait_hook = owner_hook;
	release_by = NULL;
	release_at = 0;
	released_ms = 0;
	reopen_after_release = false;
	hook_ticks = 0;
}

static void use_platform_sequence(void)
{
	platform_power_sequence[0].name = PLATFORM_POWER_SEQ_NAME;
	platform_power_sequence[0]._idx = IMGSENSOR_SENSOR_IDX_MAIN;
	platform_power_sequence[0].pwr_info[0] = (struct IMGSENSOR_HW_POWER_INFO){
		IMGSENSOR_HW_PIN_MIPI_SWITCH_SEL, IMGSENSOR_HW_PIN_STATE_LEVEL_HIGH, 1, IMGSENSOR_HW_PIN_STATE_LEVEL_0, 0 };
	platform_power_sequence[0].pwr_info[1] = (struct IMGSENSOR_HW_POWER_INFO){
		IMGSENSOR_HW_PIN_MIPI_SWITCH_EN, IMGSENSOR_HW_PIN_STATE_LEVEL_HIGH, 1, IMGSENSOR_HW_PIN_STATE_LEVEL_0, 0 };
}

static int count(int kind, unsigned long action)
{
	int i, n = 0;

	for (i = 0; i < nevs; i++)
		if (evs[i].kind == kind && (kind == EV_SET || evs[i].action == action))
			n++;
	return n;
}

static void check_on_trace(int sets)
{
	int i;

	assert(count(EV_SET, 0) == sets && count(EV_NOTIFY, IMGSENSOR_POWER_PIN_ON) == sets);
	for (i = 0; i < 2 * sets; i += 2) {
		assert(evs[i].kind == EV_SET && evs[i + 1].kind == EV_NOTIFY);
		assert(evs[i + 1].action == IMGSENSOR_POWER_PIN_ON);
		assert(!strcmp(evs[i + 1].name, imgsensor_hw_pin_names[evs[i].pin]));
		assert(evs[i + 1].ms == evs[i].ms);
	}
	assert(nevs == 2 * sets + 1 && evs[nevs - 1].kind == EV_NOTIFY && evs[nevs - 1].action == IMGSENSOR_POWER_ON);
}

static void case_imgsensor(void)
{
	unsigned long afvdd_ms = 0;
	int i, last_on, first_off;

	assert(!imgsensor_power_register_notifier(&nb));
	setup();
	assert(imgsensor_hw_power(&hw, &sensor0, IMGSENSOR_HW_POWER_STATUS_ON) == IMGSENSOR_RETURN_SUCCESS);
	check_on_trace(10);
	for (i = 0; i < nevs; i++)
		if (evs[i].kind == EV_NOTIFY && evs[i].name && !strcmp(evs[i].name, "vcamaf"))
			afvdd_ms = evs[i].ms;
	assert(afvdd_ms == 4);
	assert(hw.power_owner == IMGSENSOR_SENSOR_IDX_MAIN);

	nevs = 0;
	assert(imgsensor_hw_power(&hw, &sensor1, IMGSENSOR_HW_POWER_STATUS_ON) == IMGSENSOR_RETURN_BUSY);
	assert(imgsensor_hw_power(&hw, &sensor1, IMGSENSOR_HW_POWER_STATUS_OFF) == IMGSENSOR_RETURN_SUCCESS);
	assert(nevs == 0);

	assert(imgsensor_hw_power(&hw, &sensor0, IMGSENSOR_HW_POWER_STATUS_OFF) == IMGSENSOR_RETURN_SUCCESS);
	assert(nevs == 11 && evs[0].kind == EV_NOTIFY && evs[0].action == IMGSENSOR_POWER_OFF && !evs[0].name);
	for (i = 1; i < nevs; i++)
		assert(evs[i].kind == EV_SET);
	assert(hw.power_owner == IMGSENSOR_SENSOR_IDX_NONE);

	setup();
	fail_set_at = 5;
	assert(imgsensor_hw_power(&hw, &sensor0, IMGSENSOR_HW_POWER_STATUS_ON) == IMGSENSOR_RETURN_ERROR);
	assert(count(EV_NOTIFY, IMGSENSOR_POWER_ON) == 0 && count(EV_NOTIFY, IMGSENSOR_POWER_PIN_ON) == 5);
	last_on = -1;
	first_off = -1;
	for (i = 0; i < nevs; i++) {
		if (evs[i].kind == EV_NOTIFY && evs[i].action == IMGSENSOR_POWER_PIN_ON)
			last_on = i;
		if (evs[i].kind == EV_NOTIFY && evs[i].action == IMGSENSOR_POWER_OFF && first_off < 0)
			first_off = i;
	}
	assert(first_off == nevs - 1 && last_on < first_off);
	assert(hw.power_owner == IMGSENSOR_SENSOR_IDX_NONE);

	setup();
	use_platform_sequence();
	assert(imgsensor_hw_power(&hw, &sensor0, IMGSENSOR_HW_POWER_STATUS_ON) == IMGSENSOR_RETURN_SUCCESS);
	check_on_trace(11);
	assert(evs[0].pin == IMGSENSOR_HW_PIN_MIPI_SWITCH_SEL);
	assert(imgsensor_hw_power(&hw, &sensor0, IMGSENSOR_HW_POWER_STATUS_OFF) == IMGSENSOR_RETURN_SUCCESS);

	setup();
	use_platform_sequence();
	fail_set_at = 0;
	assert(imgsensor_hw_power(&hw, &sensor0, IMGSENSOR_HW_POWER_STATUS_ON) == IMGSENSOR_RETURN_ERROR);
	assert(count(EV_NOTIFY, IMGSENSOR_POWER_OFF) == 1 && count(EV_NOTIFY, IMGSENSOR_POWER_ON) == 0);
	assert(count(EV_NOTIFY, IMGSENSOR_POWER_PIN_ON) == 0);
	assert(evs[nevs - 1].kind == EV_NOTIFY && evs[nevs - 1].action == IMGSENSOR_POWER_OFF);
	assert(!hw.power_mutex.locked && hw.power_owner == IMGSENSOR_SENSOR_IDX_NONE);

	assert(!imgsensor_power_unregister_notifier(&nb));
	setup();
	assert(imgsensor_hw_power(&hw, &sensor0, IMGSENSOR_HW_POWER_STATUS_ON) == IMGSENSOR_RETURN_SUCCESS);
	assert(count(EV_NOTIFY, IMGSENSOR_POWER_ON) == 0);
	printf("PASS imgsensor: each pin that turns on is announced right after its set and before its delay (vcamaf at "
	       "%lu ms), ON follows the last pin, OFF precedes the first pin off, a failed or refused power-up announces OFF "
	       "and never ON, a refused or foreign power-off announces nothing\n", afvdd_ms);
}

static void case_wait(void)
{
	char expect[160];
	enum IMGSENSOR_RETURN ret;
	unsigned long start, waited;
	int i, wakes;

	assert(!imgsensor_power_register_notifier(&nb));
	setup();
	assert(imgsensor_hw_power(&hw, &sensor0, IMGSENSOR_HW_POWER_STATUS_ON) == IMGSENSOR_RETURN_SUCCESS);
	assert(hook_ticks == 0 && !log_buf[0]);
	nevs = 0;
	start = clock_ms;
	release_by = &sensor0;
	release_at = start + 40;
	ret = imgsensor_hw_power(&hw, &sensor1, IMGSENSOR_HW_POWER_STATUS_ON);
	assert(ret == IMGSENSOR_RETURN_SUCCESS && hw.power_owner == IMGSENSOR_SENSOR_IDX_SUB);
	assert(hook_ticks == 40 && released_ms > release_at);
	assert(evs[0].kind == EV_NOTIFY && evs[0].action == IMGSENSOR_POWER_OFF && evs[0].idx == IMGSENSOR_SENSOR_IDX_MAIN);
	for (i = 1; i <= 10; i++)
		assert(evs[i].kind == EV_SET && evs[i].idx == IMGSENSOR_SENSOR_IDX_MAIN && evs[i].ms <= released_ms);
	assert(evs[11].kind == EV_SET && evs[11].ms == released_ms);
	for (i = 11; i < nevs; i++)
		assert(evs[i].idx == IMGSENSOR_SENSOR_IDX_SUB && !(evs[i].kind == EV_NOTIFY && evs[i].action == IMGSENSOR_POWER_OFF));
	assert(nevs == 11 + 21 && evs[nevs - 1].kind == EV_NOTIFY && evs[nevs - 1].action == IMGSENSOR_POWER_ON);
	assert(hw.power_wait.waiters == 0 && hw.power_wait.wakes == 1 && !hw.power_mutex.locked);
	waited = released_ms - start;
	snprintf(expect, sizeof(expect),
		 "imgsensor_hw_power: sensor 1 waited %lu ms for sensor 0 to release the shared supplies\n", waited);
	assert(!strcmp(log_buf, expect));

	setup();
	assert(imgsensor_hw_power(&hw, &sensor0, IMGSENSOR_HW_POWER_STATUS_ON) == IMGSENSOR_RETURN_SUCCESS);
	nevs = 0;
	start = clock_ms;
	assert(imgsensor_hw_power(&hw, &sensor1, IMGSENSOR_HW_POWER_STATUS_ON) == IMGSENSOR_RETURN_BUSY);
	assert(clock_ms - start == 500 && hook_ticks == 500);
	assert(nevs == 0 && hw.power_owner == IMGSENSOR_SENSOR_IDX_MAIN);
	assert(hw.power_wait.waiters == 0 && !hw.power_mutex.locked);
	assert(!strcmp(log_buf, "imgsensor_hw_power: sensor 1 refused, sensor 0 holds the shared supplies\n"));
	wakes = hw.power_wait.wakes;
	assert(imgsensor_hw_power(&hw, &sensor0, IMGSENSOR_HW_POWER_STATUS_OFF) == IMGSENSOR_RETURN_SUCCESS);
	assert(hw.power_wait.wakes == wakes + 1 && hw.power_owner == IMGSENSOR_SENSOR_IDX_NONE);
	hook_ticks = 0;
	assert(imgsensor_hw_power(&hw, &sensor1, IMGSENSOR_HW_POWER_STATUS_ON) == IMGSENSOR_RETURN_SUCCESS);
	assert(hook_ticks == 0 && hw.power_owner == IMGSENSOR_SENSOR_IDX_SUB);

	setup();
	assert(imgsensor_hw_power(&hw, &sensor0, IMGSENSOR_HW_POWER_STATUS_ON) == IMGSENSOR_RETURN_SUCCESS);
	nevs = 0;
	start = clock_ms;
	release_by = &sensor0;
	release_at = start + 40;
	reopen_after_release = true;
	assert(imgsensor_hw_power(&hw, &sensor1, IMGSENSOR_HW_POWER_STATUS_ON) == IMGSENSOR_RETURN_BUSY);
	assert(released_ms > release_at && clock_ms - start == 500);
	assert(hw.power_owner == IMGSENSOR_SENSOR_IDX_MAIN && hw.power_wait.wakes == 1 && hw.power_wait.waiters == 0);
	assert(count(EV_NOTIFY, IMGSENSOR_POWER_ON) == 1 && evs[nevs - 1].idx == IMGSENSOR_SENSOR_IDX_MAIN);
	assert(!strcmp(log_buf, "imgsensor_hw_power: sensor 1 refused, sensor 0 holds the shared supplies\n"));
	assert(!imgsensor_power_unregister_notifier(&nb));
	printf("PASS imgsensor wait: a power-on of sensor 1 while sensor 0 holds the supplies waits without the mutex, "
	       "starts at the moment sensor 0's power-off wakes it (%lu ms), and is refused after 500 ms when sensor 0 "
	       "keeps or retakes the supplies\n", waited);
}

int main(void)
{
	case_imgsensor();
	case_wait();
	return 0;
}

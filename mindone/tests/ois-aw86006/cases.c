/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#define IMG_APP_LEN 0x6ef4
#define IMG_MAX_APP_LEN (0xb000 - 0x2040)

static struct i2c_adapter adap4 = { .nr = 4 };
static struct i2c_client client = { .addr = 0x69, .adapter = &adap4 };
static u8 image[0xb200];
static u8 image2[0xb200];
static u8 record_ref[512];
static u32 rng_state;

static const struct {
	const char *pin;
	unsigned int ms;
} imx766_on[] = {
	{ "pnd", 0 }, { "rst", 1 }, { "vcama", 3 }, { "vcama1", 0 }, { "vcamaf", 3 },
	{ "vcamd", 4 }, { "vcamio", 1 }, { "mclk", 6 }, { "pnd", 0 }, { "rst", 5 },
};

static struct aw86006 *dev_aw(void)
{
	return i2c_get_clientdata(&client);
}

static void notify(unsigned long action, const char *pin)
{
	struct imgsensor_power_event event = { .sensor_idx = 0, .pin = pin };

	if (power_nb)
		power_nb->notifier_call(power_nb, action, &event);
}

static void power_up(void)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(imx766_on); i++) {
		if (!strcmp(imx766_on[i].pin, "vcamaf"))
			model_power_on();
		notify(IMGSENSOR_POWER_PIN_ON, imx766_on[i].pin);
		msleep(imx766_on[i].ms);
	}
	notify(IMGSENSOR_POWER_ON, NULL);
}

static void power_down(void)
{
	notify(IMGSENSOR_POWER_OFF, NULL);
	model_power_off();
	msleep(30);
}

static s64 pin_start_ms(size_t index)
{
	s64 at = 0;
	size_t i;

	for (i = 0; i < index; i++)
		at += imx766_on[i].ms;
	return at;
}

static s64 pin_offset_us(size_t index)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(imx766_on); i++)
		if (!strcmp(imx766_on[i].pin, "vcamaf"))
			return (pin_start_ms(index) - pin_start_ms(i)) * 1000;
	abort();
}

static const char *expected_pins(void)
{
	static char buf[512];
	size_t i;
	int n;

	n = snprintf(buf, sizeof(buf), "us after vcamaf:");
	for (i = 0; i < ARRAY_SIZE(imx766_on); i++)
		n += snprintf(buf + n, sizeof(buf) - n, " %s %lld", imx766_on[i].pin, (long long)pin_offset_us(i));
	snprintf(buf + n, sizeof(buf) - n, " on %lld\n", (long long)pin_offset_us(ARRAY_SIZE(imx766_on)));
	return buf;
}

static void default_params(void)
{
	assert(!set_param("identify", "0"));
	assert(!set_param("sync", "0"));
	assert(!set_param("update", "0"));
	assert(!set_param("runtime", "0"));
	assert(!set_param("run_ms", "200"));
	assert(!set_param("gyro_type", "-1"));
	assert(!set_param("gyro_spi", "-1"));
	assert(!set_param("wait_ms", "500"));
	assert(!set_param("bus_hz", "100000"));
}

static void bind(void)
{
	assert(harness_driver->probe(&client) == 0);
	assert(power_nb);
}

static void begin(void)
{
	model_reset();
	default_params();
	bind();
}

static void unbind(void)
{
	harness_driver->remove(&client);
	assert(!power_nb);
	devm_release();
	client.dev.driver_data = NULL;
}

static void end(void)
{
	unbind();
	assert(!fw_outstanding);
	assert(!bus_locked);
	default_params();
}

static u8 rng(void)
{
	rng_state = rng_state * 1103515245 + 12345;
	return rng_state >> 16;
}

static void fix_info(u8 *img)
{
	m_put32(img + 0x2008, m_sum32(img + 0x200c, 13));
}

static size_t make_image(u8 *img, u32 app_len, u32 version, u32 app_id, u32 seed)
{
	size_t size = 0x2000 + 64 + app_len;
	u8 *h = img + 0x2000;
	size_t i;

	rng_state = seed;
	for (i = 0; i < size; i++)
		img[i] = rng();
	memset(h, 0, 64);
	memcpy(h, "AWINIC\0\0", 8);
	m_put32(h + 0x10, app_len);
	m_put32(h + 0x14, version);
	m_put32(h + 0x18, app_id);
	m_put32(h + 0x1c, m_sum32(img, 0x2000 / 4));
	m_put32(h + 0x20, 0x02000011);
	m_put32(h + 0x24, 0x2000);
	m_put32(h + 0x2c, 1);
	m_put32(h + 0x0c, m_sum32(img + 0x2040, app_len / 4));
	fix_info(img);
	return size;
}

static void put_record(void)
{
	u32 i;

	memset(record_ref, 0xff, sizeof(record_ref));
	m_put32(record_ref, 0x100);
	m_put32(record_ref + 4, 123);
	m_put32(record_ref + 12, 0);
	m_put32(record_ref + 16, 0);
	for (i = 0; i < 121; i++)
		m_put32(record_ref + 20 + 4 * i, 0x3f800000 + i * 0x1111);
	m_put32(record_ref + 8, m_sum32(record_ref + 12, 123));
	memcpy(m.flash + 0xf000, record_ref, sizeof(record_ref));
}

static bool record_intact(void)
{
	return !memcmp(m.flash + 0xf000, record_ref, sizeof(record_ref)) &&
	       !memchr_inv(m.flash + 0xb000, 0xff, 0xf000 - 0xb000);
}

static const char *state_text(void)
{
	static char buf[PAGE_SIZE];
	ssize_t n;

	memset(buf, 0, sizeof(buf));
	n = dev_attr_state.show(&client.dev, &dev_attr_state, buf);
	assert(n > 0 && n < PAGE_SIZE && (size_t)n == strlen(buf));
	return buf;
}

static bool state_has(const char *needle)
{
	if (strstr(state_text(), needle))
		return true;
	fprintf(stderr, "state lacks \"%s\":\n%s", needle, state_text());
	return false;
}

static void read_bin(const struct bin_attribute *attr, u8 *out, size_t len)
{
	size_t off = 0;
	ssize_t n;

	while ((n = attr->read(NULL, &client.dev.kobj, attr, (char *)out + off, off, 37)) > 0)
		off += n;
	assert(n == 0 && off == len && off == attr->size);
}

static int packets_since(int from, u8 module, u8 event)
{
	int i, n = 0;

	for (i = from; i < m.npackets; i++)
		if (m.packets[i].module == module && m.packets[i].event == event)
			n++;
	return n;
}

static bool hold_bytes_in_window(void)
{
	int i;

	for (i = 0; i < m.nxfers; i++)
		if (m.xfers[i].wlen == 3 && m.xfers[i].first == 0xac &&
		    m.xfers[i].t_us - m.on / 1000 > M_WINDOW_US)
			return false;
	return true;
}

static bool no_pointer_writes(void)
{
	int i;

	for (i = 0; i < m.nxfers; i++)
		if (m.xfers[i].wlen == 2)
			return false;
	return true;
}

static void expect_app_writes(int from, const u16 (*want)[2], int n)
{
	int i;

	assert(m.napp_writes - from >= n);
	for (i = 0; i < n; i++) {
		if (m.app_writes[from + i].reg != want[i][0] || m.app_writes[from + i].val != want[i][1]) {
			fprintf(stderr, "write %d: 0x%04x<-0x%02x, want 0x%04x<-0x%02x\n", i,
				m.app_writes[from + i].reg, m.app_writes[from + i].val, want[i][0], want[i][1]);
			assert(m.app_writes[from + i].reg == want[i][0] && m.app_writes[from + i].val == want[i][1]);
		}
	}
}

static void case_defaults(void)
{
	const struct attribute_group *group;
	size_t size;
	int cycle;

	model_reset();
	bind();
	assert(aw_identify == 0 && !aw_sync && !atomic_read(&aw_update) && aw_runtime == 0);
	assert(aw_run_ms == 200 && aw_gyro_type == -1 && aw_gyro_spi == -1 && aw_bus_hz == 100000);
	assert(!strcmp(aw_sync_pin, "vcamaf") && !strcmp(aw_fw_name, "aw86006-mindone.prog"));
	assert(!strcmp(get_param("identify"), "0\n") && !strcmp(get_param("sync"), "N\n"));
	assert(!strcmp(get_param("update"), "N\n") && !strcmp(get_param("runtime"), "0\n"));
	assert(find_param("bus_hz")->perm == 0444 && find_param("sync_pin")->perm == 0444 &&
	       find_param("fw_name")->perm == 0444 && find_param("update")->perm == 0644);
	assert(aw_wait_ms == 500 && find_param("wait_ms")->perm == 0644);
	size = make_image(image, IMG_APP_LEN, 0x0003005a, 0x88, 1);
	memcpy(m.flash, image, size);
	put_record();
	fw_image = image;
	fw_size = size;
	for (cycle = 0; cycle < 3; cycle++) {
		power_up();
		pump_ms(500);
		power_down();
	}
	assert(m.power_ups == 3);
	assert(m.nxfers == 0);
	assert(slow.sets == 0 && fw_requests == 0);
	assert(state_has("power: off") && state_has("hold: off") && state_has("update: none") &&
	       state_has("runtime: idle") && state_has("program: not read"));
	group = harness_driver->driver.dev_groups[0];
	assert(!harness_driver->driver.dev_groups[1]);
	assert(group->attrs[0] == &dev_attr_state.attr && group->attrs[1] == &dev_attr_stop.attr &&
	       group->attrs[2] == &dev_attr_trace.attr && !group->attrs[3]);
	assert(dev_attr_state.attr.mode == 0444 && dev_attr_stop.attr.mode == 0200 && dev_attr_trace.attr.mode == 0444);
	assert(group->bin_attrs[0]->size == 64 && group->bin_attrs[1]->size == 512 &&
	       group->bin_attrs[2]->size == 0x10000 && !group->bin_attrs[3]);
	assert(bin_attr_flash.read(NULL, &client.dev.kobj, &bin_attr_flash, (char *)image, 0, 64) == -ENODATA);
	assert(!strcmp(harness_driver->driver.of_match_table[0].compatible, "mindone,aw86006-ois"));
	assert(!strcmp(harness_driver->id_table[0].name, "aw86006-ois"));
	end();
	assert(record_intact() && !memcmp(m.flash, image, size));
	printf("PASS defaults: every parameter defaults to off (identify 0, sync N, update N, runtime 0), probe and three "
	       "module power cycles make no bus transfer, set no slow_target and request no firmware\n");
}

static void case_params(void)
{
	static const char *const bad[][2] = {
		{ "identify", "16" }, { "identify", "-1" }, { "identify", "x" }, { "runtime", "3" },
		{ "run_ms", "9" }, { "run_ms", "10001" }, { "gyro_type", "-2" }, { "gyro_type", "256" },
		{ "gyro_spi", "0x100" }, { "bus_hz", "9999" }, { "bus_hz", "100001" }, { "bus_hz", "1" },
		{ "bus_hz", "-1" }, { "update", "x" }, { "sync", "maybe" }, { "sync_pin", "0123456789abcdef" },
		{ "wait_ms", "5001" }, { "wait_ms", "-1" }, { "wait_ms", "x" },
	};
	size_t i;

	model_reset();
	for (i = 0; i < ARRAY_SIZE(bad); i++) {
		char before[64];

		snprintf(before, sizeof(before), "%s", get_param(bad[i][0]));
		assert(set_param(bad[i][0], bad[i][1]) < 0);
		assert(!strcmp(before, get_param(bad[i][0])));
	}
	assert(!set_param("identify", "7") && aw_identify == 7 && !set_param("identify", "15") && aw_identify == 15);
	assert(!set_param("runtime", "2") && aw_runtime == 2);
	assert(!set_param("run_ms", "10") && !set_param("run_ms", "10000") && aw_run_ms == 10000);
	assert(!set_param("gyro_type", "0x0e") && aw_gyro_type == 14 && !strcmp(get_param("gyro_type"), "14\n"));
	assert(!set_param("gyro_spi", "255") && aw_gyro_spi == 255);
	assert(!set_param("update", "1\n") && !strcmp(get_param("update"), "Y\n"));
	assert(!set_param("update", "N") && !strcmp(get_param("update"), "N\n"));
	assert(!set_param("bus_hz", "50000") && aw_bus_hz == 50000 && !set_param("bus_hz", "100000"));
	assert(!set_param("bus_hz", "0") && aw_bus_hz == 0 && !strcmp(get_param("bus_hz"), "0\n"));
	assert(!set_param("wait_ms", "0") && aw_wait_ms == 0 && !set_param("wait_ms", "5000") && aw_wait_ms == 5000);
	default_params();
	printf("PASS params: %zu out-of-range or malformed values are refused and keep the old value, valid ones are "
	       "stored\n", ARRAY_SIZE(bad));
}

static void case_identify_blank(void)
{
	struct aw86006 *a;
	u8 buf[512];
	int reads;

	begin();
	put_record();
	assert(!set_param("identify", "7"));
	power_up();
	a = dev_aw();
	assert(slow.sets == 1 && slow.nr == 4 && slow.addr == 0x69 && slow.hz == 100000);
	assert(m.nxfers > 0 && slow.first_set <= (ktime_t)m.xfers[0].t_us * 1000);
	assert(only_read_packets());
	assert(m.early_reads == 0);
	assert(m.fast_rom == 0 && m.overreads == 0 && m.unknown_packets == 0);
	assert(count_packets(0x03, 0x01) == 0 && count_packets(0x03, 0x21) == 0 && m.chip_erases == 0);
	assert(count_packets(0x01, 0x01) == 1 && count_packets(0x01, 0x21) == 1 && count_packets(0x01, 0x31) == 1);
	assert(count_packets(0x03, 0x11) == 1 + 8);
	assert(m.napp_writes == 0 && lock_count > 0 && !bus_locked);
	assert(no_pointer_writes() && m.bad_packets == 0 && m.packets[0].module == 0x01 && m.packets[0].event == 0x01);
	assert(m.pings == 1 && m.ping_nacks == 0 && m.window_pings == 0 && m.data_nacks == 0);
	assert(m.sys_acks == 0 && m.sys_nacks == 6 && m.sys_data == 0 && m.other_addr == 0);
	assert(a->sys_ret == -ENXIO && a->sys_polls == 6 && a->polls == 0);
	assert(a->answer_us >= pin_offset_us(ARRAY_SIZE(imx766_on)));
	assert(m.first_data_us >= 50000 + 5 * 50000);
	assert(state_has("answer: 0x69 acked") && state_has("(0 polls not acked)") &&
	       state_has("0x61: not acked (-6, 6 polls)"));
	assert(a->mode == AW_MODE_ROM && a->connect_ret == 0);
	assert(a->query_ret[0] == 0 && a->query_ret[1] == 0);
	assert(a->header_state == AW_AREA_BLANK);
	assert(a->record_state == AW_AREA_VALID && a->record_words == 123);
	read_bin(&bin_attr_record, buf, 512);
	assert(!memcmp(buf, record_ref, 512));
	read_bin(&bin_attr_header, buf, 64);
	assert(!memchr_inv(buf, 0xff, 64));
	assert(state_has("mode: rom boot loader") && state_has("connect: 0 53 01 48 01 02 05 00 01 01 00 01 00 00 00") &&
	       state_has("program: blank") && state_has("record: valid (123 words") &&
	       state_has("rom version: 0 0x00010002") && state_has("rom id: 0 0x00008600"));
	power_down();
	assert(state_has("power: off") && state_has("record: valid"));
	assert(!set_param("identify", "1"));
	reads = count_packets(0x03, 0x11);
	power_up();
	assert(count_packets(0x03, 0x11) == reads + 1 && a->record_state == AW_AREA_UNREAD);
	assert(a->query_ret[0] == 1 && only_read_packets());
	power_down();
	end();
	assert(record_intact() && !memchr_inv(m.flash, 0xff, 0xb000));
	printf("PASS identify-blank: at 4:0x69:100000 the identification sends only connect, version/ID and flash-read "
	       "requests (no write, erase or chip erase), reads the blank header and the valid 123-word record, and "
	       "never reads early or past a reply\n");
}

static void case_identify_sync(void)
{
	struct aw86006 *a;
	u8 buf[64];
	size_t size;
	int packets;

	begin();
	size = make_image(image, IMG_APP_LEN, 0x0003005a, 0x88, 1);
	memcpy(m.flash, image, size);
	put_record();
	assert(!set_param("identify", "3") && !set_param("sync", "1"));
	power_up();
	a = dev_aw();
	assert(m.hold_writes >= 5 && m.hold_bad_bytes == 0);
	assert(m.hold_first_us >= 2000 && m.hold_first_us <= 2500);
	assert(hold_bytes_in_window());
	assert(a->hold == AW_HOLD_ACKED && a->hold_first_us >= 2000 && a->hold_first_us <= 6000);
	assert(m.state == M_ROM && a->mode == AW_MODE_ROM);
	assert(no_pointer_writes());
	assert(only_read_packets() && m.napp_writes == 0);
	assert(a->header_state == AW_AREA_VALID && a->header_info.app_version == 0x0003005a &&
	       a->header_info.move_version == 0x02000011 && a->header_info.app_length == IMG_APP_LEN);
	read_bin(&bin_attr_header, buf, 64);
	assert(!memcmp(buf, image + 0x2000, 64));
	assert(state_has("hold: acked (first ack 2000 us") &&
	       state_has("program: valid v0.3.0.90 id 0x88 loader 0x02000011 length 0x6ef4") &&
	       state_has("record: valid"));
	power_down();
	assert(!set_param("sync", "0"));
	packets = m.npackets;
	power_up();
	assert(m.state == M_APP && a->mode == AW_MODE_APP && a->app_version == 0x0003005a && a->app_id == 0x88);
	assert(m.npackets == packets && m.app_rom_frames == 0 && m.napp_writes == 0);
	assert(state_has("mode: program") && state_has("chip: 0x88 v0.3.0.90") && state_has("hold: off"));
	power_down();
	end();
	assert(record_intact() && !memcmp(m.flash, image, size));
	printf("PASS identify-sync: 0xAC holds start 2 ms after vcamaf and stay inside 2-6 ms, the held chip answers in "
	       "its ROM loader with the program header (v0.3.0.90, loader 0x02000011); without sync the running program "
	       "is identified by registers 0x0000/0x0010 and receives no ROM frame\n");
}

static void case_hold_late(void)
{
	struct aw86006 *a;
	size_t size;

	begin();
	size = make_image(image, IMG_APP_LEN, 0x0003005a, 0x88, 1);
	memcpy(m.flash, image, size);
	m.por_us = 6500;
	assert(!set_param("identify", "1") && !set_param("sync", "1"));
	power_up();
	a = dev_aw();
	assert(hold_bytes_in_window());
	assert(a->hold == AW_HOLD_MISSED && a->hold_acks == 0 && a->hold_writes > 0);
	assert(!m.held && m.state == M_APP && a->mode == AW_MODE_APP);
	assert(m.npackets == 0 && m.app_rom_frames == 0 && m.napp_writes == 0);
	assert(state_has("hold: missed"));
	power_down();
	end();
	printf("PASS hold-late: a chip that does not answer before 6 ms gets no 0xAC after the window, boots its program "
	       "and receives no ROM frame\n");
}

static const char *trace_text(void)
{
	static char buf[PAGE_SIZE];
	ssize_t n;

	memset(buf, 0, sizeof(buf));
	n = dev_attr_trace.show(&client.dev, &dev_attr_trace, buf);
	assert(n > 0 && n < PAGE_SIZE && (size_t)n == strlen(buf));
	return buf;
}

static void case_identify_late(void)
{
	struct aw86006 *a;
	const char *trace;
	char text[128];
	ktime_t t;
	int packets;

	begin();
	put_record();
	m.quiet_us = 300000;
	assert(!set_param("identify", "1"));
	power_up();
	a = dev_aw();
	assert(a->answer_us >= 300000 && a->answer_us <= 300000 + 10500);
	assert(a->polls == (unsigned int)m.ping_nacks && a->polls >= 30 && a->polls <= 50);
	assert(m.data_nacks == 0 && m.first_data_us >= a->answer_us && m.window_pings == 0);
	assert(a->mode == AW_MODE_ROM && a->connect_ret == 0 && a->header_state == AW_AREA_BLANK);
	assert(only_read_packets() && m.bad_packets == 0);
	snprintf(text, sizeof(text), "answer: 0x69 acked %lld us after vcamaf (%u polls not acked)",
		 (long long)a->answer_us, a->polls);
	assert(state_has(text) && state_has("mode: rom boot loader"));
	trace = trace_text();
	assert(strstr(trace, expected_pins()));
	snprintf(text, sizeof(text), "\n%lld 0x69 w0 r0 -6\n", (long long)pin_offset_us(ARRAY_SIZE(imx766_on)));
	assert(strstr(trace, text) && strstr(trace, " 0x61 w0 r0 -6\n") && strstr(trace, "+"));
	power_down();
	assert(!set_param("wait_ms", "100"));
	packets = m.npackets;
	t = now_ns;
	power_up();
	assert(a->mode == AW_MODE_ABSENT && a->answer_us < 0 && a->sys_ret == AW_UNTRIED);
	assert(m.npackets == packets && m.first_data_us < 0 && m.data_nacks == 0 && m.sys_nacks == 6);
	assert(now_ns - t <= (pin_offset_us(ARRAY_SIZE(imx766_on)) - pin_offset_us(0) + 100000 + 11000) * 1000);
	assert(state_has("mode: absent") && state_has("answer: 0x69 not acked in") && state_has("off before power-up: 30 ms"));
	assert(!strstr(state_text(), "0x61:") && !strstr(trace_text(), "0x61") && strstr(trace_text(), text));
	power_down();
	assert(!set_param("wait_ms", "0"));
	power_up();
	assert(a->mode == AW_MODE_ABSENT && a->polls == 1 && m.first_data_us < 0);
	power_down();
	end();
	assert(record_intact() && !memchr_inv(m.flash, 0xff, 0xb000));
	printf("PASS identify-late: a chip that stays silent for 300 ms after vcamaf gets address-only polls (2 ms, then "
	       "10 ms apart) and no data until it acks, then a clean connect; with wait_ms 100 or 0 it is reported "
	       "absent and receives no data byte at all; the trace lists the pin timeline and every transfer\n");
}

static void case_identify_gate(void)
{
	struct aw86006 *a;
	size_t size;

	begin();
	size = make_image(image, IMG_APP_LEN, 0x0003005a, 0x88, 1);
	memcpy(m.flash, image, size);
	put_record();
	assert(!set_param("identify", "7"));
	m.sys_up_us = 120000;
	power_up();
	a = dev_aw();
	assert(a->sys_ret == 0 && a->sys_polls >= 2 && a->sys_polls <= 4 && m.sys_acks == 1);
	assert(m.state == M_APP && a->mode == AW_MODE_APP && a->app_id == 0x88 && a->app_version == 0x0003005a);
	assert(m.npackets == 0 && m.app_rom_frames == 0 && m.napp_writes == 0 && m.sys_data == 0);
	assert(state_has("0x61: acked (0, ") && state_has("mode: program"));
	power_down();
	m.sys_up_us = 0;
	m.chip_id = 0x5a;
	size = make_image(image2, IMG_APP_LEN, 0x0003006d, 0x88, 2);
	fw_image = image2;
	fw_size = size;
	assert(!set_param("update", "1") && !set_param("runtime", "1"));
	power_up();
	assert(m.npackets == 0 && m.app_rom_frames == 0 && m.napp_writes == 0 && m.sys_data == 0);
	assert(a->sys_ret == 0 && a->mode == AW_MODE_UNKNOWN && a->update == AW_UPDATE_NEEDS_ROM);
	assert(a->run == AW_RUN_NO_PROGRAM && a->header_state == AW_AREA_UNREAD);
	assert(!memcmp(m.flash, image, 0x2000 + 64 + IMG_APP_LEN));
	power_down();
	end();
	assert(record_intact());
	printf("PASS identify-gate: when 0x61 acknowledges (a program runs, here after 120 ms) only register reads go to "
	       "0x69; a program with a foreign chip ID gets no ROM frame, no register write and no update\n");
}

static void case_bus_dt(void)
{
	struct aw86006 *a;

	model_reset();
	default_params();
	assert(!set_param("bus_hz", "0"));
	bind();
	put_record();
	assert(!set_param("identify", "1"));
	power_up();
	a = dev_aw();
	assert(slow.sets == 0 && m.fast_rom > 0 && a->mode == AW_MODE_SILENT && a->connect_ret == -EPROTO);
	assert(count_packets(0x01, 0x01) == 5 && only_read_packets());
	assert(state_has("slow_target: off") && state_has("mode: no answer"));
	power_down();
	end();
	assert(record_intact() && !memchr_inv(m.flash, 0xff, 0xb000));
	printf("PASS bus-dt: bus_hz 0 leaves the bus at its DT speed (no slow_target), and a ROM loader read faster than "
	       "100 kHz is reported as no answer after five connect frames\n");
}

static void case_identify_dump(void)
{
	static u8 dump[0x10000];
	struct aw86006 *a;
	size_t size;
	int reads;

	begin();
	size = make_image(image, IMG_APP_LEN, 0x0003005a, 0x88, 11);
	memcpy(m.flash, image, size);
	memset(m.flash + 0x2000, 0xff, 64);
	put_record();
	assert(!set_param("identify", "8"));
	reads = count_packets(0x03, 0x11);
	power_up();
	a = dev_aw();
	assert(a->mode == AW_MODE_ROM && a->flash_complete && a->flash_bad == 0 && a->flash_ret == 0);
	assert(count_packets(0x03, 0x11) == reads + 1024 && only_read_packets() && m.napp_writes == 0);
	assert(m.fast_rom == 0 && m.early_reads == 0 && m.overreads == 0 && m.protected_hits == 0);
	read_bin(&bin_attr_flash, dump, sizeof(dump));
	assert(!memcmp(dump, m.flash, sizeof(dump)));
	assert(state_has("flash: 65536 bytes read, 0 chunks failed\n"));
	power_down();
	m.rom_refuses_record = true;
	power_up();
	assert(a->flash_complete && a->flash_bad == 64 && a->flash_first_bad == 0x0100f000 &&
	       a->flash_ret == -EREMOTEIO);
	read_bin(&bin_attr_flash, dump, sizeof(dump));
	assert(!memcmp(dump, m.flash, 0xf000) && !memchr_inv(dump + 0xf000, 0, 0x1000));
	assert(state_has("flash: 65536 bytes read, 64 chunks failed (first at 0x0100f000, -121)"));
	power_down();
	end();
	assert(record_intact() && !memcmp(m.flash, image, 0x2000));
	printf("PASS identify-dump: identify 8 reads the whole 64 KiB flash in 1024 read requests into the flash file, "
	       "byte for byte; chunks the loader refuses are zero-filled, counted and reported with the first address\n");
}

static void check_update_written(const u8 *img, size_t size, int packets_before)
{
	int i, erases = 0, writes = 0, last_write = -1, verify_reads = 0;

	assert(!memcmp(m.flash, img, size));
	for (i = packets_before; i < m.npackets; i++) {
		struct m_packet *p = &m.packets[i];

		if (p->module == 0x03 && p->event == 0x11 && writes && p->addr != 0x01002000 &&
		    p->addr < 0x01000000 + size)
			verify_reads++;

		if (p->module != 0x03 || (p->event != 0x01 && p->event != 0x21))
			continue;
		if (p->event == 0x21) {
			assert(writes == 0);
			assert(p->addr + p->count * 512 <= 0x0100b000);
			if (!erases)
				assert(p->addr == 0x01002000 && p->count == DIV_ROUND_UP(size - 0x2000, 512));
			else
				assert(p->addr == 0x01000000 && p->count == 16);
			erases++;
			continue;
		}
		assert(p->addr + p->count <= 0x0100b000 && p->count <= 64 && (p->addr - 0x01000000) % 64 == 0);
		assert(last_write < 0 || m.packets[last_write].addr != 0x01002000);
		last_write = i;
		writes++;
	}
	assert(erases == 2);
	assert(writes == (int)DIV_ROUND_UP(size, 64));
	assert(verify_reads == (int)DIV_ROUND_UP(size, 64) - 1);
	assert(m.packets[last_write].addr == 0x01002000 && m.packets[last_write].count == 64);
	assert(m.protected_hits == 0 && m.unerased_writes == 0 && m.chip_erases == 0);
	assert(m.fast_rom == 0 && m.overreads == 0 && m.early_reads == 0);
}

static void case_update_blank(void)
{
	struct aw86006 *a;
	size_t size;
	int packets;

	begin();
	put_record();
	size = make_image(image, IMG_APP_LEN, 0x0003005a, 0x88, 2);
	fw_image = image;
	fw_size = size;
	assert(!set_param("update", "1"));
	power_up();
	a = dev_aw();
	assert(!strcmp(get_param("update"), "N\n"));
	assert(fw_requests == 1 && !fw_outstanding);
	assert(a->update == AW_UPDATE_DONE && a->update_ret == 0);
	check_update_written(image, size, 0);
	assert(!memchr_inv(m.flash + size, 0xff, 0xb000 - size));
	assert(record_intact() && a->record_after == AW_AREA_VALID);
	assert(a->header_state == AW_AREA_VALID && a->header_info.app_version == 0x0003005a);
	assert(state_has("update: written and verified, power-cycle the module (0, image 0x0003005a") &&
	       state_has("record after update: unchanged"));
	power_down();
	assert(!set_param("identify", "1"));
	power_up();
	assert(m.state == M_APP && a->mode == AW_MODE_APP && a->app_version == 0x0003005a);
	power_down();
	packets = m.npackets;
	assert(!set_param("update", "1"));
	power_up();
	assert(a->update == AW_UPDATE_CURRENT && m.npackets == packets && m.app_rom_frames == 0);
	power_down();
	end();
	assert(record_intact());
	printf("PASS update-blank: one armed attempt erases 0x01002000 (%zu blocks) and 0x01000000 (16 blocks), writes "
	       "%zu chunks with the header chunk last after a full readback, never touches 0x0100b000+, keeps the "
	       "record, disarms itself, and the program then boots and is reported current\n",
	       (size_t)DIV_ROUND_UP(size - 0x2000, 512), (size_t)DIV_ROUND_UP(size, 64));
}

static void case_update_max(void)
{
	struct aw86006 *a;
	size_t size;
	int packets;

	begin();
	put_record();
	size = make_image(image, IMG_MAX_APP_LEN, 0x0003006d, 0x88, 3);
	assert(size == 0xb000);
	fw_image = image;
	fw_size = size;
	assert(!set_param("update", "1"));
	power_up();
	a = dev_aw();
	assert(a->update == AW_UPDATE_DONE);
	check_update_written(image, size, 0);
	assert(record_intact());
	power_down();
	memset(m.flash, 0xff, 0xb000);
	size = make_image(image2, IMG_MAX_APP_LEN + 64, 0x0003006e, 0x88, 4);
	fw_image = image2;
	fw_size = size;
	packets = m.npackets;
	assert(!set_param("update", "1"));
	power_up();
	assert(a->update == AW_UPDATE_BAD_IMAGE && a->update_ret == -EFBIG);
	assert(packets_since(packets, 0x03, 0x01) == 0 && packets_since(packets, 0x03, 0x21) == 0);
	power_down();
	end();
	assert(record_intact() && !memchr_inv(m.flash, 0xff, 0xb000));
	printf("PASS update-max: a 0xb000-byte image erases exactly up to 0x0100b000 and is written; a 0xb040-byte image "
	       "is refused with -EFBIG before any erase\n");
}

static void case_update_refused(void)
{
	enum { BAD_MAGIC, BAD_ID, BAD_MOVE, BAD_APP, BAD_INFO, SHORT, MOVE_LEN, ODD_LEN, TINY, NO_FILE, KINDS };
	static const int want[KINDS] = {
		-EBADMSG, -EINVAL, -EBADMSG, -EBADMSG, -EBADMSG, -EINVAL, -EINVAL, -EINVAL, -EINVAL, -ENOENT,
	};
	struct aw86006 *a;
	size_t size;
	int kind;

	begin();
	put_record();
	a = dev_aw();
	for (kind = 0; kind < KINDS; kind++) {
		int packets = m.npackets;

		size = make_image(image, IMG_APP_LEN, 0x0003005a, kind == BAD_ID ? 0x89 : 0x88, 5);
		fw_image = image;
		fw_size = size;
		switch (kind) {
		case BAD_MAGIC:
			image[0x2000] = 'B';
			fix_info(image);
			break;
		case BAD_MOVE:
			image[5] ^= 1;
			break;
		case BAD_APP:
			image[0x3000] ^= 1;
			break;
		case BAD_INFO:
			image[0x2014] ^= 1;
			break;
		case SHORT:
			fw_size = size - 4;
			break;
		case MOVE_LEN:
			m_put32(image + 0x2024, 0x1000);
			fix_info(image);
			break;
		case ODD_LEN:
			m_put32(image + 0x2010, IMG_APP_LEN - 2);
			fix_info(image);
			fw_size = size - 2;
			break;
		case TINY:
			fw_size = 0x2000;
			break;
		case NO_FILE:
			fw_image = NULL;
			break;
		}
		assert(!set_param("update", "1"));
		power_up();
		if (a->update_ret != want[kind])
			fprintf(stderr, "kind %d: update %d ret %d\n", kind, a->update, a->update_ret);
		assert(a->update == (kind == NO_FILE ? AW_UPDATE_NO_FILE : AW_UPDATE_BAD_IMAGE));
		assert(a->update_ret == want[kind]);
		assert(packets_since(packets, 0x03, 0x01) == 0 && packets_since(packets, 0x03, 0x21) == 0);
		assert(!memchr_inv(m.flash, 0xff, 0xb000) && record_intact() && !fw_outstanding);
		power_down();
	}
	end();
	printf("PASS update-refused: wrong identify string, app_id 0x89, loader/app/info checksum errors, a size that "
	       "does not match the header, a loader length other than 0x2000, an app length not a multiple of 4, a "
	       "truncated file and a missing file are all refused before any erase or write\n");
}

static void case_update_verify_fail(void)
{
	struct aw86006 *a;
	char text[128];
	size_t size;
	int i;

	begin();
	put_record();
	size = make_image(image, IMG_APP_LEN, 0x0003005a, 0x88, 6);
	for (i = 0x3001; image[i] == 0xff; i++)
		;
	m.stuck = i;
	fw_image = image;
	fw_size = size;
	assert(!set_param("update", "1"));
	power_up();
	a = dev_aw();
	assert(a->update == AW_UPDATE_FAILED && a->update_ret == -EILSEQ);
	assert(a->update_addr == 0x01000000 + (i & ~63));
	assert(!memchr_inv(m.flash + 0x2000, 0xff, 64));
	for (i = 0; i < m.npackets; i++)
		assert(!(m.packets[i].module == 0x03 && m.packets[i].event == 0x01 && m.packets[i].addr == 0x01002000));
	assert(record_intact() && m.protected_hits == 0);
	snprintf(text, sizeof(text), "update: failed (%d, image 0x0003005a, at 0x%08x)", -EILSEQ, a->update_addr);
	assert(state_has(text));
	power_down();
	assert(!set_param("identify", "1"));
	power_up();
	assert(m.state == M_ROM && a->mode == AW_MODE_ROM && a->header_state == AW_AREA_BLANK);
	power_down();
	end();
	assert(record_intact());
	printf("PASS update-verify-fail: a byte that does not program fails the readback with -EILSEQ, the header chunk "
	       "is never written, the chip stays in its ROM loader with a blank header and the record is untouched\n");
}

static void case_update_write_fail(void)
{
	struct aw86006 *a;
	size_t size;

	begin();
	put_record();
	size = make_image(image, IMG_APP_LEN, 0x0003005a, 0x88, 7);
	fw_image = image;
	fw_size = size;
	m.drop_writes_from = 50;
	assert(!set_param("update", "1"));
	power_up();
	a = dev_aw();
	assert(a->update == AW_UPDATE_FAILED && a->update_ret == -EPROTO);
	assert(m.write_packets == 50 + 1);
	assert(!memchr_inv(m.flash + 0x2000, 0xff, 64) && record_intact() && m.protected_hits == 0);
	power_down();
	m.drop_writes_from = 0;
	assert(!set_param("update", "1"));
	power_up();
	assert(a->update == AW_UPDATE_DONE);
	assert(!memcmp(m.flash, image, size) && record_intact());
	power_down();
	end();
	printf("PASS update-write-fail: an unacknowledged write fails the attempt after two tries with the header still "
	       "erased; the next armed power-up writes the image completely\n");
}

static void case_update_replace(void)
{
	struct aw86006 *a;
	size_t size_a, size_b;
	int packets;

	begin();
	put_record();
	size_a = make_image(image, IMG_APP_LEN, 0x0003005a, 0x88, 8);
	size_b = make_image(image2, IMG_APP_LEN - 0x400, 0x0003006d, 0x88, 9);
	memcpy(m.flash, image, size_a);
	fw_image = image2;
	fw_size = size_b;
	assert(!set_param("update", "1"));
	power_up();
	a = dev_aw();
	assert(a->mode == AW_MODE_APP && a->update == AW_UPDATE_NEEDS_ROM);
	assert(m.npackets == 0 && m.app_rom_frames == 0 && !memcmp(m.flash, image, size_a));
	power_down();
	assert(!set_param("update", "1") && !set_param("sync", "1"));
	power_up();
	assert(a->hold == AW_HOLD_ACKED && a->update == AW_UPDATE_DONE);
	check_update_written(image2, size_b, 0);
	assert(record_intact() && a->record_after == AW_AREA_VALID);
	power_down();
	packets = m.npackets;
	assert(!set_param("update", "1"));
	power_up();
	assert(a->update == AW_UPDATE_CURRENT);
	assert(packets_since(packets, 0x03, 0x01) == 0 && packets_since(packets, 0x03, 0x21) == 0);
	power_down();
	assert(!set_param("sync", "0") && !set_param("identify", "1"));
	power_up();
	assert(m.state == M_APP && a->app_version == 0x0003006d);
	power_down();
	end();
	printf("PASS update-replace: with a program running the update is refused without a ROM frame; with sync the "
	       "held chip gets the new image, a repeat is reported current, and the new program "
	       "boots\n");
}

static void case_guards(void)
{
	struct aw86006 *a;
	u8 data[65];
	int erases, writes;

	begin();
	put_record();
	assert(!set_param("identify", "1"));
	power_up();
	a = dev_aw();
	assert(a->mode == AW_MODE_ROM);
	memset(data, 0x5a, sizeof(data));
	erases = count_packets(0x03, 0x21);
	writes = count_packets(0x03, 0x01);
	assert(aw86006_rom_erase(a, 0x0100ae00, 1) == 0);
	assert(aw86006_rom_erase(a, 0x0100ae00, 2) == -EPERM);
	assert(aw86006_rom_erase(a, 0x0100b000, 1) == -EPERM);
	assert(aw86006_rom_erase(a, 0x0100f000, 1) == -EPERM);
	assert(aw86006_rom_erase(a, 0x00fffe00, 1) == -EPERM);
	assert(aw86006_rom_erase(a, 0x01000100, 1) == -EPERM);
	assert(aw86006_rom_erase(a, 0x01000000, 0) == -EPERM);
	assert(aw86006_rom_erase(a, 0x01000000, 256) == -EPERM);
	assert(count_packets(0x03, 0x21) == erases + 1);
	assert(aw86006_rom_write(a, 0x0100afc0, data, 64) == 0);
	assert(aw86006_rom_write(a, 0x0100afc1, data, 64) == -EPERM);
	assert(aw86006_rom_write(a, 0x0100f000, data, 4) == -EPERM);
	assert(aw86006_rom_write(a, 0x00ffffc0, data, 64) == -EPERM);
	assert(aw86006_rom_write(a, 0x01000000, data, 0) == -EPERM);
	assert(aw86006_rom_write(a, 0x01000000, data, 65) == -EPERM);
	assert(count_packets(0x03, 0x01) == writes + 1);
	assert(aw86006_flash_writable(0x0100afff, 1) && !aw86006_flash_writable(0x0100b000, 1));
	assert(!aw86006_flash_writable(0x00ffffff, 1) && !aw86006_flash_writable(0x01000000, 0));
	assert(!aw86006_flash_writable(0xffffffff, 2) && !aw86006_flash_writable(0x01000000, 0xb001));
	assert(m.protected_hits == 0 && record_intact());
	power_down();
	end();
	printf("PASS guards: erase and write refuse every range that is unaligned, empty, above 64 bytes or 255 blocks, "
	       "below 0x01000000 or reaching 0x0100b000, with no frame sent\n");
}

static void case_runtime_centre(void)
{
	static const u16 start[][2] = {
		{ 0x0007, 0x0e }, { 0xff1f, 0xae }, { 0xff11, 0x02 }, { 0x0001, 0x00 },
		{ 0xf31f, 0xa6 }, { 0xf300, 0x04 }, { 0x0001, 0x01 },
	};
	static const u16 stop[][2] = { { 0x0001, 0x00 }, { 0xf31f, 0xa6 }, { 0xf300, 0x00 } };
	struct aw86006 *a;
	unsigned int samples;
	size_t size;
	s64 on_ms;
	int writes;

	begin();
	size = make_image(image, IMG_APP_LEN, 0x0003005a, 0x88, 1);
	memcpy(m.flash, image, size);
	assert(!set_param("runtime", "1") && !set_param("run_ms", "200") && !set_param("gyro_type", "14"));
	power_up();
	a = dev_aw();
	assert(a->mode == AW_MODE_APP && a->run == AW_RUN_ACTIVE && a->running);
	assert(m.napp_writes == (int)ARRAY_SIZE(start));
	expect_app_writes(0, start, ARRAY_SIZE(start));
	assert(m.ois_on && m.regs[0xf300] == 0x04);
	pump_ms(400);
	assert(!a->running && a->run == AW_RUN_DONE && !m.ois_on);
	on_ms = (m.ois_off_at - m.ois_on_at) / 1000000;
	assert(on_ms >= 200 && on_ms <= 230);
	assert(m.napp_writes == (int)ARRAY_SIZE(start) + (int)ARRAY_SIZE(stop));
	expect_app_writes(ARRAY_SIZE(start), stop, ARRAY_SIZE(stop));
	samples = a->samples;
	assert(m.regs[0xf300] == 0 && samples >= 10 && samples <= 13);
	assert(m.npackets == 0 && m.app_rom_frames == 0);
	assert(state_has("runtime: done (mode 1, 0,") && state_has("first 00 00 80 3c 00 00 00 bd"));
	writes = m.napp_writes;
	pump_ms(1000);
	assert(m.napp_writes == writes);
	power_down();
	end();
	printf("PASS runtime-centre: gyro 0x0007, normal mode, centring (0xF31F<-0xA6, 0xF300<-0x04, 0x0001<-1) for %lld "
	       "ms with %u hall reads of 0xF44C, then 0x0001<-0 and 0xF300<-0 at the run_ms limit\n",
	       (long long)on_ms, samples);
}

static void case_runtime_stop(void)
{
	struct aw86006 *a;
	size_t size;
	ktime_t t;
	int xfers;

	begin();
	size = make_image(image, IMG_APP_LEN, 0x0003005a, 0x88, 1);
	memcpy(m.flash, image, size);
	assert(!set_param("runtime", "2") && !set_param("run_ms", "10000"));
	power_up();
	a = dev_aw();
	assert(m.ois_on && a->running && a->run_mode == 2);
	pump_ms(50);
	assert(dev_attr_stop.store(&client.dev, &dev_attr_stop, "x", 1) == -EINVAL);
	assert(dev_attr_stop.store(&client.dev, &dev_attr_stop, "0", 1) == 1 && m.ois_on);
	t = now_ns;
	assert(dev_attr_stop.store(&client.dev, &dev_attr_stop, "1\n", 2) == 2);
	assert(!m.ois_on && m.ois_off_at - t <= 2000000);
	assert(!a->running && a->run == AW_RUN_STOPPED);
	xfers = m.nxfers;
	pump_ms(1000);
	assert(m.nxfers == xfers);
	assert(state_has("runtime: stopped"));
	power_down();
	end();
	printf("PASS runtime-stop: writing 1 to stop turns OIS off (0x0001<-0) within 2 ms of the write and no transfer "
	       "follows\n");
}

static void case_runtime_power_off(void)
{
	struct aw86006 *a;
	size_t size;
	int xfers;

	begin();
	size = make_image(image, IMG_APP_LEN, 0x0003005a, 0x88, 1);
	memcpy(m.flash, image, size);
	assert(!set_param("runtime", "1") && !set_param("run_ms", "10000"));
	power_up();
	a = dev_aw();
	pump_ms(100);
	assert(m.ois_on);
	power_down();
	assert(!m.ois_on_at_power_off);
	assert(a->run == AW_RUN_POWER_OFF && !a->running);
	xfers = m.nxfers;
	pump_ms(1000);
	assert(m.nxfers == xfers);
	end();
	printf("PASS runtime-power-off: the imgsensor power-off event turns OIS off before the module rail drops\n");
}

static void case_runtime_refused(void)
{
	struct aw86006 *a;
	size_t size;
	int i;

	begin();
	assert(!set_param("runtime", "1"));
	power_up();
	a = dev_aw();
	assert(a->mode == AW_MODE_UNKNOWN && a->run == AW_RUN_NO_PROGRAM);
	assert(m.napp_writes == 0 && m.npackets == 0);
	assert(state_has("runtime: no program running"));
	power_down();
	size = make_image(image, IMG_APP_LEN, 0x0003005a, 0x88, 1);
	memcpy(m.flash, image, size);
	m.refuse_sys_unlock = true;
	power_up();
	assert(a->run == AW_RUN_FAILED && a->run_ret == -EIO && !m.ois_on && !a->running);
	for (i = m.napp_writes - 1; i >= 0 && m.app_writes[i].reg != 0x0001; i--)
		;
	assert(i >= 0 && m.app_writes[i].val == 0 && m.regs[0xf300] == 0);
	power_down();
	end();
	printf("PASS runtime-refused: without a running program no register is written; a failed start leaves OIS "
	       "off\n");
}

static void case_rom_errors(void)
{
	struct aw86006 *a;
	size_t size;
	int reads;

	begin();
	size = make_image(image, IMG_APP_LEN, 0x0003005a, 0x88, 1);
	memcpy(m.flash, image, size);
	put_record();
	assert(!set_param("identify", "3") && !set_param("sync", "1"));
	m.corrupt_addr = 0x01002000;
	m.corrupt_times = 1;
	reads = count_packets(0x03, 0x11);
	power_up();
	a = dev_aw();
	assert(a->header_state == AW_AREA_VALID && count_packets(0x03, 0x11) == reads + 2 + 8);
	power_down();
	m.corrupt_times = 2;
	power_up();
	assert(a->header_state == AW_AREA_FAILED && a->header_ret == -EBADMSG);
	assert(a->record_state == AW_AREA_VALID);
	power_down();
	m.rom_refuses_record = true;
	power_up();
	assert(a->header_state == AW_AREA_VALID && a->record_state == AW_AREA_FAILED && a->record_ret == -EREMOTEIO);
	power_down();
	m.rom_silent = true;
	reads = count_packets(0x03, 0x11);
	power_up();
	assert(a->mode == AW_MODE_SILENT && a->header_state == AW_AREA_UNREAD);
	assert(count_packets(0x03, 0x11) == reads && m.overreads == 0);
	assert(state_has("mode: no answer"));
	power_down();
	end();
	begin();
	put_record();
	a = dev_aw();
	assert(!set_param("identify", "3") && !set_param("sync", "1"));
	slow.fail = -EBUSY;
	power_up();
	assert(a->hold == AW_HOLD_FAILED && m.nxfers == 0 && a->mode == AW_MODE_UNKNOWN);
	assert(state_has("slow_target: -16"));
	power_down();
	end();
	assert(record_intact());
	printf("PASS rom-errors: a corrupted reply is retried once, a second corruption marks the header read failed, a "
	       "refused record read is reported, a silent loader gets connect frames only, and a refused slow_target "
	       "stops all traffic\n");
}

static void case_remove(void)
{
	size_t size;

	begin();
	size = make_image(image, IMG_APP_LEN, 0x0003005a, 0x88, 1);
	memcpy(m.flash, image, size);
	assert(!set_param("runtime", "2") && !set_param("run_ms", "10000"));
	power_up();
	assert(m.ois_on && slow.hz == 100000);
	pump_ms(30);
	unbind();
	assert(!m.ois_on && slow.hz == 0);
	notify(IMGSENSOR_POWER_OFF, NULL);
	model_power_off();
	assert(!m.ois_on_at_power_off);
	default_params();
	printf("PASS remove: unbinding with OIS on turns it off, clears the slow_target and unregisters the power "
	       "notifier\n");
}

int main(int argc, char **argv)
{
	static const struct {
		const char *name;
		void (*fn)(void);
	} cases[] = {
		{ "defaults", case_defaults }, { "params", case_params }, { "identify-blank", case_identify_blank },
		{ "identify-sync", case_identify_sync }, { "hold-late", case_hold_late },
		{ "identify-late", case_identify_late }, { "identify-gate", case_identify_gate },
		{ "bus-dt", case_bus_dt }, { "identify-dump", case_identify_dump },
		{ "update-blank", case_update_blank }, { "update-max", case_update_max },
		{ "update-refused", case_update_refused }, { "update-verify-fail", case_update_verify_fail },
		{ "update-write-fail", case_update_write_fail }, { "update-replace", case_update_replace },
		{ "guards", case_guards }, { "runtime-centre", case_runtime_centre },
		{ "runtime-stop", case_runtime_stop }, { "runtime-power-off", case_runtime_power_off },
		{ "runtime-refused", case_runtime_refused }, { "rom-errors", case_rom_errors },
		{ "remove", case_remove },
	};
	size_t i;
	int ran = 0;

	for (i = 0; i < ARRAY_SIZE(cases); i++)
		if (argc < 2 || !strcmp(argv[1], cases[i].name)) {
			cases[i].fn();
			ran++;
		}
	if (!ran) {
		fprintf(stderr, "unknown case %s\n", argc > 1 ? argv[1] : "");
		return 2;
	}
	return 0;
}

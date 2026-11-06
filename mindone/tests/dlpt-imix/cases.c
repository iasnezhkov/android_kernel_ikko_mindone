// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project

static void reset(void)
{
	memset(&dlpt, 0, sizeof(dlpt));
	dlpt.chan_imix_r = &channel;
	jiffies = 100;
	reads = notices = computations = callbacks = wakes = skips = 0;
	read_ret = 0;
	read_value = 117;
	ui_soc = 50;
	gauge_present = 1;
	charging = 0;
	notice_seen = false;
}

static void tick(void)
{
	dlpt.notify_flag = true;
	CHECK(dlpt_notify_handler(NULL) == 0);
	CHECK(!dlpt.notify_lock.held);
}

static void failed_reads(void)
{
	reset();
	read_ret = -ENODATA;
	read_value = 999;
	tick();
	CHECK(reads == 1 && notices == 1);
	CHECK(dlpt.imix_r == 0 && computations == 0 && callbacks == 0);
	jiffies += DLPT_IMIX_R_RETRY_INTERVAL;
	read_ret = -EIO;
	tick();
	CHECK(reads == 2 && notices == 1 && dlpt.imix_r == 0);
	CHECK(computations == 0);
	puts("PASS ENODATA, other error, poisoned output, one boot notice");
}

static void successful_read(void)
{
	reset();
	tick();
	CHECK(reads == 1 && dlpt.imix_r == 117 && computations == 1 && callbacks == 1);
	read_value = 200;
	jiffies += DLPT_IMIX_R_RETRY_INTERVAL;
	tick();
	CHECK(reads == 1 && dlpt.imix_r == 117 && computations == 2);
	reset();
	dlpt.imix_r = 123;
	tick();
	CHECK(reads == 0 && dlpt.imix_r == 123 && computations == 1);
	reset();
	charging = 1;
	tick();
	CHECK(reads == 0 && dlpt.imix_r == 0 && computations == 0);
	tick();
	CHECK(reads == 0 && dlpt.imix_r == 0 && computations == 0);
	charging = 0;
	tick();
	CHECK(reads == 1 && dlpt.imix_r == 117 && computations == 1);
	charging = 1;
	tick();
	CHECK(reads == 1 && dlpt.imix_r == 117 && computations == 2);
	puts("PASS positive cache, both computation branches, filled cache retained, no re-read on charger");
}

static void retry_interval(void)
{
	reset();
	CHECK(DLPT_IMIX_R_RETRY_INTERVAL == 20 * HZ);
	read_ret = -ENODATA;
	ui_soc = 10;
	tick();
	CHECK(timer_at == jiffies + 10 * HZ);
	jiffies += 10 * HZ;
	tick();
	CHECK(reads == 1);
	jiffies += 10 * HZ - 1;
	tick();
	CHECK(reads == 1);
	jiffies++;
	read_ret = 0;
	tick();
	CHECK(reads == 2 && dlpt.imix_r == 117 && computations == 1);
	reset();
	jiffies = ULONG_MAX - 10 * HZ;
	read_ret = -ENODATA;
	tick();
	jiffies += DLPT_IMIX_R_RETRY_INTERVAL - 1;
	tick();
	CHECK(reads == 1);
	jiffies++;
	tick();
	CHECK(reads == 2);
	puts("PASS 20 second bound, exact deadline, jiffies wrap");
}

static void invalid_values(void)
{
	reset();
	read_value = 0;
	tick();
	CHECK(reads == 1 && notices == 1 && dlpt.imix_r == 0 && computations == 0);
	jiffies += DLPT_IMIX_R_RETRY_INTERVAL;
	read_value = -1;
	tick();
	CHECK(reads == 2 && notices == 1 && dlpt.imix_r == 0 && computations == 0);
	puts("PASS zero and negative resistance rejected");
}

static void resume_and_context(void)
{
	reset();
	dlpt_timer_func(&dlpt.notify_timer);
	CHECK(reads == 0 && wakes == 1 && dlpt.notify_flag);
	dlpt.suspend_flag = true;
	tick();
	CHECK(reads == 0 && computations == 0);
	CHECK(dlpt_resume(NULL) == 0);
	CHECK(!dlpt.suspend_flag && reads == 1 && dlpt.imix_r == 117 && wakes == 2);
	CHECK(!dlpt.notify_lock.held);
	dlpt.imix_r_retry_pending = true;
	dlpt.imix_r_retry_at = jiffies + DLPT_IMIX_R_RETRY_INTERVAL;
	read_value = 130;
	CHECK(dlpt_resume(NULL) == 0);
	CHECK(reads == 2 && dlpt.imix_r == 130);
	read_ret = -ENODATA;
	read_value = 999;
	CHECK(dlpt_resume(NULL) == 0);
	CHECK(reads == 3 && dlpt.imix_r == 130 && notices == 1);
	reset();
	read_ret = -ENODATA;
	tick();
	read_ret = 0;
	CHECK(dlpt_resume(NULL) == 0);
	CHECK(reads == 2 && dlpt.imix_r == 117);
	puts("PASS timer only wakes, suspend skips, resume unconditionally reads under mutex");
}

int main(void)
{
	failed_reads();
	successful_read();
	retry_interval();
	invalid_values();
	resume_and_context();
	return 0;
}

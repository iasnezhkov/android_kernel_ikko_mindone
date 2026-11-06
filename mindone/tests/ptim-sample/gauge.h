/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
struct iio_channel { int unused; };
struct mtk_battery { struct { int car_tune_value; } fg_cust_data; };
struct mtk_gauge {
    struct mtk_battery *gm;
    struct iio_channel *chan_ptim_sample;
    struct mutex ops_lock;
    struct { int r_fg_value; } hw_status;
    u64 ptim_sequence;
};
static struct mtk_battery battery;
static struct iio_channel channel;
static struct mtk_gauge gauge;
static int scale_rc, raw_rc, scale_n, scale_d, sample_v, sample_i;
static int scale_calls, raw_calls, time_calls;
static u64 first_time, last_time;

static int iio_read_channel_attribute(struct iio_channel *chan, int *first, int *second, int attr)
{
    CHECK(chan == &channel && gauge.ops_lock.held);
    if (attr == IIO_CHAN_INFO_SCALE) {
        scale_calls++;
        if (scale_rc < 0)
            return scale_rc;
        *first = scale_n;
        *second = scale_d;
        return scale_rc;
    }
    CHECK(attr == IIO_CHAN_INFO_RAW && scale_calls == 1);
    raw_calls++;
    if (raw_rc < 0)
        return raw_rc;
    *first = sample_v;
    *second = sample_i;
    return raw_rc;
}

static u64 ktime_get_boottime_ns(void)
{
    CHECK(gauge.ops_lock.held);
    return time_calls++ ? last_time : first_time;
}

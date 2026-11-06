/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#ifndef EUCLEAN
#define EUCLEAN 117
#endif
#define WRITE_ONCE(value, new_value) ((value) = (new_value))
#define cmpxchg(pointer, old, new) (*(pointer) == (old) ? (*(pointer) = (new), (old)) : *(pointer))
static int io_calls;
static int io_fail_at;
static int io_errno;
static int mismatch_property = -1;
static int gauge_writes[GAUGE_PROP_COUNT];
static int background_error;
static int io_step(bool write)
{
	io_calls++;
	if (write && active_gm->kernel_setup_in_progress)
		active_gm->kernel_setup_mutated = true;
	if (io_calls == io_fail_at) {
		if (write && active_gm->kernel_setup_in_progress && !active_gm->kernel_setup_error)
			active_gm->kernel_setup_error = io_errno;
		return io_errno;
	}
	return 0;
}

// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
static struct gpuppm_platform_fp *saved_gpuppm_fp;

void test_wrapper_eb(int eb, int error)
{
	g_gpueb_support = eb;
	knob_ipi_error = error;
	g_recv_msg.u.return_value = 0;
}

void test_wrapper_drop_gpuppm(int drop)
{
	if (drop) {
		saved_gpuppm_fp = gpuppm_fp;
		gpuppm_fp = NULL;
	} else {
		gpuppm_fp = saved_gpuppm_fp;
	}
}

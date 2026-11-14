/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
static void reset(unsigned long long size, int mapped)
{
	gpu_info_ref = NULL;
	rec_phys_addr = rec_virt_addr = 0;
	rec_size = 0;
	memset(&device, 0, sizeof(device));
	memset(memory, 0x5a, sizeof(memory));
	stub_size = size;
	stub_virt = mapped ? (phys_addr_t)(uintptr_t)memory : 0;
	setups = 0;
}

int main(int argc, char **argv)
{
	const char *name = argc > 1 ? argv[1] : "";

	if (!strcmp(name, "unmapped")) {
		reset(sizeof(memory), 0);
		assert(mtk_bandwith_resource_init(&device) == -1);
		assert(device.v1 == NULL && setups == 0);
	} else if (!strcmp(name, "empty")) {
		reset(0, 1);
		assert(mtk_bandwith_resource_init(&device) == -1);
		assert(device.v1 == NULL && setups == 0);
	} else if (!strcmp(name, "short")) {
		reset(sizeof(struct v1_data) - 1, 1);
		assert(mtk_bandwith_resource_init(&device) == -1);
		assert(device.v1 == NULL && setups == 0);
	} else if (!strcmp(name, "mapped")) {
		reset(sizeof(memory), 1);
		assert(mtk_bandwith_resource_init(&device) == 0);
		assert(device.v1 == (struct v1_data *)memory && device.v1->version == 1);
		assert(device.v1->ctx == 0 && device.v1->job == 0 && setups == 1);
		assert(device.job_status_addr.phyaddr == STUB_PHYS && setup_size == sizeof(memory));
	} else {
		return 2;
	}
	printf("PASS %s\n", name);
	return 0;
}

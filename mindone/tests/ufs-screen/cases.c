// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2026 The LineageOS Project
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../modules/mindone_ufs_screen/mindone_ufs_screen.c"

int ufs_test_warnings;
const struct bus_type platform_bus_type;

static struct device host_dev;
static struct device other_dev;
static int host_present;
static int add_fail;
static int adds;
static int removes;
static int bad_qos;
static int disp_register_fail;
static struct notifier_block *disp_registered;
static struct notifier_block *bus_registered;
static int failures;

static void expect(int condition, const char *name)
{
	if (!condition) {
		printf("FAIL %s (adds %d removes %d bad %d warnings %d)\n", name, adds, removes, bad_qos,
		       ufs_test_warnings);
		failures++;
	}
}

int kstrtoint(const char *s, unsigned int base, int *res)
{
	char *end;
	long value = strtol(s, &end, base);

	if (end == s || *end)
		return -EINVAL;
	*res = (int)value;
	return 0;
}

int param_get_int(char *buffer, const struct kernel_param *kp)
{
	return snprintf(buffer, 16, "%d", *(int *)kp->arg);
}

void mutex_lock(struct mutex *m)
{
	expect(!m->held, "mutex is not taken recursively");
	m->held = 1;
}

void mutex_unlock(struct mutex *m)
{
	expect(m->held, "mutex is held on unlock");
	m->held = 0;
}

void put_device(struct device *dev)
{
	if (dev)
		dev->refs--;
}

struct device *bus_find_device_by_name(const struct bus_type *bus, struct device *start, const char *name)
{
	if (!host_present || strcmp(name, "11270000.ufshci"))
		return NULL;
	host_dev.refs++;
	return &host_dev;
}

int bus_register_notifier(const struct bus_type *bus, struct notifier_block *nb)
{
	bus_registered = nb;
	return 0;
}

int bus_unregister_notifier(const struct bus_type *bus, struct notifier_block *nb)
{
	expect(bus_registered == nb, "bus notifier unregistered once");
	bus_registered = NULL;
	return 0;
}

int dev_pm_qos_add_request(struct device *dev, struct dev_pm_qos_request *req, enum dev_pm_qos_req_type type, int value)
{
	if (req->dev || type != DEV_PM_QOS_MIN_FREQUENCY || dev != &host_dev) {
		bad_qos++;
		return -EINVAL;
	}
	if (add_fail)
		return -ENOMEM;
	req->dev = dev;
	req->value = value;
	adds++;
	return 1;
}

int dev_pm_qos_remove_request(struct dev_pm_qos_request *req)
{
	if (!req->dev) {
		bad_qos++;
		return -EINVAL;
	}
	req->dev = NULL;
	removes++;
	return 0;
}

int mtk_disp_notifier_register(const char *source, struct notifier_block *nb)
{
	if (disp_register_fail)
		return -EBUSY;
	disp_registered = nb;
	return 0;
}

int mtk_disp_notifier_unregister(struct notifier_block *nb)
{
	if (disp_registered != nb)
		return -ENOENT;
	disp_registered = NULL;
	return 0;
}

static void core_destroy_requests(void)
{
	if (min_req.dev == &host_dev)
		memset(&min_req, 0, sizeof(min_req));
}

static void display(int blank)
{
	disp_registered->notifier_call(disp_registered, MTK_DISP_EVENT_BLANK, &blank);
}

static void reset(void)
{
	ufs_dev = NULL;
	ufs_gone = false;
	req_active = false;
	memset(&min_req, 0, sizeof(min_req));
	screen_on_khz = 192000;
	host_dev.refs = 0;
	host_present = 1;
	add_fail = 0;
	adds = removes = bad_qos = ufs_test_warnings = 0;
	disp_register_fail = 0;
	disp_registered = NULL;
	bus_registered = NULL;
}

int main(void)
{
	struct kernel_param kp = { .arg = &screen_on_khz };
	int blank_other = MTK_DISP_BLANK_UNBLANK;

	reset();
	expect(ufs_test_init() == 0, "init succeeds with the host present");
	expect(adds == 1 && min_req.value == 192000 && host_dev.refs == 1, "init takes the floor and a device ref");
	expect(disp_registered && bus_registered, "init registers both notifiers");
	display(MTK_DISP_BLANK_UNBLANK);
	expect(adds == 1 && !bad_qos, "unblank while pinned does not add twice");
	display(MTK_DISP_BLANK_POWERDOWN);
	display(MTK_DISP_BLANK_POWERDOWN);
	expect(removes == 1 && !bad_qos, "powerdown removes the floor once");
	disp_registered->notifier_call(disp_registered, MTK_DISP_EARLY_EVENT_BLANK, &blank_other);
	expect(adds == 1, "early blank events are ignored");
	expect(screen_on_khz_ops.set("0", &kp) == 0, "floor can be set to zero");
	display(MTK_DISP_BLANK_UNBLANK);
	expect(adds == 1, "zero floor never pins");
	expect(screen_on_khz_ops.set("-1", &kp) == -EINVAL && screen_on_khz == 0, "negative floor is rejected");
	expect(screen_on_khz_ops.set("x", &kp) == -EINVAL, "malformed floor is rejected");
	expect(screen_on_khz_ops.set("150000", &kp) == 0, "floor can be changed");
	add_fail = 1;
	display(MTK_DISP_BLANK_UNBLANK);
	expect(adds == 1 && !req_active, "failed request is not recorded as active");
	add_fail = 0;
	display(MTK_DISP_BLANK_UNBLANK);
	expect(adds == 2 && min_req.value == 150000, "next unblank retries with the new floor");
	bus_cb(bus_registered, BUS_NOTIFY_DEL_DEVICE, &other_dev);
	bus_cb(bus_registered, BUS_NOTIFY_ADD_DEVICE, &host_dev);
	expect(removes == 1 && !ufs_gone, "unrelated bus events are ignored");
	bus_cb(bus_registered, BUS_NOTIFY_DEL_DEVICE, &host_dev);
	expect(removes == 2 && ufs_gone, "host removal drops the floor before core teardown");
	core_destroy_requests();
	display(MTK_DISP_BLANK_UNBLANK);
	display(MTK_DISP_BLANK_POWERDOWN);
	expect(adds == 2 && removes == 2, "removed host is left alone");
	ufs_test_exit();
	expect(!bad_qos && !ufs_test_warnings, "no invalid QoS call or warning after host removal");
	expect(host_dev.refs == 0 && !disp_registered && !bus_registered, "exit releases the ref and both notifiers");

	reset();
	expect(ufs_test_init() == 0 && adds == 1, "second lifecycle starts pinned");
	ufs_test_exit();
	expect(removes == 1 && host_dev.refs == 0 && !bad_qos && !ufs_test_warnings, "exit removes an active floor");

	reset();
	host_present = 0;
	expect(ufs_test_init() == 0 && !adds && !bus_registered && !disp_registered, "missing host leaves the module idle");
	ufs_test_exit();
	expect(!ufs_test_warnings && !bad_qos, "idle exit is clean");

	reset();
	disp_register_fail = 1;
	expect(ufs_test_init() == -EBUSY, "display registration failure is returned");
	expect(adds == 1 && removes == 1 && host_dev.refs == 0 && !bus_registered && !ufs_dev, "failed init unwinds");
	expect(!bad_qos && !ufs_test_warnings && !lock.held, "failed init leaves no invalid call or held lock");

	printf("%s %d failures\n", failures ? "FAIL" : "PASS", failures);
	return failures != 0;
}

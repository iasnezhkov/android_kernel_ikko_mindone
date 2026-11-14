/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
static struct adapter_device world_adapter;
static int world_registered;
static int events[32];
static long event_times[32];
static int nevents;
static struct platform_device world_pdev;
static struct mtk_pd_adapter_info *info;

struct adapter_device *adapter_device_register(const char *name, struct device *parent, void *devdata,
					       const struct adapter_ops *ops, const struct adapter_properties *props)
{
	(void)props;
	assert(!strcmp(name, "pd_adapter") && parent == &world_pdev.dev);
	memset(&world_adapter, 0, sizeof(world_adapter));
	world_adapter.ops = ops;
	world_adapter.driver_data = devdata;
	world_registered = 1;
	return &world_adapter;
}

void adapter_device_unregister(struct adapter_device *adapter)
{
	assert(adapter == &world_adapter);
	world_registered = 0;
}

static int charger_event(struct notifier_block *nb, unsigned long event, void *data)
{
	int pd_type = world_adapter.ops->get_property(&world_adapter, PD_TYPE);

	(void)nb;
	(void)data;
	assert(nevents < 32);
	assert(pd_type == (event == MTK_PD_CONNECT_HARD_RESET ? MTK_PD_CONNECT_NONE : (int)event));
	event_times[nevents] = world_now;
	events[nevents++] = (int)event;
	return NOTIFY_OK;
}

static struct notifier_block charger_nb = { .notifier_call = charger_event };

static void tcpm(int online, int usb_type, int mv, int ma)
{
	world_tcpm.online = online;
	world_tcpm.usb_type = usb_type;
	world_tcpm.uv = mv * 1000;
	world_tcpm.ua = ma * 1000;
	if (world_psy_nb)
		world_psy_nb->notifier_call(world_psy_nb, PSY_EVENT_PROP_CHANGED, &world_tcpm);
}

static void boot(void)
{
	memset(&world_tcpm, 0, sizeof(world_tcpm));
	world_tcpm.dev.parent = &world_tcpc_dev;
	world_tcpm.dev.fwnode = &world_typec_node;
	world_tcpm.usb_type = POWER_SUPPLY_USB_TYPE_C;
	memset(&world_pdev, 0, sizeof(world_pdev));
	world_pdev.dev.fwnode = &world_adapter_node;
	world_now = 0;
	world_nworks = 0;
	world_ndelayed = 0;
	nevents = 0;
	assert(mtk_pd_adapter_probe(&world_pdev) == 0);
	info = world_pdev.drvdata;
	world_adapter.evt_nh.nb = &charger_nb;
	advance(1);
}

static void shutdown(void)
{
	mtk_pd_adapter_remove(&world_pdev);
	assert(!world_registered && !world_psy_nb);
	assert(world_tcpm.refs == 0);
}

static int prop(enum adapter_property p)
{
	return world_adapter.ops->get_property(&world_adapter, p);
}

static void expect(int n, const int *list)
{
	int i;

	if (n != nevents) {
		for (i = 0; i < nevents; i++)
			printf("event %d at %ld\n", events[i], event_times[i]);
	}
	assert(n == nevents);
	for (i = 0; i < n; i++)
		assert(events[i] == list[i]);
}

static void case_non_pd(void)
{
	static const int rp3[] = { MTK_PD_CONNECT_TYPEC_ONLY_SNK, MTK_PD_CONNECT_NONE };
	struct adapter_power_cap cap;

	boot();
	assert(prop(PD_TYPE) == MTK_PD_CONNECT_NONE && prop(TYPEC_RP_LEVEL) == 0);
	tcpm(1, POWER_SUPPLY_USB_TYPE_C, 5000, 3000);
	advance(10);
	assert(prop(TYPEC_RP_LEVEL) == 3000 && nevents == 0);
	advance(4900);
	assert(nevents == 0);
	advance(200);
	assert(nevents == 1 && prop(PD_TYPE) == MTK_PD_CONNECT_TYPEC_ONLY_SNK);
	assert(world_adapter.ops->get_cap(&world_adapter, MTK_PD, &cap) == MTK_ADAPTER_OK && cap.nr == 0);
	tcpm(1, POWER_SUPPLY_USB_TYPE_C, 5000, 1500);
	advance(10);
	assert(prop(TYPEC_RP_LEVEL) == 1500 && nevents == 1);
	tcpm(0, POWER_SUPPLY_USB_TYPE_C, 0, 0);
	advance(10);
	expect(2, rp3);
	assert(prop(TYPEC_RP_LEVEL) == 0);
	shutdown();

	boot();
	tcpm(1, POWER_SUPPLY_USB_TYPE_C, 5000, 0);
	advance(6000);
	assert(prop(TYPEC_RP_LEVEL) == 500 && prop(PD_TYPE) == MTK_PD_CONNECT_TYPEC_ONLY_SNK);
	tcpm(0, POWER_SUPPLY_USB_TYPE_C, 0, 0);
	advance(100);
	tcpm(1, POWER_SUPPLY_USB_TYPE_C, 5000, 3000);
	advance(1000);
	tcpm(0, POWER_SUPPLY_USB_TYPE_C, 0, 0);
	advance(10000);
	assert(nevents == 2 && prop(PD_TYPE) == MTK_PD_CONNECT_NONE);
	shutdown();
}

static void case_pd(void)
{
	static const int seq[] = { MTK_PD_CONNECT_PE_READY_SNK, MTK_PD_CONNECT_PE_READY_SNK, MTK_PD_CONNECT_NONE };
	struct adapter_power_cap cap;

	boot();
	tcpm(1, POWER_SUPPLY_USB_TYPE_C, 5000, 3000);
	advance(300);
	tcpm(1, POWER_SUPPLY_USB_TYPE_PD, 5000, 3000);
	advance(10);
	assert(nevents == 1 && events[0] == MTK_PD_CONNECT_PE_READY_SNK);
	assert(prop(TYPEC_RP_LEVEL) == 3000);
	assert(world_adapter.ops->get_cap(&world_adapter, MTK_PD, &cap) == MTK_ADAPTER_OK);
	assert(cap.nr == 1 && cap.selected_cap_idx == 0 && cap.max_mv[0] == 5000 && cap.min_mv[0] == 5000 &&
	       cap.ma[0] == 3000 && cap.type[0] == MTK_PD);
	assert(world_adapter.ops->get_cap(&world_adapter, MTK_PD_APDO, &cap) == MTK_ADAPTER_OK && cap.nr == 0);
	assert(world_adapter.ops->set_cap(&world_adapter, MTK_PD, 5000, 3000) == MTK_ADAPTER_OK);
	assert(world_adapter.ops->set_cap(&world_adapter, MTK_PD, 5000, 2000) == MTK_ADAPTER_OK);
	assert(world_adapter.ops->set_cap(&world_adapter, MTK_PD, 9000, 2000) != MTK_ADAPTER_OK);
	assert(world_adapter.ops->set_cap(&world_adapter, MTK_PD, 5000, 3100) != MTK_ADAPTER_OK);
	assert(world_adapter.ops->set_cap(&world_adapter, MTK_PD_APDO_START, 5000, 3000) != MTK_ADAPTER_OK);
	tcpm(1, POWER_SUPPLY_USB_TYPE_PD, 5000, 2000);
	advance(10);
	assert(nevents == 2);
	advance(10000);
	assert(nevents == 2);
	tcpm(0, POWER_SUPPLY_USB_TYPE_C, 0, 0);
	advance(10);
	expect(3, seq);
	assert(world_adapter.ops->get_cap(&world_adapter, MTK_PD, &cap) == MTK_ADAPTER_OK && cap.nr == 0);
	assert(world_adapter.ops->set_cap(&world_adapter, MTK_PD, 5000, 2000) != MTK_ADAPTER_OK);
	shutdown();
}

static void case_hard_reset(void)
{
	static const int seq[] = { MTK_PD_CONNECT_PE_READY_SNK, MTK_PD_CONNECT_HARD_RESET,
				   MTK_PD_CONNECT_PE_READY_SNK, MTK_PD_CONNECT_NONE };

	boot();
	tcpm(1, POWER_SUPPLY_USB_TYPE_C, 5000, 3000);
	advance(200);
	tcpm(1, POWER_SUPPLY_USB_TYPE_PD, 5000, 3000);
	advance(10);
	tcpm(0, POWER_SUPPLY_USB_TYPE_PD, 5000, 3000);
	advance(10);
	assert(prop(PD_TYPE) == MTK_PD_CONNECT_NONE);
	tcpm(1, POWER_SUPPLY_USB_TYPE_PD, 5000, 3000);
	advance(10);
	tcpm(0, POWER_SUPPLY_USB_TYPE_C, 0, 0);
	advance(10000);
	expect(4, seq);
	shutdown();
}

static void case_pps(void)
{
	static const int seq[] = { MTK_PD_CONNECT_PE_READY_SNK_APDO, MTK_PD_CONNECT_NONE };

	boot();
	tcpm(1, POWER_SUPPLY_USB_TYPE_C, 5000, 3000);
	advance(200);
	tcpm(1, POWER_SUPPLY_USB_TYPE_PD_PPS, 5000, 3000);
	advance(10);
	tcpm(0, POWER_SUPPLY_USB_TYPE_C, 0, 0);
	advance(10);
	expect(2, seq);
	shutdown();
}

static void case_avs(void)
{
	static const int seq[] = { MTK_PD_CONNECT_PE_READY_SNK, MTK_PD_CONNECT_NONE, MTK_PD_CONNECT_PE_READY_SNK_APDO,
				   MTK_PD_CONNECT_NONE };

	boot();
	tcpm(1, POWER_SUPPLY_USB_TYPE_C, 5000, 3000);
	advance(200);
	tcpm(1, POWER_SUPPLY_USB_TYPE_PD_SPR_AVS, 5000, 3000);
	advance(10);
	tcpm(0, POWER_SUPPLY_USB_TYPE_C, 0, 0);
	advance(10);
	tcpm(1, POWER_SUPPLY_USB_TYPE_C, 5000, 3000);
	advance(200);
	tcpm(1, POWER_SUPPLY_USB_TYPE_PD_PPS_SPR_AVS, 5000, 3000);
	advance(10);
	tcpm(0, POWER_SUPPLY_USB_TYPE_C, 0, 0);
	advance(10000);
	expect(4, seq);
	shutdown();
}

static void case_late(void)
{
	static const int seq[] = { MTK_PD_CONNECT_PE_READY_SNK };

	world_tcpm_present = 0;
	boot();
	tcpm(1, POWER_SUPPLY_USB_TYPE_PD, 5000, 3000);
	advance(10);
	assert(nevents == 0 && world_registered);
	world_tcpm_present = 1;
	tcpm(1, POWER_SUPPLY_USB_TYPE_PD, 5000, 3000);
	advance(10);
	expect(1, seq);
	world_eta.dev.parent = &world_eta_dev;
	world_psy_nb->notifier_call(world_psy_nb, PSY_EVENT_PROP_CHANGED, &world_eta);
	advance(10);
	expect(1, seq);
	shutdown();
}

static void case_error(void)
{
	static const int seq[] = { MTK_PD_CONNECT_PE_READY_SNK, MTK_PD_CONNECT_NONE };

	boot();
	tcpm(1, POWER_SUPPLY_USB_TYPE_PD, 5000, 3000);
	advance(10);
	world_tcpm.fail = 1;
	tcpm(1, POWER_SUPPLY_USB_TYPE_PD, 5000, 3000);
	advance(10);
	expect(2, seq);
	world_tcpm.fail = 0;
	shutdown();
}

static void case_coalesced(void)
{
	static const int seq[] = { MTK_PD_CONNECT_TYPEC_ONLY_SNK, MTK_PD_CONNECT_NONE, MTK_PD_CONNECT_PE_READY_SNK };

	boot();
	tcpm(1, POWER_SUPPLY_USB_TYPE_C, 5000, 1500);
	advance(6000);
	assert(prop(TYPEC_RP_LEVEL) == 1500);
	tcpm(0, POWER_SUPPLY_USB_TYPE_C, 0, 0);
	advance(10);
	tcpm(1, POWER_SUPPLY_USB_TYPE_PD, 5000, 2000);
	advance(10);
	expect(3, seq);
	assert(prop(TYPEC_RP_LEVEL) == 500);
	shutdown();
}

int main(int argc, char **argv)
{
	static const struct {
		const char *name;
		void (*run)(void);
	} cases[] = {
		{ "non-pd", case_non_pd }, { "pd", case_pd }, { "hard-reset", case_hard_reset }, { "pps", case_pps }, { "avs", case_avs },
		{ "late", case_late }, { "error", case_error }, { "coalesced", case_coalesced },
	};
	unsigned int i;

	if (argc != 2)
		return 2;
	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		if (!strcmp(argv[1], cases[i].name)) {
			world_tcpm_present = 1;
			cases[i].run();
			printf("PASS %s\n", argv[1]);
			return 0;
		}
	}
	return 2;
}

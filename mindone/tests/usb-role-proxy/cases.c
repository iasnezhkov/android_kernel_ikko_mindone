/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
static struct platform_device world_pdev;
static struct mtk_extcon_info *world_extcon;
static int accdet_events[8];
static int accdet_count;

static int accdet_call(struct notifier_block *nb, unsigned long event, void *data)
{
	(void)nb;
	(void)data;
	assert(accdet_count < 8);
	accdet_events[accdet_count++] = (int)event;
	return NOTIFY_OK;
}

static struct notifier_block accdet_nb = { .notifier_call = accdet_call };

static void reset_world(void)
{
	devm_release_all();
	memset(&world_psy, 0, sizeof(world_psy));
	memset(&world_musb, 0, sizeof(world_musb));
	memset(&world_port, 0, sizeof(world_port));
	memset(&world_mux, 0, sizeof(world_mux));
	memset(&world_pdev, 0, sizeof(world_pdev));
	world_bypass = 1;
	world_musb_present = 1;
	world_psy_present = 1;
	world_mux_fail = 0;
	world_work = NULL;
	world_extcon = NULL;
	world_psy_nb = NULL;
	accdet_count = 0;
	mtk_usb_extcon_unregister_audio_notifier(&accdet_nb);
	mtk_usb_extcon_audio = false;
}

static int boot(int usb_type)
{
	int ret;

	world_psy.usb_type = usb_type;
	ret = mtk_usb_extcon_probe(&world_pdev);
	if (ret)
		return ret;
	world_extcon = world_pdev.drvdata;
	world_work = &world_extcon->role_work;
	drain();
	return 0;
}

static void tcpm_role(enum usb_role role)
{
	assert(world_port_registered);
	world_port.set(&world_port, role);
	drain();
	assert(world_port.get(&world_port) == role);
}

static void tcpm_mode(unsigned long mode, struct typec_altmode *alt)
{
	struct typec_mux_state state = { .alt = alt, .mode = mode };

	assert(world_mux_registered);
	world_mux.set(&world_mux, &state);
	drain();
}

static void charger(int usb_type)
{
	world_psy.usb_type = usb_type;
	if (world_psy_nb)
		world_psy_nb->notifier_call(world_psy_nb, PSY_EVENT_PROP_CHANGED, &world_psy);
	drain();
}

static enum usb_role musb(void)
{
	return world_musb.role;
}

static void case_no_tcpm(void)
{
	static const int types[] = { POWER_SUPPLY_USB_TYPE_SDP, POWER_SUPPLY_USB_TYPE_CDP };
	unsigned int i;

	for (i = 0; i < 2; i++) {
		reset_world();
		assert(boot(types[i]) == 0);
		assert(musb() == USB_ROLE_DEVICE);
		assert(world_edev.state[EXTCON_USB] == 1);
	}
	reset_world();
	assert(boot(POWER_SUPPLY_USB_TYPE_DCP) == 0);
	assert(musb() == USB_ROLE_NONE);
	reset_world();
	assert(boot(POWER_SUPPLY_USB_TYPE_UNKNOWN) == 0);
	assert(musb() == USB_ROLE_NONE);
	charger(POWER_SUPPLY_USB_TYPE_SDP);
	assert(musb() == USB_ROLE_DEVICE);
	charger(POWER_SUPPLY_USB_TYPE_UNKNOWN);
	assert(musb() == USB_ROLE_NONE);
}

static void case_pc(void)
{
	reset_world();
	assert(boot(POWER_SUPPLY_USB_TYPE_UNKNOWN) == 0);
	tcpm_role(USB_ROLE_DEVICE);
	assert(musb() == USB_ROLE_NONE);
	charger(POWER_SUPPLY_USB_TYPE_SDP);
	assert(musb() == USB_ROLE_DEVICE);
	tcpm_role(USB_ROLE_NONE);
	charger(POWER_SUPPLY_USB_TYPE_UNKNOWN);
	assert(musb() == USB_ROLE_NONE);
	assert(world_edev.state[EXTCON_USB] == 0);
}

static void case_otg(void)
{
	reset_world();
	assert(boot(POWER_SUPPLY_USB_TYPE_UNKNOWN) == 0);
	tcpm_role(USB_ROLE_HOST);
	assert(musb() == USB_ROLE_HOST);
	assert(world_edev.state[EXTCON_USB_HOST] == 1);
	charger(POWER_SUPPLY_USB_TYPE_SDP);
	assert(musb() == USB_ROLE_HOST);
	tcpm_role(USB_ROLE_NONE);
	charger(POWER_SUPPLY_USB_TYPE_UNKNOWN);
	assert(musb() == USB_ROLE_NONE);
	assert(world_edev.state[EXTCON_USB_HOST] == 0);
}

static void case_dr_swap(void)
{
	reset_world();
	assert(boot(POWER_SUPPLY_USB_TYPE_SDP) == 0);
	tcpm_role(USB_ROLE_DEVICE);
	assert(musb() == USB_ROLE_DEVICE);
	tcpm_role(USB_ROLE_HOST);
	assert(musb() == USB_ROLE_HOST);
	tcpm_role(USB_ROLE_DEVICE);
	assert(musb() == USB_ROLE_DEVICE);
}

static void case_audio(void)
{
	struct typec_altmode dp = { 0xff01 };

	reset_world();
	assert(boot(POWER_SUPPLY_USB_TYPE_UNKNOWN) == 0);
	assert(mtk_usb_extcon_register_audio_notifier(&accdet_nb) == 0);
	assert(accdet_count == 0);
	tcpm_mode(TYPEC_MODE_AUDIO, &dp);
	assert(accdet_count == 0);
	tcpm_role(USB_ROLE_HOST);
	tcpm_mode(TYPEC_MODE_AUDIO, NULL);
	assert(musb() == USB_ROLE_NONE);
	assert(accdet_count == 1 && accdet_events[0] == MTK_USB_EXTCON_AUDIO_ATTACH);
	tcpm_mode(TYPEC_MODE_AUDIO, NULL);
	assert(accdet_count == 1);
	tcpm_role(USB_ROLE_NONE);
	tcpm_mode(TYPEC_STATE_SAFE, NULL);
	assert(musb() == USB_ROLE_NONE);
	assert(accdet_count == 2 && accdet_events[1] == MTK_USB_EXTCON_AUDIO_DETACH);
	tcpm_role(USB_ROLE_HOST);
	tcpm_mode(TYPEC_STATE_USB, NULL);
	assert(musb() == USB_ROLE_HOST);
	assert(accdet_count == 2);
}

static void case_audio_late(void)
{
	reset_world();
	assert(boot(POWER_SUPPLY_USB_TYPE_UNKNOWN) == 0);
	tcpm_role(USB_ROLE_HOST);
	tcpm_mode(TYPEC_MODE_AUDIO, NULL);
	assert(mtk_usb_extcon_register_audio_notifier(&accdet_nb) == 0);
	assert(accdet_count == 1 && accdet_events[0] == MTK_USB_EXTCON_AUDIO_ATTACH);
	devm_release_all();
	assert(accdet_count == 2 && accdet_events[1] == MTK_USB_EXTCON_AUDIO_DETACH);
	assert(!world_port_registered && !world_mux_registered && !world_psy_nb);
}

static void case_psy_error(void)
{
	reset_world();
	assert(boot(POWER_SUPPLY_USB_TYPE_SDP) == 0);
	assert(musb() == USB_ROLE_DEVICE);
	world_psy.fail = 1;
	tcpm_role(USB_ROLE_NONE);
	assert(musb() == USB_ROLE_DEVICE);
	tcpm_role(USB_ROLE_HOST);
	assert(musb() == USB_ROLE_HOST);
}

static void case_no_bypass(void)
{
	reset_world();
	world_bypass = 0;
	assert(boot(POWER_SUPPLY_USB_TYPE_DCP) == 0);
	assert(musb() == USB_ROLE_NONE);
	tcpm_role(USB_ROLE_DEVICE);
	assert(musb() == USB_ROLE_DEVICE);
	tcpm_role(USB_ROLE_NONE);
	assert(musb() == USB_ROLE_NONE);
}

static void case_probe(void)
{
	reset_world();
	world_musb_present = 0;
	assert(boot(POWER_SUPPLY_USB_TYPE_SDP) == -EPROBE_DEFER);
	assert(!world_port_registered && !world_mux_registered);
	reset_world();
	world_psy_present = 0;
	assert(boot(POWER_SUPPLY_USB_TYPE_SDP) == -EPROBE_DEFER);
	assert(!world_port_registered && !world_mux_registered);
	reset_world();
	assert(boot(POWER_SUPPLY_USB_TYPE_SDP) == 0);
	assert(world_port_registered && world_mux_registered);
	assert(world_port.fwnode == &world_pdev.dev.fwnode && world_mux.fwnode == &world_pdev.dev.fwnode);
}

enum fork_state {
	F_UNATTACHED,
	F_SNK,
	F_SRC,
	F_NORP_SRC,
	F_AUDIO,
	F_STATES,
};

enum fork_pd {
	PD_NONE,
	PD_UFP,
	PD_DFP,
	PD_ROLES,
};

static enum usb_role fork_role(int state, int pd, int usb_type, int bypass)
{
	int data;

	switch (state) {
	case F_SRC:
		return pd == PD_UFP ? USB_ROLE_DEVICE : USB_ROLE_HOST;
	case F_SNK:
	case F_NORP_SRC:
		if (pd == PD_DFP)
			return USB_ROLE_HOST;
		data = !bypass || usb_type == POWER_SUPPLY_USB_TYPE_SDP || usb_type == POWER_SUPPLY_USB_TYPE_CDP;
		return data ? USB_ROLE_DEVICE : USB_ROLE_NONE;
	default:
		return USB_ROLE_NONE;
	}
}

static void tcpm_inputs(int state, int pd, enum usb_role *role, int *audio)
{
	*audio = 0;
	switch (state) {
	case F_SNK:
		*role = pd == PD_DFP ? USB_ROLE_HOST : USB_ROLE_DEVICE;
		return;
	case F_SRC:
		*role = pd == PD_UFP ? USB_ROLE_DEVICE : USB_ROLE_HOST;
		return;
	case F_AUDIO:
		*role = USB_ROLE_HOST;
		*audio = 1;
		return;
	default:
		*role = USB_ROLE_NONE;
		return;
	}
}

static int documented(int state, int pd, int usb_type, int bypass, enum usb_role was, enum usb_role now)
{
	int data = usb_type == POWER_SUPPLY_USB_TYPE_SDP || usb_type == POWER_SUPPLY_USB_TYPE_CDP;

	if (bypass && state == F_UNATTACHED && data && was == USB_ROLE_NONE && now == USB_ROLE_DEVICE)
		return 1;
	if (bypass && state == F_SRC && pd == PD_UFP && !data && was == USB_ROLE_DEVICE && now == USB_ROLE_NONE)
		return 2;
	if (!bypass && state == F_NORP_SRC && pd != PD_DFP && was == USB_ROLE_DEVICE && now == USB_ROLE_NONE)
		return 3;
	return 0;
}

static void case_fork_parity(void)
{
	static const int types[] = { POWER_SUPPLY_USB_TYPE_UNKNOWN, POWER_SUPPLY_USB_TYPE_SDP,
				     POWER_SUPPLY_USB_TYPE_DCP, POWER_SUPPLY_USB_TYPE_CDP };
	int state, pd, t, bypass, audio, same = 0, kinds[4] = { 0 };
	enum usb_role role, was, now;

	for (bypass = 0; bypass < 2; bypass++) {
		for (state = 0; state < F_STATES; state++) {
			for (pd = 0; pd < PD_ROLES; pd++) {
				if (pd != PD_NONE && state != F_SNK && state != F_SRC)
					continue;
				for (t = 0; t < 4; t++) {
					reset_world();
					world_bypass = bypass;
					assert(boot(types[t]) == 0);
					tcpm_inputs(state, pd, &role, &audio);
					tcpm_role(role);
					tcpm_mode(audio ? TYPEC_MODE_AUDIO : TYPEC_STATE_USB, NULL);
					was = fork_role(state, pd, types[t], bypass);
					now = musb();
					if (was == now) {
						same++;
						continue;
					}
					kinds[documented(state, pd, types[t], bypass, was, now)]++;
					if (!documented(state, pd, types[t], bypass, was, now)) {
						printf("fork %d new %d for state %d pd %d type %d bypass %d\n", was, now, state, pd,
						       types[t], bypass);
						assert(!"undocumented difference to the fork");
					}
				}
			}
		}
	}
	printf("fork parity: %d equal, %d adb fallback, %d source-ufp without SDP, %d no-bypass no-Rp\n", same, kinds[1],
	       kinds[2], kinds[3]);
	assert(kinds[1] && kinds[2]);
}

int main(int argc, char **argv)
{
	static const struct {
		const char *name;
		void (*run)(void);
	} cases[] = {
		{ "no-tcpm", case_no_tcpm }, { "pc", case_pc }, { "otg", case_otg }, { "dr-swap", case_dr_swap },
		{ "audio", case_audio }, { "audio-late", case_audio_late }, { "psy-error", case_psy_error },
		{ "no-bypass", case_no_bypass }, { "probe", case_probe }, { "fork-parity", case_fork_parity },
	};
	unsigned int i;

	if (argc != 2)
		return 2;
	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		if (!strcmp(argv[1], cases[i].name)) {
			cases[i].run();
			reset_world();
			printf("PASS %s\n", argv[1]);
			return 0;
		}
	}
	return 2;
}

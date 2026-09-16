// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 MediaTek Inc.
 */

#include <linux/extcon-provider.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/delay.h>  /* MINDONE: msleep for INITSETTLE */
#include <linux/power_supply.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/usb/role.h>
#include <linux/workqueue.h>
#include <linux/proc_fs.h>

/* MINDONE-TCPC (01.09.2026, F3335): CONFIG_TCPC_CLASS is not a Kconfig symbol in
 * our tree - tcpc_class builds out-of-tree, so IS_ENABLED() was always false and
 * the entire TCPC-notifier path was compiled out of every build we've shipped.
 * With a tcpc present that path is the ONLY one that sets USB_ROLE_NONE on
 * Type-C unplug, so unplug never reached musb: the "USB suspend lock" stayed
 * held forever and the kernel made zero suspend attempts (F3211/F3303/F3333).
 * Must precede extcon-mtk-usb.h: the struct's tcpc_dev/tcpc_nb fields share
 * this guard. Full nm/symbol evidence: MINDONE-MODULES-NOTES-0901. */
#define CONFIG_TCPC_CLASS 1

#include "extcon-mtk-usb.h"
//prize add by lipengpeng 20220613 start
#include "mtk_charger.h"
//prize add by lipengpeng 20220613 end

//#include "mtk_charger.h"//prize
#include "sm5602_fg.h"//prize

extern int get_MT5725_status(void);//drv add by liuruiqian for wireless charge lock USB,20241118

#if IS_ENABLED(CONFIG_TCPC_CLASS)
#include <drivers/misc/mediatek/typec/tcpc/inc/tcpm.h>
#endif

static const unsigned int usb_extcon_cable[] = {
	EXTCON_USB,
	EXTCON_USB_HOST,
	EXTCON_NONE,
};

struct mtk_extcon_info * g_extcon = NULL;//prize

static void mtk_usb_extcon_update_role(struct work_struct *work)
{
	struct usb_role_info *role = container_of(to_delayed_work(work),
					struct usb_role_info, dwork);
	struct mtk_extcon_info *extcon = role->extcon;
	unsigned int cur_dr, new_dr;

	cur_dr = extcon->c_role;
	new_dr = role->d_role;

	dev_info(extcon->dev, "cur_dr(%d) new_dr(%d)\n", cur_dr, new_dr);

	/* none -> device */
	if (cur_dr == USB_ROLE_NONE &&
			new_dr == USB_ROLE_DEVICE) {
		extcon_set_state_sync(extcon->edev, EXTCON_USB, true);
	/* none -> host */
	} else if (cur_dr == USB_ROLE_NONE &&
			new_dr == USB_ROLE_HOST) {
		extcon_set_state_sync(extcon->edev, EXTCON_USB_HOST, true);
	/* device -> none */
	} else if (cur_dr == USB_ROLE_DEVICE &&
			new_dr == USB_ROLE_NONE) {
		extcon_set_state_sync(extcon->edev, EXTCON_USB, false);
	/* host -> none */
	} else if (cur_dr == USB_ROLE_HOST &&
			new_dr == USB_ROLE_NONE) {
		extcon_set_state_sync(extcon->edev, EXTCON_USB_HOST, false);
	/* device -> host */
	} else if (cur_dr == USB_ROLE_DEVICE &&
			new_dr == USB_ROLE_HOST) {
		extcon_set_state_sync(extcon->edev, EXTCON_USB, false);
		extcon_set_state_sync(extcon->edev, EXTCON_USB_HOST, true);
	/* host -> device */
	} else if (cur_dr == USB_ROLE_HOST &&
			new_dr == USB_ROLE_DEVICE) {
		extcon_set_state_sync(extcon->edev, EXTCON_USB_HOST, false);
		extcon_set_state_sync(extcon->edev, EXTCON_USB, true);
	}

	/* usb role switch */
	if (extcon->role_sw)
		usb_role_switch_set_role(extcon->role_sw, new_dr);

	extcon->c_role = new_dr;
	kfree(role);
}

static int mtk_usb_extcon_set_role(struct mtk_extcon_info *extcon,
						unsigned int role)
{
	struct usb_role_info *role_info;

	/* create and prepare worker */
	role_info = kzalloc(sizeof(*role_info), GFP_ATOMIC);
	if (!role_info)
		return -ENOMEM;

	INIT_DELAYED_WORK(&role_info->dwork, mtk_usb_extcon_update_role);

	role_info->extcon = extcon;
	role_info->d_role = role;
	/* issue connection work */
	queue_delayed_work(extcon->extcon_wq, &role_info->dwork, 0);

	return 0;
}


int sc89601a_set_roal(int a)//prize
{
	if(!g_extcon){
		pr_err("gezi g_extcon is NULL,return..........\n");
		return -1;
	}
	
	pr_err("gezi mtk_usb_extcon_set_role.....a = %d..\n",a);
	if(a){
		mtk_usb_extcon_set_role(g_extcon,USB_ROLE_DEVICE);
	}
	else{
		mtk_usb_extcon_set_role(g_extcon,USB_ROLE_NONE);
	}
	
	return 0;
}
EXPORT_SYMBOL(sc89601a_set_roal);


static bool usb_is_online(struct mtk_extcon_info *extcon)
{
	union power_supply_propval pval;
	union power_supply_propval tval;
	int ret;

	ret = power_supply_get_property(extcon->usb_psy,
				POWER_SUPPLY_PROP_ONLINE, &pval);
	if (ret < 0) {
		dev_info(extcon->dev, "failed to get online prop\n");
		return false;
	}

	ret = power_supply_get_property(extcon->usb_psy,
				POWER_SUPPLY_PROP_TYPE, &tval);
	if (ret < 0) {
		dev_info(extcon->dev, "failed to get usb type\n");
		return false;
	}

	dev_info(extcon->dev, "online=%d, type=%d\n", pval.intval, tval.intval);

	if (pval.intval && (tval.intval == POWER_SUPPLY_TYPE_USB ||
			tval.intval == POWER_SUPPLY_TYPE_USB_CDP) &&
			0 != get_MT5725_status())//drv mod by liuruiqian for wireless charge lock USB,20241118
		return true;
	else
		return false;
}

/* MINDONE-EXTCON-REPLUG v2 (01.09.2026, F3344 residue): the stock PR_SWAP
 * workaround relies on Type-C detach to drop the role to NONE, but this
 * board's TCPC never delivers detach (DT bypss-typec-sink), so replug after
 * unplug saw a stale role and did nothing. v1 (drop on usb_is_online()==
 * false) got fooled by a transient false "offline" ~56s after boot, killing
 * a live connection. v2 drops the role ONLY on the pure VBUS/online
 * property (real removal); prev_vbus edge-guards against PSY bounce. Full
 * detail: MINDONE-MODULES-NOTES-0901. */
static bool mindone_prev_vbus;

static bool mindone_vbus_present(struct mtk_extcon_info *extcon)
{
	union power_supply_propval pval;
	int ret;

	ret = power_supply_get_property(extcon->usb_psy,
					POWER_SUPPLY_PROP_ONLINE, &pval);
	if (ret < 0)
		return true;	/* never drop the role on a read error */
	return !!pval.intval;
}

static void mtk_usb_extcon_psy_detector(struct work_struct *work)
{
	struct mtk_extcon_info *extcon = container_of(to_delayed_work(work),
		struct mtk_extcon_info, wq_psy);
	bool vbus = mindone_vbus_present(extcon);
	bool online = usb_is_online(extcon);

	dev_info(extcon->dev,
		 "MINDONE-EXTCON-REPLUG: psy event vbus=%d online=%d prev=%d c_role=%d\n",
		 vbus, online, mindone_prev_vbus, extcon->c_role);

	/* MINDONE-EXTCON-TCPCROLE: with a live TCPC the Type-C state machine
	 * owns both raise and drop of the device role — the charger PSY lags
	 * cable events by tens of seconds here, and a stale "offline" arriving
	 * after a TCPC re-attach would kill a live connection. Keep the PSY
	 * path as a raise-only fallback in that case. */
#if IS_ENABLED(CONFIG_TCPC_CLASS)
	if (extcon->tcpc_dev) {
		if (vbus && online && extcon->c_role == USB_ROLE_NONE)
			mtk_usb_extcon_set_role(extcon, USB_ROLE_DEVICE);
		mindone_prev_vbus = vbus;
		return;
	}
#endif

	if (vbus) {
		if (extcon->c_role == USB_ROLE_NONE && online) {
			mtk_usb_extcon_set_role(extcon, USB_ROLE_DEVICE);
		} else if (extcon->c_role == USB_ROLE_DEVICE &&
			   !mindone_prev_vbus) {
			mtk_usb_extcon_set_role(extcon, USB_ROLE_NONE);
			mtk_usb_extcon_set_role(extcon, USB_ROLE_DEVICE);
		}
	} else {
		if (extcon->c_role == USB_ROLE_DEVICE)
			mtk_usb_extcon_set_role(extcon, USB_ROLE_NONE);
	}
	mindone_prev_vbus = vbus;
}

static int mtk_usb_extcon_psy_notifier(struct notifier_block *nb,
				unsigned long event, void *data)
{
	struct power_supply *psy = data;
	struct mtk_extcon_info *extcon = container_of(nb,
					struct mtk_extcon_info, psy_nb);

	if (event != PSY_EVENT_PROP_CHANGED || psy != extcon->usb_psy)
		return NOTIFY_DONE;

	queue_delayed_work(system_power_efficient_wq, &extcon->wq_psy, 0);

	return NOTIFY_DONE;
}

static int mtk_usb_extcon_psy_init(struct mtk_extcon_info *extcon)
{
	int ret = 0;
	struct device *dev = extcon->dev;

	extcon->usb_psy = devm_power_supply_get_by_phandle(dev, "charger");
	if (IS_ERR_OR_NULL(extcon->usb_psy)) {
		/* MINDONE-EXTCON-PSYDEFER: the charger power supply usually registers
		 * after this probe on this device (F3719-era boot log: "fail to get
		 * usb_psy" at 1.41 s, primary_chg at 0.89 s but its psy later). Failing
		 * with -EINVAL silently drops the whole BC1.2 device-role path for the
		 * rest of the boot; defer instead so the probe is retried once the psy
		 * exists. Only devm resources are held at this point. */
		if (PTR_ERR(extcon->usb_psy) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		dev_err(dev, "fail to get usb_psy\n");
		extcon->usb_psy = NULL;
		return -EINVAL;
	}

	INIT_DELAYED_WORK(&extcon->wq_psy, mtk_usb_extcon_psy_detector);

	extcon->psy_nb.notifier_call = mtk_usb_extcon_psy_notifier;
	ret = power_supply_reg_notifier(&extcon->psy_nb);
	if (ret)
		dev_err(dev, "fail to register notifer\n");

	/* MINDONE-EXTCON-REPLUG: seed the edge guard with the real state so
	 * the first charger PSY event after boot does not re-kick an
	 * already-established connection. */
	mindone_prev_vbus = mindone_vbus_present(extcon);
/*
	if (usb_is_online(extcon))
		mtk_usb_extcon_set_role(extcon, USB_ROLE_DEVICE);
	else
		mtk_usb_extcon_set_role(extcon, USB_ROLE_NONE);
*/
	return ret;
}

#if 1//prize IS_ENABLED(CONFIG_CHARGER_RT9458)
/* ADAPT_CHARGER_V1 */
#include <charger_class.h>
static struct charger_device *primary_charger;

static int mtk_usb_extcon_set_vbus_v1(struct mtk_extcon_info *extcon, bool is_on)
{
	struct device *dev = extcon->dev;
	if (!primary_charger) {
		primary_charger = get_charger_by_name("primary_chg");
		if (!primary_charger) {
			dev_info(dev, "%s : get primary charger device failed\n", __func__);
			return -ENODEV;
		}
	}
#if IS_ENABLED(CONFIG_MTK_GAUGE_VERSION) && (CONFIG_MTK_GAUGE_VERSION == 30)
	dev_info(dev, "%s vbus turn %s\n", __func__, is_on ? "on" : "off");
	if (is_on) {
		charger_dev_enable_otg(primary_charger, true);
		charger_dev_set_boost_current_limit(primary_charger,
			1500000);
		#if 0
		{// # workaround
			charger_dev_kick_wdt(primary_charger);
			enable_boost_polling(true);
		}
		#endif
	} else {
		charger_dev_enable_otg(primary_charger, false);
		#if 0
			//# workaround
			enable_boost_polling(false);
		#endif
	}
#else
	if (is_on) {
		charger_dev_enable_otg(primary_charger, true);
		charger_dev_set_boost_current_limit(primary_charger,
			1500000);
	} else {
		charger_dev_enable_otg(primary_charger, false);
	}
#endif
		return 0;
}
#endif

static int mtk_usb_extcon_set_vbus(struct mtk_extcon_info *extcon,
							bool is_on)
{
	int ret;

//prize add by lipengpeng 20210308 start	
#if IS_ENABLED(CONFIG_PRIZE_MT5725_SUPPORT_15W)
	if(is_on){
		//turn_off_5725(1);  //GPIO88 OD5
		//set_otg_gpio(1);  //OD7  87
		set_otg_en_t(1);
	}else{
		//set_otg_gpio(0);//OD7
		//turn_off_5725(0);//OD5   0--->low  1--->high
		set_otg_en_t(0);
	}
#endif
//prize add by lipengpeng 20210308 end

#if 1//prize IS_ENABLED(CONFIG_CHARGER_RT9458)
	ret = mtk_usb_extcon_set_vbus_v1(extcon, is_on);
#else
	struct regulator *vbus = extcon->vbus;
	struct device *dev = extcon->dev;

	/* vbus is optional */
	if (!vbus || extcon->vbus_on == is_on)
		return 0;

	dev_info(dev, "vbus turn %s\n", is_on ? "on" : "off");

	if (is_on) {
		if (extcon->vbus_vol) {
			ret = regulator_set_voltage(vbus,
					extcon->vbus_vol, extcon->vbus_vol);
			if (ret) {
				dev_err(dev, "vbus regulator set voltage failed\n");
				return ret;
			}
		}

		if (extcon->vbus_cur) {
			ret = regulator_set_current_limit(vbus,
					extcon->vbus_cur, extcon->vbus_cur);
			if (ret) {
				dev_err(dev, "vbus regulator set current failed\n");
				return ret;
			}
		}

		ret = regulator_enable(vbus);
		if (ret) {
			dev_err(dev, "vbus regulator enable failed\n");
			return ret;
		}
	} else {
		regulator_disable(vbus);
	}

	extcon->vbus_on = is_on;

	ret = 0;
#endif
	return ret;
}

#if IS_ENABLED(CONFIG_TCPC_CLASS)
/* MINDONE-EXTCON-ROLESYNC (03.09, F3633): setting the extcon role once musb is up brings
 * a boot-present cable to DEVICE; at probe (~1s) it does not stick (TCPC class not yet
 * registered, notifier does not replay). Reconcile the role from the port controller on a
 * delayed work over the boot window: SINK attach -> DEVICE, SRC -> HOST; idempotent, so an
 * active link is never disturbed. Kernel-native, survives image/kernel changes. */
/* MINDONE diagnostics for the boot role reconcile - readable via
 * /sys/module/extcon_mtk_usb/parameters/* (persistent, unlike boot dmesg which
 * wraps before it can be read). */
static int mindone_rs_runs;
module_param(mindone_rs_runs, int, 0644);
static int mindone_rs_have_tcpc;
module_param(mindone_rs_have_tcpc, int, 0644);
static int mindone_rs_cc1 = -1;
module_param(mindone_rs_cc1, int, 0644);
static int mindone_rs_cc2 = -1;
module_param(mindone_rs_cc2, int, 0644);
static int mindone_rs_attach = -1;
module_param(mindone_rs_attach, int, 0644);
static int mindone_rs_crole = -1;
module_param(mindone_rs_crole, int, 0644);
static int mindone_rs_sets;
module_param(mindone_rs_sets, int, 0644);
static int mindone_rs_have_rolesw;
module_param(mindone_rs_have_rolesw, int, 0644);

static void mtk_usb_extcon_rolesync_work(struct work_struct *work)
{
	struct mtk_extcon_info *extcon = container_of(to_delayed_work(work),
		struct mtk_extcon_info, wq_rolesync);
	struct tcpc_device *tcpc_dev = extcon->tcpc_dev;

	mindone_rs_runs++;
	mindone_rs_crole = extcon->c_role;
	mindone_rs_have_rolesw = extcon->role_sw ? 1 : 0;

	if (!tcpc_dev) {
		struct device_node *np = extcon->dev->of_node;
		const char *tcpc_name;

		if (of_property_read_string(np, "tcpc", &tcpc_name) == 0)
			tcpc_dev = tcpc_dev_get_by_name(tcpc_name);
	}
	mindone_rs_have_tcpc = tcpc_dev ? 1 : 0;
	if (tcpc_dev) {
		uint8_t cc1 = 0, cc2 = 0;

		tcpm_inquire_remote_cc(tcpc_dev, &cc1, &cc2, true);
		mindone_rs_cc1 = cc1;
		mindone_rs_cc2 = cc2;
		mindone_rs_attach = tcpm_inquire_typec_attach_state(tcpc_dev);
	}
	/* Confirmed on-device that driving the role to DEVICE once musb is up
	 * enumerates a boot-present cable. The TCPC attach-state query and CC read
	 * proved unreliable at boot, so reconcile unconditionally: while our role is
	 * NONE, drive DEVICE (this board is a peripheral; a real HOST/OTG attach is
	 * corrected by the TCPC notifier's SRC path). Idempotent - once the role is
	 * DEVICE we stop touching it, so a live link is never disturbed.
	 *
	 * NOTE: do NOT gate this on !bypss_typec_sink. DT sets mediatek,bypss-typec-sink
	 * on this board, which means "the charger stack raises the sink/device role" -
	 * but here it never does (F3344), so every sink path that honoured that flag
	 * (INITSTATE and earlier tries) silently skipped and the role stayed NONE.
	 * That flag being set was the actual reason the boot-present cable was never
	 * brought up. */
	if (extcon->c_role == USB_ROLE_NONE) {
		mindone_rs_sets++;
		dev_info(extcon->dev,
			 "MINDONE-EXTCON-ROLESYNC: role NONE (cc=%d/%d attach=%d rolesw=%d), drive DEVICE (try %d)\n",
			 mindone_rs_cc1, mindone_rs_cc2, mindone_rs_attach,
			 mindone_rs_have_rolesw, extcon->rolesync_tries);
		mtk_usb_extcon_set_role(extcon, USB_ROLE_DEVICE);
	}
	/* keep reconciling across the boot window (userspace configures the gadget
	 * around boot_completed ~30 s); the NONE guard above makes each pass a no-op
	 * once the role took. */
	if (++extcon->rolesync_tries < 14)
		schedule_delayed_work(&extcon->wq_rolesync, msecs_to_jiffies(3000));
}
static int mtk_extcon_tcpc_notifier(struct notifier_block *nb,
		unsigned long event, void *data)
{
	struct tcp_notify *noti = data;
	struct mtk_extcon_info *extcon =
			container_of(nb, struct mtk_extcon_info, tcpc_nb);
	struct device *dev = extcon->dev;
	bool vbus_on;

	switch (event) {
	case TCP_NOTIFY_SOURCE_VBUS:
		dev_info(dev, "source vbus = %dmv\n",
				 noti->vbus_state.mv);
		vbus_on = (noti->vbus_state.mv) ? true : false;
		mtk_usb_extcon_set_vbus(extcon, vbus_on);
		break;
	case TCP_NOTIFY_TYPEC_STATE:
		/* MINDONE-EXTCON-TCPCROLE (01.09.2026, F3386/F3344): on this board the TCPC
		 * is the ONLY live runtime source of cable events (the charger stack
		 * delivers none - CHRDET unrequested). Stock code gated the SINK-attach
		 * branch behind DT bypss-typec-sink, expecting the charger stack to raise
		 * the device role - which never happens here, so an unplugged role stayed
		 * NONE forever. Drive the device role from the TCPC state machine
		 * unconditionally: SNK-class attach -> DEVICE (re-kick through NONE if
		 * stale), UNATTACHED -> NONE. Detail: MINDONE-MODULES-NOTES-0901. */
		dev_info(dev, "MINDONE-EXTCON-TCPCROLE: typec %d -> %d (c_role=%d)\n",
				noti->typec_state.old_state,
				noti->typec_state.new_state, extcon->c_role);
		switch (noti->typec_state.new_state) {
		case TYPEC_ATTACHED_SRC:
			dev_info(dev, "Type-C SRC plug in\n");
			mtk_usb_extcon_set_role(extcon, USB_ROLE_HOST);
			break;
		case TYPEC_ATTACHED_SNK:
		case TYPEC_ATTACHED_NORP_SRC:
		case TYPEC_ATTACHED_CUSTOM_SRC:
		case TYPEC_ATTACHED_DBGACC_SNK:
			dev_info(dev, "Type-C SINK plug in\n");
			if (extcon->c_role == USB_ROLE_DEVICE)
				mtk_usb_extcon_set_role(extcon, USB_ROLE_NONE);
			mtk_usb_extcon_set_role(extcon, USB_ROLE_DEVICE);
			break;
		case TYPEC_UNATTACHED:
			dev_info(dev, "Type-C plug out\n");
			mtk_usb_extcon_set_role(extcon, USB_ROLE_NONE);
			break;
		default:
			break;
		}
		break;
	case TCP_NOTIFY_DR_SWAP:
		dev_info(dev, "%s dr_swap, new role=%d\n",
				__func__, noti->swap_state.new_role);
		if (noti->swap_state.new_role == PD_ROLE_UFP &&
				extcon->c_role == USB_ROLE_HOST) {//prize
			dev_info(dev, "switch role to device\n");
			mtk_usb_extcon_set_role(extcon, USB_ROLE_NONE);
			mtk_usb_extcon_set_role(extcon, USB_ROLE_DEVICE);
		} else if (noti->swap_state.new_role == PD_ROLE_DFP &&
				extcon->c_role == USB_ROLE_DEVICE) {//prize
			dev_info(dev, "switch role to host\n");
			mtk_usb_extcon_set_role(extcon, USB_ROLE_NONE);
			mtk_usb_extcon_set_role(extcon, USB_ROLE_HOST);
		}
		break;
	}

	return NOTIFY_OK;
}

static int mtk_usb_extcon_tcpc_init(struct mtk_extcon_info *extcon)
{
	struct tcpc_device *tcpc_dev;
	struct device_node *np = extcon->dev->of_node;
	const char *tcpc_name;
	int ret;

	ret = of_property_read_string(np, "tcpc", &tcpc_name);
	if (ret < 0)
		return -ENODEV;

	tcpc_dev = tcpc_dev_get_by_name(tcpc_name);
	if (!tcpc_dev) {
		dev_err(extcon->dev, "get tcpc device fail\n");
		return -ENODEV;
	}

	extcon->tcpc_nb.notifier_call = mtk_extcon_tcpc_notifier;
	ret = register_tcp_dev_notifier(tcpc_dev, &extcon->tcpc_nb,
		TCP_NOTIFY_TYPE_USB | TCP_NOTIFY_TYPE_VBUS |
		TCP_NOTIFY_TYPE_MISC);
	if (ret < 0) {
		dev_err(extcon->dev, "register notifer fail\n");
		return -EINVAL;
	}

	extcon->tcpc_dev = tcpc_dev;

	/* MINDONE-EXTCON-INITSTATE (01.09.2026, F3342/F2957/F3212): the
	 * TCP_NOTIFY_TYPEC_STATE handler only reacts to TRANSITIONS (UNATTACHED ->
	 * attached). A cable already attached before this module registered its
	 * notifier (boot with cable in; load order isn't deterministic) produces no
	 * transition - event lost forever, extcon stays USB=0 despite typec+BC1.2
	 * SDP. The psy fallback only fires on PSY_EVENT_PROP_CHANGED and its
	 * initial-state call shipped commented out. Query the attach state once,
	 * right after registration, and replay it. */
	{
		uint8_t st = tcpm_inquire_typec_attach_state(tcpc_dev);
		uint8_t cc1 = 0, cc2 = 0;
		int i;

		/* MINDONE-EXTCON-INITSETTLE (03.09): register_tcp_dev_notifier() does not replay
		 * the current state, and the controller may still be classifying a boot-attached
		 * cable when first asked. While CC shows something but type is UNATTACHED, wait
		 * (bounded ~1s, exits as soon as it classifies) for the real state, then replay. */
		for (i = 0; st == TYPEC_UNATTACHED && i < 50; i++) {
			tcpm_inquire_remote_cc(tcpc_dev, &cc1, &cc2, true);
			if ((cc1 == TYPEC_CC_VOLT_OPEN || cc1 == TYPEC_CC_DRP_TOGGLING) &&
			    (cc2 == TYPEC_CC_VOLT_OPEN || cc2 == TYPEC_CC_DRP_TOGGLING))
				break;
			msleep(20);
			st = tcpm_inquire_typec_attach_state(tcpc_dev);
		}
		if (i)
			dev_info(extcon->dev,
				 "MINDONE-EXTCON-INITSETTLE: waited %d ms, attach_state=%u cc=%u/%u\n",
				 i * 20, st, cc1, cc2);

		if (st == TYPEC_ATTACHED_SRC) {
			dev_info(extcon->dev,
				 "MINDONE-EXTCON-INITSTATE: attach_state=%u already SRC, replay host\n",
				 st);
			mtk_usb_extcon_set_role(extcon, USB_ROLE_HOST);
		} else if (!(extcon->bypss_typec_sink) &&
			   (st == TYPEC_ATTACHED_SNK ||
			    st == TYPEC_ATTACHED_NORP_SRC ||
			    st == TYPEC_ATTACHED_CUSTOM_SRC ||
			    st == TYPEC_ATTACHED_DBGACC_SNK)) {
			dev_info(extcon->dev,
				 "MINDONE-EXTCON-INITSTATE: attach_state=%u already SNK, replay device\n",
				 st);
			mtk_usb_extcon_set_role(extcon, USB_ROLE_DEVICE);
		} else {
			dev_info(extcon->dev,
				 "MINDONE-EXTCON-INITSTATE: attach_state=%u, nothing to replay\n",
				 st);
		}
	}

	return 0;
}
#endif

static void mtk_usb_extcon_detect_cable(struct work_struct *work)
{
	struct mtk_extcon_info *extcon = container_of(to_delayed_work(work),
		struct mtk_extcon_info, wq_detcable);
	int id;

	/* check ID and update cable state */
	id = extcon->id_gpiod ?
		gpiod_get_value_cansleep(extcon->id_gpiod) : 1;

	/* at first we clean states which are no longer active */
	if (id) {
		mtk_usb_extcon_set_vbus(extcon, false);
		mtk_usb_extcon_set_role(extcon, USB_ROLE_NONE);
	} else {
		mtk_usb_extcon_set_vbus(extcon, true);
		mtk_usb_extcon_set_role(extcon, USB_ROLE_HOST);
	}
}

static irqreturn_t mtk_usb_idpin_handle(int irq, void *dev_id)
{
	struct mtk_extcon_info *extcon = dev_id;

	/* issue detection work */
	queue_delayed_work(system_power_efficient_wq, &extcon->wq_detcable, 0);

	return IRQ_HANDLED;
}

static int mtk_usb_extcon_id_pin_init(struct mtk_extcon_info *extcon)
{
	int ret = 0;
	int id;

	extcon->id_gpiod = devm_gpiod_get(extcon->dev, "id", GPIOD_IN);

	if (!extcon->id_gpiod || IS_ERR(extcon->id_gpiod)) {
		dev_info(extcon->dev, "failed to get id gpio\n");
		return -ENODEV;
	}

	extcon->id_irq = gpiod_to_irq(extcon->id_gpiod);
	if (extcon->id_irq < 0) {
		dev_info(extcon->dev, "failed to get ID IRQ\n");
		return extcon->id_irq;
	}

	INIT_DELAYED_WORK(&extcon->wq_detcable, mtk_usb_extcon_detect_cable);

	ret = devm_request_threaded_irq(extcon->dev, extcon->id_irq, NULL,
			mtk_usb_idpin_handle, IRQF_TRIGGER_RISING |
			IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
			dev_name(extcon->dev), extcon);

	if (ret < 0) {
		dev_info(extcon->dev, "failed to request handler for ID IRQ\n");
		return ret;
	}

	/* get id pin value when boot on */
	id = extcon->id_gpiod ?
		gpiod_get_value_cansleep(extcon->id_gpiod) : 1;
	dev_info(extcon->dev, "id value : %d\n", id);
	if (!id) {
		mtk_usb_extcon_set_vbus(extcon, true);
		mtk_usb_extcon_set_role(extcon, USB_ROLE_HOST);
	}

	return 0;
}

#if IS_ENABLED(CONFIG_TCPC_CLASS)
#define PROC_FILE_SMT "mtk_typec"
#define FILE_SMT_U2_CC_MODE "mtk_typec/smt_u2_cc_mode"

static int usb_cc_smt_procfs_show(struct seq_file *s, void *unused)
{
	struct mtk_extcon_info *extcon = s->private;
	struct device_node *np = extcon->dev->of_node;
	const char *tcpc_name;
	uint8_t cc1, cc2;
	char buf[2];
	int ret;

	ret = of_property_read_string(np, "tcpc", &tcpc_name);
	if (ret < 0)
		return -ENODEV;

	extcon->tcpc_dev = tcpc_dev_get_by_name(tcpc_name);
	if (!extcon->tcpc_dev)
		return -ENODEV;

	tcpm_inquire_remote_cc(extcon->tcpc_dev, &cc1, &cc2, false);
	dev_info(extcon->dev, "cc1=%d, cc2=%d\n", cc1, cc2);

	if (cc1 == TYPEC_CC_VOLT_OPEN || cc1 == TYPEC_CC_DRP_TOGGLING)
		seq_puts(s, "0\n");
	else if (cc2 == TYPEC_CC_VOLT_OPEN || cc2 == TYPEC_CC_DRP_TOGGLING)
		seq_puts(s, "0\n");
	else
		seq_puts(s, "1\n");
	buf[1] = '\0';

	return 0;
}

static int usb_cc_smt_procfs_open(struct inode *inode, struct file *file)
{
	return single_open(file, usb_cc_smt_procfs_show, pde_data(inode));
}

static const struct  proc_ops usb_cc_smt_procfs_fops = {
	.proc_open = usb_cc_smt_procfs_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int mtk_usb_extcon_procfs_init(struct mtk_extcon_info *extcon)
{
	struct proc_dir_entry *file;
	struct proc_dir_entry *root;

	root = proc_mkdir(PROC_FILE_SMT, NULL);
	file = proc_create_data(FILE_SMT_U2_CC_MODE, 0400, NULL,
		&usb_cc_smt_procfs_fops, extcon);

	return 0;
}
#endif

static int mtk_usb_extcon_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_extcon_info *extcon;
#if IS_ENABLED(CONFIG_TCPC_CLASS)
	const char *tcpc_name;
#endif
	int ret;
      printk("gezi---------mtk_usb_extcon_probe\n");//prize
	extcon = devm_kzalloc(&pdev->dev, sizeof(*extcon), GFP_KERNEL);
	if (!extcon)
		return -ENOMEM;

	extcon->dev = dev;

#if IS_ENABLED(CONFIG_TCPC_CLASS)
	/* MINDONE-EXTCON-TCPCDEFER (03.09, F3630): rt1711h registers its tcpc_class device
	 * from its own probe; if extcon probes first, tcpc_init() below fails non-fatally, the
	 * TCPC notifier is never registered and a boot-attached cable stays charge-only until
	 * a replug. Defer until the named controller exists (only the devm kzalloc above is
	 * held, so -EPROBE_DEFER leaks nothing). */
	if (of_property_read_string(dev->of_node, "tcpc", &tcpc_name) == 0 &&
	    !tcpc_dev_get_by_name(tcpc_name))
		return -EPROBE_DEFER;
#endif

	/* extcon */
	extcon->edev = devm_extcon_dev_allocate(dev, usb_extcon_cable);
	if (IS_ERR(extcon->edev)) {
		dev_err(dev, "failed to allocate extcon device\n");
		return -ENOMEM;
	}

	ret = devm_extcon_dev_register(dev, extcon->edev);
	if (ret < 0) {
		dev_info(dev, "failed to register extcon device\n");
		return ret;
	}

	/* usb role switch */
	extcon->role_sw = usb_role_switch_get(extcon->dev);
	if (IS_ERR(extcon->role_sw)) {
		/* MINDONE 12.09: the role-switch registers the USB controller later than
		 * extcon; until then usb_role_switch_get() legitimately returns -EPROBE_DEFER --
		 * this is not an error, just a normal retry (54 "failed to get usb role" lines
		 * per boot). */
		if (PTR_ERR(extcon->role_sw) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		dev_err(dev, "failed to get usb role\n");
		return PTR_ERR(extcon->role_sw);
	}

	/* initial usb role */
	if (extcon->role_sw)
		extcon->c_role = USB_ROLE_NONE;

	/* vbus */
	extcon->vbus = devm_regulator_get(dev, "vbus");
	if (IS_ERR(extcon->vbus)) {
		dev_err(dev, "failed to get vbus\n");
		return PTR_ERR(extcon->vbus);
	}

	/* sync vbus state */
	if (extcon->vbus) {
		extcon->vbus_on = regulator_is_enabled(extcon->vbus);
		dev_info(dev, "vbus is %s\n", extcon->vbus_on ? "on" : "off");

		if (!of_property_read_u32(dev->of_node, "vbus-voltage",
					&extcon->vbus_vol))
			dev_info(dev, "vbus-voltage=%d", extcon->vbus_vol);

		if (!of_property_read_u32(dev->of_node, "vbus-current",
					&extcon->vbus_cur))
			dev_info(dev, "vbus-current=%d", extcon->vbus_cur);
	}

	extcon->bypss_typec_sink =
		of_property_read_bool(dev->of_node,
			"mediatek,bypss-typec-sink");

#if IS_ENABLED(CONFIG_TCPC_CLASS)
	ret = of_property_read_string(dev->of_node, "tcpc", &tcpc_name);
	if (of_property_read_bool(dev->of_node, "mediatek,u2") && ret == 0
		&& strcmp(tcpc_name, "type_c_port0") == 0) {
		dev_info(dev, "create %s dir\n", PROC_FILE_SMT);
		mtk_usb_extcon_procfs_init(extcon);
	}
#endif

	extcon->extcon_wq = create_singlethread_workqueue("extcon_usb");
	if (!extcon->extcon_wq)
		return -ENOMEM;

	/* get id resources */
	ret = mtk_usb_extcon_id_pin_init(extcon);
	if (ret < 0)
		dev_info(dev, "failed to init id pin\n");

	/* power psy */
	ret = mtk_usb_extcon_psy_init(extcon);
	if (ret == -EPROBE_DEFER)
		return ret;
	if (ret < 0)
		dev_err(dev, "failed to init psy\n");

#if IS_ENABLED(CONFIG_TCPC_CLASS)
	/* tcpc */
	ret = mtk_usb_extcon_tcpc_init(extcon);
	if (ret < 0)
		dev_err(dev, "failed to init tcpc\n");

	/* MINDONE-EXTCON-ROLESYNC (F3633): reconcile the data role from the TCPC once
	 * musb is up, so a cable already attached at boot enumerates without a replug.
	 * See mtk_usb_extcon_rolesync_work(). */
	INIT_DELAYED_WORK(&extcon->wq_rolesync, mtk_usb_extcon_rolesync_work);
	extcon->rolesync_tries = 0;
	schedule_delayed_work(&extcon->wq_rolesync, msecs_to_jiffies(3000));
#endif

	platform_set_drvdata(pdev, extcon);

	g_extcon = extcon;//prize

	/* MINDONE-EXTCON-INITSTATE-PSY (01.09.2026, F3342/F2957/F3212): on this
	 * device DT sets mediatek,bypss-typec-sink, so device-role comes ONLY from
	 * the psy path (mtk_usb_extcon_psy_detector), which fires solely on
	 * PSY_EVENT_PROP_CHANGED. A charger-detect (BC1.2) concluded BEFORE this
	 * notifier registered leaves no event to react to - role stays NONE, gadget
	 * never starts. The factory initial-state check in psy_init shipped
	 * commented out. Replay once here, after tcpc_dev is set (c_role==NONE
	 * guard keeps it a no-op if a real event already handled it). */
	if (extcon->usb_psy)
		queue_delayed_work(system_power_efficient_wq, &extcon->wq_psy, 0);

	return 0;
}

static void mtk_usb_extcon_remove(struct platform_device *pdev)
{
}

static void mtk_usb_extcon_shutdown(struct platform_device *pdev)
{
	struct mtk_extcon_info *extcon = platform_get_drvdata(pdev);

	dev_info(extcon->dev, "%s\n", __func__);

	mtk_usb_extcon_set_vbus(extcon, false);
}

static const struct of_device_id mtk_usb_extcon_of_match[] = {
	{ .compatible = "mediatek,extcon-usb", },
	{ },
};
MODULE_DEVICE_TABLE(of, mtk_usb_extcon_of_match);

static struct platform_driver mtk_usb_extcon_driver = {
	.probe		= mtk_usb_extcon_probe,
	.remove_new		= mtk_usb_extcon_remove,
	.shutdown	= mtk_usb_extcon_shutdown,
	.driver		= {
		.name	= "mtk-extcon-usb",
		.of_match_table = mtk_usb_extcon_of_match,
	},
};

static int __init mtk_usb_extcon_init(void)
{
	printk("gezi---------mtk_usb_extcon_init\n");//prize

	/* mind_one F551: reference-platform ODM hack removed. The 5000ms
	 * schedule_delayed_work() indirection (register the platform_driver only
	 * after a fixed 5s delay) is the exact pattern F049 disassembled OUT of
	 * the real stock module and traced to a DIFFERENT device's vendor tree,
	 * not ours. It had regressed back into this source file; restored to a
	 * direct, synchronous registration matching F049's disassembly of the
	 * real .ko. */
	return platform_driver_register(&mtk_usb_extcon_driver);
}
late_initcall_sync(mtk_usb_extcon_init);//prize

static void __exit mtk_usb_extcon_exit(void)
{
	platform_driver_unregister(&mtk_usb_extcon_driver);
}
module_exit(mtk_usb_extcon_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MediaTek Extcon USB Driver");

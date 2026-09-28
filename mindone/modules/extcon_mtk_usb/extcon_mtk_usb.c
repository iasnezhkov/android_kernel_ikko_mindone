// SPDX-License-Identifier: GPL-2.0

#include <linux/extcon-provider.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/regulator/consumer.h>
#include <linux/spinlock.h>
#include <linux/usb/role.h>
#include <linux/workqueue.h>
#include <drivers/misc/mediatek/typec/tcpc/inc/tcpm.h>

#define PD_ROLE_NONE	0xff

struct mtk_extcon_info {
	struct device *dev;
	struct extcon_dev *edev;
	struct usb_role_switch *role_sw;
	struct regulator *vbus;
	struct power_supply *chg_psy;
	struct tcpc_device *tcpc_dev;
	struct notifier_block psy_nb;
	struct notifier_block tcpc_nb;
	struct workqueue_struct *wq;
	struct work_struct role_work;
	spinlock_t state_lock;
	enum usb_role role;
	u8 typec_state;
	u8 pd_data_role;
	bool typec_event_seen;
	bool vbus_on;
	bool bypss_typec_sink;
};

static const unsigned int usb_extcon_cable[] = {
	EXTCON_USB,
	EXTCON_USB_HOST,
	EXTCON_NONE,
};

static int mtk_usb_extcon_data_port(struct mtk_extcon_info *extcon)
{
	union power_supply_propval val;
	int ret;

	if (!extcon->bypss_typec_sink)
		return 1;
	ret = power_supply_get_property(extcon->chg_psy,
					POWER_SUPPLY_PROP_USB_TYPE, &val);
	if (ret)
		return ret;
	return val.intval == POWER_SUPPLY_USB_TYPE_SDP ||
	       val.intval == POWER_SUPPLY_USB_TYPE_CDP;
}

static int mtk_usb_extcon_desired_role(struct mtk_extcon_info *extcon,
				       enum usb_role *role)
{
	unsigned long flags;
	u8 typec_state, pd_data_role;
	int data;

	spin_lock_irqsave(&extcon->state_lock, flags);
	typec_state = extcon->typec_state;
	pd_data_role = extcon->pd_data_role;
	spin_unlock_irqrestore(&extcon->state_lock, flags);

	switch (typec_state) {
	case TYPEC_ATTACHED_SRC:
		*role = pd_data_role == PD_ROLE_UFP ? USB_ROLE_DEVICE :
						       USB_ROLE_HOST;
		return 0;
	case TYPEC_ATTACHED_SNK:
	case TYPEC_ATTACHED_NORP_SRC:
	case TYPEC_ATTACHED_CUSTOM_SRC:
	case TYPEC_ATTACHED_DBGACC_SNK:
		if (pd_data_role == PD_ROLE_DFP) {
			*role = USB_ROLE_HOST;
			return 0;
		}
		data = mtk_usb_extcon_data_port(extcon);
		if (data < 0)
			return data;
		*role = data ? USB_ROLE_DEVICE : USB_ROLE_NONE;
		return 0;
	default:
		*role = USB_ROLE_NONE;
		return 0;
	}
}

static void mtk_usb_extcon_set_role(struct mtk_extcon_info *extcon,
				    enum usb_role role)
{
	enum usb_role old = extcon->role;
	int ret;

	if (role == old)
		return;
	if (old == USB_ROLE_DEVICE)
		extcon_set_state_sync(extcon->edev, EXTCON_USB, false);
	else if (old == USB_ROLE_HOST)
		extcon_set_state_sync(extcon->edev, EXTCON_USB_HOST, false);
	if (role == USB_ROLE_DEVICE)
		extcon_set_state_sync(extcon->edev, EXTCON_USB, true);
	else if (role == USB_ROLE_HOST)
		extcon_set_state_sync(extcon->edev, EXTCON_USB_HOST, true);

	ret = usb_role_switch_set_role(extcon->role_sw, role);
	if (ret)
		dev_err(extcon->dev, "usb role %s: %d\n", usb_role_string(role),
			ret);
	extcon->role = role;
	dev_info(extcon->dev, "usb role %s -> %s\n", usb_role_string(old),
		 usb_role_string(role));
}

static void mtk_usb_extcon_role_work(struct work_struct *work)
{
	struct mtk_extcon_info *extcon =
		container_of(work, struct mtk_extcon_info, role_work);
	enum usb_role role;
	int ret;

	ret = mtk_usb_extcon_desired_role(extcon, &role);
	if (ret) {
		dev_warn(extcon->dev, "charger type unreadable (%d), role kept\n",
			 ret);
		return;
	}
	mtk_usb_extcon_set_role(extcon, role);
}

static void mtk_usb_extcon_set_vbus(struct mtk_extcon_info *extcon, bool on)
{
	int ret;

	if (extcon->vbus_on == on)
		return;
	ret = on ? regulator_enable(extcon->vbus) :
		   regulator_disable(extcon->vbus);
	if (ret) {
		dev_err(extcon->dev, "vbus %s: %d\n", on ? "on" : "off", ret);
		return;
	}
	extcon->vbus_on = on;
	dev_info(extcon->dev, "vbus %s\n", on ? "on" : "off");
}

static int mtk_usb_extcon_psy_notifier(struct notifier_block *nb,
				       unsigned long event, void *data)
{
	struct mtk_extcon_info *extcon =
		container_of(nb, struct mtk_extcon_info, psy_nb);

	if (event == PSY_EVENT_PROP_CHANGED && data == extcon->chg_psy)
		queue_work(extcon->wq, &extcon->role_work);
	return NOTIFY_DONE;
}

static int mtk_usb_extcon_tcpc_notifier(struct notifier_block *nb,
					unsigned long event, void *data)
{
	struct mtk_extcon_info *extcon =
		container_of(nb, struct mtk_extcon_info, tcpc_nb);
	struct tcp_notify *noti = data;
	unsigned long flags;

	switch (event) {
	case TCP_NOTIFY_SOURCE_VBUS:
		mtk_usb_extcon_set_vbus(extcon, noti->vbus_state.mv > 0);
		break;
	case TCP_NOTIFY_TYPEC_STATE:
		dev_info(extcon->dev, "typec state %u -> %u\n",
			 noti->typec_state.old_state,
			 noti->typec_state.new_state);
		spin_lock_irqsave(&extcon->state_lock, flags);
		extcon->typec_state = noti->typec_state.new_state;
		extcon->pd_data_role = PD_ROLE_NONE;
		extcon->typec_event_seen = true;
		spin_unlock_irqrestore(&extcon->state_lock, flags);
		queue_work(extcon->wq, &extcon->role_work);
		break;
	case TCP_NOTIFY_DR_SWAP:
		dev_info(extcon->dev, "data role swap to %s\n",
			 noti->swap_state.new_role == PD_ROLE_DFP ? "dfp" : "ufp");
		spin_lock_irqsave(&extcon->state_lock, flags);
		extcon->pd_data_role = noti->swap_state.new_role;
		spin_unlock_irqrestore(&extcon->state_lock, flags);
		queue_work(extcon->wq, &extcon->role_work);
		break;
	}
	return NOTIFY_OK;
}

static void mtk_usb_extcon_put_role_sw(void *data)
{
	usb_role_switch_put(data);
}

static void mtk_usb_extcon_destroy_wq(void *data)
{
	destroy_workqueue(data);
}

static void mtk_usb_extcon_psy_unreg(void *data)
{
	power_supply_unreg_notifier(data);
}

static void mtk_usb_extcon_tcpc_unreg(void *data)
{
	struct mtk_extcon_info *extcon = data;

	unregister_tcp_dev_notifier(extcon->tcpc_dev, &extcon->tcpc_nb,
				    TCP_NOTIFY_TYPE_USB | TCP_NOTIFY_TYPE_VBUS |
				    TCP_NOTIFY_TYPE_MISC);
}

static int mtk_usb_extcon_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_extcon_info *extcon;
	const char *tcpc_name;
	unsigned long flags;
	u8 typec_state;
	int ret;

	extcon = devm_kzalloc(dev, sizeof(*extcon), GFP_KERNEL);
	if (!extcon)
		return -ENOMEM;
	extcon->dev = dev;
	spin_lock_init(&extcon->state_lock);
	INIT_WORK(&extcon->role_work, mtk_usb_extcon_role_work);
	extcon->role = USB_ROLE_NONE;
	extcon->typec_state = TYPEC_UNATTACHED;
	extcon->pd_data_role = PD_ROLE_NONE;
	extcon->bypss_typec_sink =
		of_property_read_bool(dev->of_node, "mediatek,bypss-typec-sink");

	ret = of_property_read_string(dev->of_node, "tcpc", &tcpc_name);
	if (ret)
		return dev_err_probe(dev, ret, "no tcpc property\n");
	extcon->tcpc_dev = tcpc_dev_get_by_name(tcpc_name);
	if (!extcon->tcpc_dev)
		return -EPROBE_DEFER;

	extcon->chg_psy = devm_power_supply_get_by_phandle(dev, "charger");
	if (IS_ERR(extcon->chg_psy))
		return dev_err_probe(dev, PTR_ERR(extcon->chg_psy),
				     "no charger power supply\n");
	if (!extcon->chg_psy)
		return -EPROBE_DEFER;

	extcon->role_sw = usb_role_switch_get(dev);
	if (IS_ERR(extcon->role_sw))
		return dev_err_probe(dev, PTR_ERR(extcon->role_sw),
				     "no usb role switch\n");
	if (!extcon->role_sw)
		return dev_err_probe(dev, -ENODEV, "no usb role switch\n");
	ret = devm_add_action_or_reset(dev, mtk_usb_extcon_put_role_sw,
				       extcon->role_sw);
	if (ret)
		return ret;

	extcon->vbus = devm_regulator_get(dev, "vbus");
	if (IS_ERR(extcon->vbus))
		return dev_err_probe(dev, PTR_ERR(extcon->vbus), "no vbus\n");

	extcon->edev = devm_extcon_dev_allocate(dev, usb_extcon_cable);
	if (IS_ERR(extcon->edev))
		return PTR_ERR(extcon->edev);
	ret = devm_extcon_dev_register(dev, extcon->edev);
	if (ret)
		return dev_err_probe(dev, ret, "cannot register extcon\n");

	extcon->wq = alloc_ordered_workqueue("extcon_usb", 0);
	if (!extcon->wq)
		return -ENOMEM;
	ret = devm_add_action_or_reset(dev, mtk_usb_extcon_destroy_wq,
				       extcon->wq);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, extcon);

	extcon->psy_nb.notifier_call = mtk_usb_extcon_psy_notifier;
	ret = power_supply_reg_notifier(&extcon->psy_nb);
	if (ret)
		return dev_err_probe(dev, ret, "cannot watch power supplies\n");
	ret = devm_add_action_or_reset(dev, mtk_usb_extcon_psy_unreg,
				       &extcon->psy_nb);
	if (ret)
		return ret;

	extcon->tcpc_nb.notifier_call = mtk_usb_extcon_tcpc_notifier;
	ret = register_tcp_dev_notifier(extcon->tcpc_dev, &extcon->tcpc_nb,
					TCP_NOTIFY_TYPE_USB |
					TCP_NOTIFY_TYPE_VBUS |
					TCP_NOTIFY_TYPE_MISC);
	if (ret)
		return dev_err_probe(dev, ret, "cannot watch %s\n", tcpc_name);
	ret = devm_add_action_or_reset(dev, mtk_usb_extcon_tcpc_unreg, extcon);
	if (ret)
		return ret;

	typec_state = tcpm_inquire_typec_attach_state(extcon->tcpc_dev);
	spin_lock_irqsave(&extcon->state_lock, flags);
	if (!extcon->typec_event_seen)
		extcon->typec_state = typec_state;
	spin_unlock_irqrestore(&extcon->state_lock, flags);
	dev_info(dev, "typec state %u at probe\n", typec_state);

	queue_work(extcon->wq, &extcon->role_work);
	return 0;
}

static void mtk_usb_extcon_shutdown(struct platform_device *pdev)
{
	struct mtk_extcon_info *extcon = platform_get_drvdata(pdev);

	if (extcon)
		mtk_usb_extcon_set_vbus(extcon, false);
}

static const struct of_device_id mtk_usb_extcon_of_match[] = {
	{ .compatible = "mediatek,extcon-usb" },
	{}
};
MODULE_DEVICE_TABLE(of, mtk_usb_extcon_of_match);

static struct platform_driver mtk_usb_extcon_driver = {
	.probe = mtk_usb_extcon_probe,
	.shutdown = mtk_usb_extcon_shutdown,
	.driver = {
		.name = "mtk-extcon-usb",
		.of_match_table = mtk_usb_extcon_of_match,
	},
};
module_platform_driver(mtk_usb_extcon_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MediaTek Extcon USB Driver");

// SPDX-License-Identifier: GPL-2.0

#include <linux/extcon-provider.h>
#include <mindone/compat-power-supply.h>
#include <mindone/extcon-mtk-usb.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/property.h>
#include <linux/spinlock.h>
#include <linux/usb/role.h>
#include <linux/usb/typec_altmode.h>
#include <linux/usb/typec_mux.h>
#include <linux/workqueue.h>

struct mtk_extcon_info {
	struct device *dev;
	struct extcon_dev *edev;
	struct usb_role_switch *role_sw;
	struct usb_role_switch *port_sw;
	struct typec_mux_dev *mux;
	struct power_supply *chg_psy;
	struct notifier_block psy_nb;
	struct workqueue_struct *wq;
	struct work_struct role_work;
	spinlock_t state_lock;
	enum usb_role role;
	enum usb_role port_role;
	bool port_audio;
	bool bypss_typec_sink;
};

static DEFINE_MUTEX(mtk_usb_extcon_audio_lock);
static RAW_NOTIFIER_HEAD(mtk_usb_extcon_audio_chain);
static bool mtk_usb_extcon_audio;

static const unsigned int usb_extcon_cable[] = {
	EXTCON_USB,
	EXTCON_USB_HOST,
	EXTCON_NONE,
};

int mtk_usb_extcon_register_audio_notifier(struct notifier_block *nb)
{
	int ret;

	mutex_lock(&mtk_usb_extcon_audio_lock);
	ret = raw_notifier_chain_register(&mtk_usb_extcon_audio_chain, nb);
	if (!ret && mtk_usb_extcon_audio)
		nb->notifier_call(nb, MTK_USB_EXTCON_AUDIO_ATTACH, NULL);
	mutex_unlock(&mtk_usb_extcon_audio_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(mtk_usb_extcon_register_audio_notifier);

int mtk_usb_extcon_unregister_audio_notifier(struct notifier_block *nb)
{
	int ret;

	mutex_lock(&mtk_usb_extcon_audio_lock);
	ret = raw_notifier_chain_unregister(&mtk_usb_extcon_audio_chain, nb);
	mutex_unlock(&mtk_usb_extcon_audio_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(mtk_usb_extcon_unregister_audio_notifier);

static void mtk_usb_extcon_report_audio(bool audio)
{
	mutex_lock(&mtk_usb_extcon_audio_lock);
	if (mtk_usb_extcon_audio != audio) {
		mtk_usb_extcon_audio = audio;
		raw_notifier_call_chain(&mtk_usb_extcon_audio_chain,
					audio ? MTK_USB_EXTCON_AUDIO_ATTACH :
						MTK_USB_EXTCON_AUDIO_DETACH,
					NULL);
	}
	mutex_unlock(&mtk_usb_extcon_audio_lock);
}

static int mtk_usb_extcon_data_port(struct mtk_extcon_info *extcon)
{
	union power_supply_propval val;
	int ret;

	ret = power_supply_get_property(extcon->chg_psy,
					POWER_SUPPLY_PROP_USB_TYPE, &val);
	if (ret)
		return ret;
	return val.intval == POWER_SUPPLY_USB_TYPE_SDP ||
	       val.intval == POWER_SUPPLY_USB_TYPE_CDP;
}

static int mtk_usb_extcon_desired_role(struct mtk_extcon_info *extcon,
				       enum usb_role *role, bool *audio)
{
	unsigned long flags;
	enum usb_role port_role;
	int data;

	spin_lock_irqsave(&extcon->state_lock, flags);
	port_role = extcon->port_role;
	*audio = extcon->port_audio;
	spin_unlock_irqrestore(&extcon->state_lock, flags);

	if (*audio) {
		*role = USB_ROLE_NONE;
		return 0;
	}
	if (port_role == USB_ROLE_HOST) {
		*role = USB_ROLE_HOST;
		return 0;
	}
	if (!extcon->bypss_typec_sink) {
		*role = port_role;
		return 0;
	}
	data = mtk_usb_extcon_data_port(extcon);
	if (data < 0)
		return data;
	*role = data ? USB_ROLE_DEVICE : USB_ROLE_NONE;
	return 0;
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
	bool audio;
	int ret;

	ret = mtk_usb_extcon_desired_role(extcon, &role, &audio);
	mtk_usb_extcon_report_audio(audio);
	if (ret) {
		dev_warn(extcon->dev, "charger type unreadable (%d), role kept\n",
			 ret);
		return;
	}
	mtk_usb_extcon_set_role(extcon, role);
}

static int mtk_usb_extcon_port_set(struct usb_role_switch *sw,
				   enum usb_role role)
{
	struct mtk_extcon_info *extcon = usb_role_switch_get_drvdata(sw);
	unsigned long flags;

	spin_lock_irqsave(&extcon->state_lock, flags);
	extcon->port_role = role;
	spin_unlock_irqrestore(&extcon->state_lock, flags);
	dev_info(extcon->dev, "typec port role %s\n", usb_role_string(role));
	queue_work(extcon->wq, &extcon->role_work);
	return 0;
}

static enum usb_role mtk_usb_extcon_port_get(struct usb_role_switch *sw)
{
	struct mtk_extcon_info *extcon = usb_role_switch_get_drvdata(sw);
	unsigned long flags;
	enum usb_role role;

	spin_lock_irqsave(&extcon->state_lock, flags);
	role = extcon->port_role;
	spin_unlock_irqrestore(&extcon->state_lock, flags);
	return role;
}

static int mtk_usb_extcon_mux_set(struct typec_mux_dev *mux,
				  struct typec_mux_state *state)
{
	struct mtk_extcon_info *extcon = typec_mux_get_drvdata(mux);
	unsigned long flags;
	bool audio;

	if (state->alt)
		return 0;
	audio = state->mode == TYPEC_MODE_AUDIO;
	spin_lock_irqsave(&extcon->state_lock, flags);
	extcon->port_audio = audio;
	spin_unlock_irqrestore(&extcon->state_lock, flags);
	queue_work(extcon->wq, &extcon->role_work);
	return 0;
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

static void mtk_usb_extcon_mux_unreg(void *data)
{
	typec_mux_unregister(data);
}

static void mtk_usb_extcon_port_unreg(void *data)
{
	usb_role_switch_unregister(data);
}

static void mtk_usb_extcon_audio_off(void *data)
{
	mtk_usb_extcon_report_audio(false);
}

static int mtk_usb_extcon_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct usb_role_switch_desc port_desc = { };
	struct typec_mux_desc mux_desc = { };
	struct mtk_extcon_info *extcon;
	int ret;

	extcon = devm_kzalloc(dev, sizeof(*extcon), GFP_KERNEL);
	if (!extcon)
		return -ENOMEM;
	extcon->dev = dev;
	spin_lock_init(&extcon->state_lock);
	INIT_WORK(&extcon->role_work, mtk_usb_extcon_role_work);
	extcon->role = USB_ROLE_NONE;
	extcon->port_role = USB_ROLE_NONE;
	extcon->bypss_typec_sink =
		of_property_present(dev->of_node, "mediatek,bypss-typec-sink");

	extcon->chg_psy = mindone_devm_power_supply_get_by_reference(dev, "charger");
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

	ret = devm_add_action_or_reset(dev, mtk_usb_extcon_audio_off, NULL);
	if (ret)
		return ret;

	mux_desc.fwnode = dev_fwnode(dev);
	mux_desc.set = mtk_usb_extcon_mux_set;
	mux_desc.drvdata = extcon;
	extcon->mux = typec_mux_register(dev, &mux_desc);
	if (IS_ERR(extcon->mux))
		return dev_err_probe(dev, PTR_ERR(extcon->mux),
				     "cannot register typec mux\n");
	ret = devm_add_action_or_reset(dev, mtk_usb_extcon_mux_unreg,
				       extcon->mux);
	if (ret)
		return ret;

	port_desc.fwnode = dev_fwnode(dev);
	port_desc.set = mtk_usb_extcon_port_set;
	port_desc.get = mtk_usb_extcon_port_get;
	port_desc.driver_data = extcon;
	extcon->port_sw = usb_role_switch_register(dev, &port_desc);
	if (IS_ERR(extcon->port_sw))
		return dev_err_probe(dev, PTR_ERR(extcon->port_sw),
				     "cannot register typec role switch\n");
	ret = devm_add_action_or_reset(dev, mtk_usb_extcon_port_unreg,
				       extcon->port_sw);
	if (ret)
		return ret;

	dev_info(dev, "role from typec port and charger type, bypass %d\n",
		 extcon->bypss_typec_sink);
	queue_work(extcon->wq, &extcon->role_work);
	return 0;
}

static const struct of_device_id mtk_usb_extcon_of_match[] = {
	{ .compatible = "mediatek,extcon-usb" },
	{}
};
MODULE_DEVICE_TABLE(of, mtk_usb_extcon_of_match);

static struct platform_driver mtk_usb_extcon_driver = {
	.probe = mtk_usb_extcon_probe,
	.driver = {
		.name = "mtk-extcon-usb",
		.of_match_table = mtk_usb_extcon_of_match,
	},
};
module_platform_driver(mtk_usb_extcon_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MediaTek Extcon USB Driver");

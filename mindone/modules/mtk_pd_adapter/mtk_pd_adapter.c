// SPDX-License-Identifier: GPL-2.0

#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/property.h>
#include <linux/workqueue.h>

#include "adapter_class.h"

#define MTK_PD_ADAPTER_TYPEC_SETTLE_MS	5000

struct mtk_pd_adapter_info {
	struct device *dev;
	struct adapter_device *adapter_dev;
	const char *adapter_dev_name;
	struct fwnode_handle *typec;
	struct power_supply *psy;
	struct notifier_block psy_nb;
	struct work_struct update_work;
	struct delayed_work settle_work;
	struct mutex lock;
	int pd_type;
	int rp_ma;
	int contract_mv;
	int contract_ma;
	bool online;
	bool pd;
};

static bool mtk_pd_adapter_pd_ready(int pd_type)
{
	return pd_type == MTK_PD_CONNECT_PE_READY_SNK ||
	       pd_type == MTK_PD_CONNECT_PE_READY_SNK_PD30 ||
	       pd_type == MTK_PD_CONNECT_PE_READY_SNK_APDO;
}

static int mtk_pd_adapter_rp_ma(int current_ma)
{
	if (current_ma >= 3000)
		return 3000;
	if (current_ma >= 1500)
		return 1500;
	return 500;
}

static void mtk_pd_adapter_notify(struct mtk_pd_adapter_info *info, int event)
{
	if (event != MTK_PD_CONNECT_HARD_RESET)
		WRITE_ONCE(info->pd_type, event);
	else
		WRITE_ONCE(info->pd_type, MTK_PD_CONNECT_NONE);
	dev_info(info->dev, "pd event %d, %d mV %d mA, rp %d mA\n", event,
		 info->contract_mv, info->contract_ma, info->rp_ma);
	srcu_notifier_call_chain(&info->adapter_dev->evt_nh, event, NULL);
}

static int mtk_pd_adapter_read(struct power_supply *psy,
			       enum power_supply_property prop, int *value)
{
	union power_supply_propval val;
	int ret;

	ret = power_supply_get_property(psy, prop, &val);
	if (!ret)
		*value = val.intval;
	return ret;
}

static void mtk_pd_adapter_update(struct work_struct *work)
{
	struct mtk_pd_adapter_info *info =
		container_of(work, struct mtk_pd_adapter_info, update_work);
	int online, usb_type, uv, ua, event, old_mv, old_ma;
	bool pd, pps;

	mutex_lock(&info->lock);
	if (!info->psy) {
		info->psy = power_supply_get_by_reference(dev_fwnode(info->dev),
							  "typec");
		if (IS_ERR_OR_NULL(info->psy)) {
			info->psy = NULL;
			goto out;
		}
	}
	if (mtk_pd_adapter_read(info->psy, POWER_SUPPLY_PROP_ONLINE, &online) ||
	    mtk_pd_adapter_read(info->psy, POWER_SUPPLY_PROP_USB_TYPE, &usb_type) ||
	    mtk_pd_adapter_read(info->psy, POWER_SUPPLY_PROP_VOLTAGE_NOW, &uv) ||
	    mtk_pd_adapter_read(info->psy, POWER_SUPPLY_PROP_CURRENT_MAX, &ua)) {
		online = 0;
		usb_type = POWER_SUPPLY_USB_TYPE_C;
		uv = 0;
		ua = 0;
	}
	pps = usb_type == POWER_SUPPLY_USB_TYPE_PD_PPS ||
	      usb_type == POWER_SUPPLY_USB_TYPE_PD_PPS_SPR_AVS;
	pd = pps || usb_type == POWER_SUPPLY_USB_TYPE_PD ||
	     usb_type == POWER_SUPPLY_USB_TYPE_PD_SPR_AVS;
	old_mv = info->contract_mv;
	old_ma = info->contract_ma;
	WRITE_ONCE(info->online, online > 0);
	WRITE_ONCE(info->pd, pd);
	WRITE_ONCE(info->contract_mv, uv / 1000);
	WRITE_ONCE(info->contract_ma, ua / 1000);

	if (online <= 0) {
		cancel_delayed_work(&info->settle_work);
		WRITE_ONCE(info->rp_ma, 0);
		if (pd && mtk_pd_adapter_pd_ready(info->pd_type))
			event = MTK_PD_CONNECT_HARD_RESET;
		else if (info->pd_type != MTK_PD_CONNECT_NONE)
			event = MTK_PD_CONNECT_NONE;
		else
			goto out;
		mtk_pd_adapter_notify(info, event);
		goto out;
	}

	if (!pd) {
		WRITE_ONCE(info->rp_ma, mtk_pd_adapter_rp_ma(ua / 1000));
		if (mtk_pd_adapter_pd_ready(info->pd_type)) {
			mtk_pd_adapter_notify(info, MTK_PD_CONNECT_NONE);
			schedule_delayed_work(&info->settle_work,
				msecs_to_jiffies(MTK_PD_ADAPTER_TYPEC_SETTLE_MS));
		} else if (info->pd_type == MTK_PD_CONNECT_NONE &&
			   !delayed_work_pending(&info->settle_work)) {
			schedule_delayed_work(&info->settle_work,
				msecs_to_jiffies(MTK_PD_ADAPTER_TYPEC_SETTLE_MS));
		}
		goto out;
	}

	cancel_delayed_work(&info->settle_work);
	if (!info->rp_ma)
		WRITE_ONCE(info->rp_ma, 500);
	event = pps ? MTK_PD_CONNECT_PE_READY_SNK_APDO :
		      MTK_PD_CONNECT_PE_READY_SNK;
	if (event != info->pd_type || info->contract_mv != old_mv ||
	    info->contract_ma != old_ma)
		mtk_pd_adapter_notify(info, event);
out:
	mutex_unlock(&info->lock);
}

static void mtk_pd_adapter_settle(struct work_struct *work)
{
	struct mtk_pd_adapter_info *info =
		container_of(to_delayed_work(work), struct mtk_pd_adapter_info,
			     settle_work);

	mutex_lock(&info->lock);
	if (info->online && !info->pd &&
	    info->pd_type != MTK_PD_CONNECT_TYPEC_ONLY_SNK)
		mtk_pd_adapter_notify(info, MTK_PD_CONNECT_TYPEC_ONLY_SNK);
	mutex_unlock(&info->lock);
}

static int mtk_pd_adapter_psy_notifier(struct notifier_block *nb,
				       unsigned long event, void *data)
{
	struct mtk_pd_adapter_info *info =
		container_of(nb, struct mtk_pd_adapter_info, psy_nb);
	struct power_supply *psy = data;

	if (event != PSY_EVENT_PROP_CHANGED)
		return NOTIFY_DONE;
	if (psy == READ_ONCE(info->psy) ||
	    (psy->dev.parent && dev_fwnode(psy->dev.parent) == info->typec))
		schedule_work(&info->update_work);
	return NOTIFY_DONE;
}

static int pd_get_property(struct adapter_device *dev,
			   enum adapter_property prop)
{
	struct mtk_pd_adapter_info *info = adapter_dev_get_drvdata(dev);

	if (!info)
		return -1;
	switch (prop) {
	case TYPEC_RP_LEVEL:
		return READ_ONCE(info->online) ? READ_ONCE(info->rp_ma) : 0;
	case PD_TYPE:
		return READ_ONCE(info->pd_type);
	default:
		return -1;
	}
}

static int pd_get_cap(struct adapter_device *dev, enum adapter_cap_type type,
		      struct adapter_power_cap *cap)
{
	struct mtk_pd_adapter_info *info = adapter_dev_get_drvdata(dev);
	int mv, ma;

	cap->nr = 0;
	cap->selected_cap_idx = 0;
	cap->pdp = 0;
	if (!info || type != MTK_PD ||
	    !mtk_pd_adapter_pd_ready(READ_ONCE(info->pd_type)))
		return MTK_ADAPTER_OK;
	mv = READ_ONCE(info->contract_mv);
	ma = READ_ONCE(info->contract_ma);
	if (mv <= 0 || ma <= 0)
		return MTK_ADAPTER_OK;
	cap->nr = 1;
	cap->type[0] = MTK_PD;
	cap->pwr_limit[0] = 0;
	cap->max_mv[0] = mv;
	cap->min_mv[0] = mv;
	cap->ma[0] = ma;
	cap->maxwatt[0] = mv * ma;
	cap->minwatt[0] = mv * ma;
	cap->info[0] = 0;
	return MTK_ADAPTER_OK;
}

static int pd_set_cap(struct adapter_device *dev, enum adapter_cap_type type,
		      int mV, int mA)
{
	struct mtk_pd_adapter_info *info = adapter_dev_get_drvdata(dev);

	if (!info || type != MTK_PD)
		return MTK_ADAPTER_NOT_SUPPORT;
	if (!mtk_pd_adapter_pd_ready(READ_ONCE(info->pd_type)))
		return MTK_ADAPTER_ERROR;
	if (mV == READ_ONCE(info->contract_mv) && mA > 0 &&
	    mA <= READ_ONCE(info->contract_ma))
		return MTK_ADAPTER_OK;
	return MTK_ADAPTER_NOT_SUPPORT;
}

static const struct adapter_ops adapter_ops = {
	.get_property = pd_get_property,
	.get_cap = pd_get_cap,
	.set_cap = pd_set_cap,
};

static int mtk_pd_adapter_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_pd_adapter_info *info;
	int ret;

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	info->dev = dev;
	info->pd_type = MTK_PD_CONNECT_NONE;
	mutex_init(&info->lock);
	INIT_WORK(&info->update_work, mtk_pd_adapter_update);
	INIT_DELAYED_WORK(&info->settle_work, mtk_pd_adapter_settle);

	if (device_property_read_string(dev, "adapter_name",
					&info->adapter_dev_name))
		info->adapter_dev_name = "pd_adapter";
	info->typec = fwnode_find_reference(dev_fwnode(dev), "typec", 0);
	if (IS_ERR(info->typec))
		return dev_err_probe(dev, PTR_ERR(info->typec),
				     "no typec port reference\n");

	info->adapter_dev = adapter_device_register(info->adapter_dev_name,
						    dev, info, &adapter_ops,
						    NULL);
	if (IS_ERR_OR_NULL(info->adapter_dev)) {
		ret = info->adapter_dev ? PTR_ERR(info->adapter_dev) : -ENODEV;
		fwnode_handle_put(info->typec);
		return ret;
	}
	adapter_dev_set_drvdata(info->adapter_dev, info);
	platform_set_drvdata(pdev, info);

	info->psy_nb.notifier_call = mtk_pd_adapter_psy_notifier;
	ret = power_supply_reg_notifier(&info->psy_nb);
	if (ret) {
		adapter_device_unregister(info->adapter_dev);
		fwnode_handle_put(info->typec);
		return ret;
	}
	schedule_work(&info->update_work);
	return 0;
}

static void mtk_pd_adapter_remove(struct platform_device *pdev)
{
	struct mtk_pd_adapter_info *info = platform_get_drvdata(pdev);

	power_supply_unreg_notifier(&info->psy_nb);
	cancel_work_sync(&info->update_work);
	cancel_delayed_work_sync(&info->settle_work);
	adapter_device_unregister(info->adapter_dev);
	if (info->psy)
		power_supply_put(info->psy);
	fwnode_handle_put(info->typec);
}

static const struct of_device_id mtk_pd_adapter_of_match[] = {
	{ .compatible = "mediatek,pd_adapter" },
	{}
};
MODULE_DEVICE_TABLE(of, mtk_pd_adapter_of_match);

static struct platform_driver mtk_pd_adapter_driver = {
	.probe = mtk_pd_adapter_probe,
	.remove = mtk_pd_adapter_remove,
	.driver = {
		.name = "pd_adapter",
		.of_match_table = mtk_pd_adapter_of_match,
	},
};
module_platform_driver(mtk_pd_adapter_driver);

MODULE_DESCRIPTION("PD adapter of the MediaTek charger framework on the TCPM power supply");
MODULE_LICENSE("GPL");

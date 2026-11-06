// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeup.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#define NFC_MAGIC			0xE9
#define NFC_SET_PWR			_IOW(NFC_MAGIC, 0x01, uint32_t)
#define ESE_SET_PWR			_IOW(NFC_MAGIC, 0x02, uint32_t)
#define ESE_GET_PWR			_IOR(NFC_MAGIC, 0x03, uint32_t)
#define NFC_SET_RESET_READ_PENDING	_IOW(NFC_MAGIC, 0x04, uint32_t)
#define NFC_GET_GPIO_STATUS		_IOR(NFC_MAGIC, 0x05, uint32_t)

#define MAX_NCI_BUFFER_SIZE		(3 + 255)
#define MAX_DL_BUFFER_SIZE		(2 + 2 + 4096)
#define MAX_WRITE_IRQ_COUNT		5
#define WAKEUP_SRC_TIMEOUT_MS		100
#define GPIO_SET_WAIT_US		10000
#define WRITE_RETRY_WAIT_US		3000
#define MAX_WRITE_RETRY_COUNT		3
#define GPIO_STATUS_BITS		2
#define GPIO_STATUS_MASK		3
#define GPIO_STATUS_INVALID		(-2)

enum nfc_pwr_request {
	NFC_POWER_OFF = 0,
	NFC_POWER_ON,
	NFC_FW_DWL_VEN_TOGGLE,
	NFC_ISO_RESET,
	NFC_FW_DWL_HIGH,
	NFC_VEN_FORCED_HARD_RESET,
	NFC_FW_DWL_LOW,
};

enum ese_pwr_request {
	ESE_POWER_ON = 0,
	ESE_POWER_OFF,
	ESE_POWER_STATE,
};

enum nfc_read_pending {
	NFC_RESET_READ_PENDING = 0,
	NFC_SET_READ_PENDING,
};

struct nfc_pins {
	struct pinctrl *pinctrl;
	struct pinctrl_state *ven_high;
	struct pinctrl_state *ven_low;
	struct pinctrl_state *dwl_high;
	struct pinctrl_state *dwl_low;
	struct pinctrl_state *irq_init;
	struct gpio_desc *irq_gpio;
};

struct nfc_dev {
	struct i2c_client *client;
	struct nfc_pins *pins;
	struct miscdevice misc;
	wait_queue_head_t read_wq;
	struct mutex read_mutex;
	struct mutex write_mutex;
	struct mutex dev_ref_mutex;
	unsigned int dev_ref_count;
	spinlock_t irq_enabled_lock;
	bool irq_enabled;
	bool irq_wake_up;
	bool release_read;
	bool ven_high;
	bool dwl_high;
	bool ven_on_by_nfc;
	u8 *read_kbuf;
	u8 *write_kbuf;
};

static int nfc_irq_level(struct nfc_dev *nfc)
{
	return gpiod_get_value(nfc->pins->irq_gpio);
}

static void nfc_set_ven(struct nfc_dev *nfc, bool high)
{
	if (nfc->ven_high == high)
		return;
	pinctrl_select_state(nfc->pins->pinctrl,
			     high ? nfc->pins->ven_high : nfc->pins->ven_low);
	nfc->ven_high = high;
	usleep_range(GPIO_SET_WAIT_US, GPIO_SET_WAIT_US + 100);
}

static void nfc_set_dwl(struct nfc_dev *nfc, bool high)
{
	pinctrl_select_state(nfc->pins->pinctrl,
			     high ? nfc->pins->dwl_high : nfc->pins->dwl_low);
	nfc->dwl_high = high;
	usleep_range(GPIO_SET_WAIT_US, GPIO_SET_WAIT_US + 100);
}

static void nfc_disable_irq(struct nfc_dev *nfc)
{
	unsigned long flags;

	spin_lock_irqsave(&nfc->irq_enabled_lock, flags);
	if (nfc->irq_enabled) {
		disable_irq_nosync(nfc->client->irq);
		nfc->irq_enabled = false;
	}
	spin_unlock_irqrestore(&nfc->irq_enabled_lock, flags);
}

static void nfc_enable_irq(struct nfc_dev *nfc)
{
	unsigned long flags;

	spin_lock_irqsave(&nfc->irq_enabled_lock, flags);
	if (!nfc->irq_enabled) {
		nfc->irq_enabled = true;
		enable_irq(nfc->client->irq);
	}
	spin_unlock_irqrestore(&nfc->irq_enabled_lock, flags);
}

static irqreturn_t nfc_irq_handler(int irq, void *dev_id)
{
	struct nfc_dev *nfc = dev_id;

	if (device_may_wakeup(&nfc->client->dev))
		pm_wakeup_event(&nfc->client->dev, WAKEUP_SRC_TIMEOUT_MS);
	nfc_disable_irq(nfc);
	wake_up(&nfc->read_wq);
	return IRQ_HANDLED;
}

static int nfc_wait_irq(struct nfc_dev *nfc)
{
	int ret;

	while (!nfc_irq_level(nfc)) {
		nfc_enable_irq(nfc);
		if (!nfc_irq_level(nfc)) {
			ret = wait_event_interruptible(nfc->read_wq,
						       !nfc->irq_enabled);
			if (ret)
				return ret;
		}
		nfc_disable_irq(nfc);
		if (nfc_irq_level(nfc))
			break;
		if (!nfc->ven_high)
			return -EIO;
		if (nfc->release_read)
			return 1;
		dev_warn_ratelimited(&nfc->client->dev, "spurious interrupt\n");
	}
	return 0;
}

static ssize_t nfc_read(struct file *filp, char __user *buf, size_t count,
			loff_t *offset)
{
	struct nfc_dev *nfc = filp->private_data;
	int ret;

	if (count > MAX_NCI_BUFFER_SIZE)
		count = MAX_NCI_BUFFER_SIZE;

	mutex_lock(&nfc->read_mutex);
	if (!(filp->f_flags & O_NONBLOCK)) {
		ret = nfc_wait_irq(nfc);
		if (ret) {
			mutex_unlock(&nfc->read_mutex);
			return ret > 0 ? 0 : ret;
		}
	}
	memset(nfc->read_kbuf, 0, count);
	ret = i2c_master_recv(nfc->client, nfc->read_kbuf, count);
	if (ret > 0 && copy_to_user(buf, nfc->read_kbuf, ret))
		ret = -EFAULT;
	mutex_unlock(&nfc->read_mutex);
	return ret;
}

static ssize_t nfc_write(struct file *filp, const char __user *buf,
			 size_t count, loff_t *offset)
{
	struct nfc_dev *nfc = filp->private_data;
	int ret;
	int i;

	if (count > MAX_DL_BUFFER_SIZE)
		count = MAX_DL_BUFFER_SIZE;

	mutex_lock(&nfc->write_mutex);
	if (copy_from_user(nfc->write_kbuf, buf, count)) {
		mutex_unlock(&nfc->write_mutex);
		return -EFAULT;
	}
	for (i = 0; i < MAX_WRITE_IRQ_COUNT && nfc_irq_level(nfc); i++)
		usleep_range(WRITE_RETRY_WAIT_US, WRITE_RETRY_WAIT_US + 100);
	for (i = 1; i <= MAX_WRITE_RETRY_COUNT; i++) {
		ret = i2c_master_send(nfc->client, nfc->write_kbuf, count);
		if (ret > 0 || i == MAX_WRITE_RETRY_COUNT)
			break;
		usleep_range(WRITE_RETRY_WAIT_US, WRITE_RETRY_WAIT_US + 100);
	}
	if (ret >= 0 && ret != count)
		ret = -EIO;
	mutex_unlock(&nfc->write_mutex);
	return ret;
}

static int nfc_power(struct nfc_dev *nfc, unsigned long arg)
{
	switch (arg) {
	case NFC_POWER_OFF:
		nfc_disable_irq(nfc);
		nfc_set_dwl(nfc, false);
		nfc_set_ven(nfc, false);
		nfc->ven_on_by_nfc = false;
		return 0;
	case NFC_POWER_ON:
		nfc_enable_irq(nfc);
		nfc_set_dwl(nfc, false);
		nfc_set_ven(nfc, true);
		nfc->ven_on_by_nfc = true;
		return 0;
	case NFC_FW_DWL_VEN_TOGGLE:
		nfc_disable_irq(nfc);
		nfc_set_dwl(nfc, true);
		nfc_set_ven(nfc, false);
		nfc_set_ven(nfc, true);
		nfc_enable_irq(nfc);
		return 0;
	case NFC_FW_DWL_HIGH:
		nfc_set_dwl(nfc, true);
		return 0;
	case NFC_VEN_FORCED_HARD_RESET:
		nfc_disable_irq(nfc);
		nfc_set_ven(nfc, false);
		nfc_set_ven(nfc, true);
		nfc_enable_irq(nfc);
		return 0;
	case NFC_FW_DWL_LOW:
		nfc_set_dwl(nfc, false);
		return 0;
	default:
		return -EINVAL;
	}
}

static int nfc_ese_power(struct nfc_dev *nfc, unsigned long arg)
{
	switch (arg) {
	case ESE_POWER_ON:
		nfc->ven_on_by_nfc = nfc->ven_high;
		nfc_set_ven(nfc, true);
		return 0;
	case ESE_POWER_OFF:
		if (!nfc->ven_on_by_nfc)
			nfc_set_ven(nfc, false);
		return 0;
	case ESE_POWER_STATE:
		return nfc->ven_high;
	default:
		return -EINVAL;
	}
}

static int nfc_gpio_status(struct nfc_dev *nfc, u32 __user *arg)
{
	int levels[] = { nfc_irq_level(nfc), nfc->ven_high, nfc->dwl_high };
	u32 status = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(levels); i++) {
		int v = levels[i] < 0 ? GPIO_STATUS_INVALID : levels[i];

		status |= (v & GPIO_STATUS_MASK) << (GPIO_STATUS_BITS * i);
	}
	return put_user(status, arg);
}

static long nfc_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct nfc_dev *nfc = filp->private_data;

	switch (cmd) {
	case NFC_SET_PWR:
		return nfc_power(nfc, arg);
	case NFC_SET_RESET_READ_PENDING:
		return (arg == NFC_SET_READ_PENDING ||
			arg == NFC_RESET_READ_PENDING) ? 0 : -EINVAL;
	case ESE_SET_PWR:
		return nfc_ese_power(nfc, arg);
	case ESE_GET_PWR:
		return nfc_ese_power(nfc, ESE_POWER_STATE);
	case NFC_GET_GPIO_STATUS:
		return nfc_gpio_status(nfc, (u32 __user *)arg);
	default:
		return -ENOTTY;
	}
}

static int nfc_open(struct inode *inode, struct file *filp)
{
	struct nfc_dev *nfc = container_of(filp->private_data, struct nfc_dev,
					   misc);

	filp->private_data = nfc;
	mutex_lock(&nfc->dev_ref_mutex);
	if (nfc->dev_ref_count == 0) {
		nfc_set_dwl(nfc, false);
		nfc_enable_irq(nfc);
	}
	nfc->dev_ref_count++;
	mutex_unlock(&nfc->dev_ref_mutex);
	return 0;
}

static int nfc_flush(struct file *filp, fl_owner_t id)
{
	struct nfc_dev *nfc = filp->private_data;

	if (!mutex_trylock(&nfc->read_mutex)) {
		nfc->release_read = true;
		nfc_disable_irq(nfc);
		wake_up(&nfc->read_wq);
		mutex_lock(&nfc->read_mutex);
		nfc->release_read = false;
	}
	mutex_unlock(&nfc->read_mutex);
	return 0;
}

static int nfc_release(struct inode *inode, struct file *filp)
{
	struct nfc_dev *nfc = filp->private_data;

	mutex_lock(&nfc->dev_ref_mutex);
	if (nfc->dev_ref_count == 1) {
		nfc_disable_irq(nfc);
		nfc_set_dwl(nfc, false);
	}
	if (nfc->dev_ref_count > 0)
		nfc->dev_ref_count--;
	mutex_unlock(&nfc->dev_ref_mutex);
	return 0;
}

static const struct file_operations nfc_fops = {
	.owner = THIS_MODULE,
	.read = nfc_read,
	.write = nfc_write,
	.open = nfc_open,
	.flush = nfc_flush,
	.release = nfc_release,
	.unlocked_ioctl = nfc_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
};

static void nfc_put_device(void *dev)
{
	put_device(dev);
}

static struct nfc_pins *nfc_get_pins(struct device *dev)
{
	struct platform_device *pdev;
	struct device_node *np;
	struct nfc_pins *pins;
	int ret;

	np = of_find_compatible_node(NULL, NULL, "mediatek,nfc-gpio-v2");
	if (!np)
		return ERR_PTR(dev_err_probe(dev, -ENODEV,
					     "no mediatek,nfc-gpio-v2 node\n"));
	pdev = of_find_device_by_node(np);
	of_node_put(np);
	if (!pdev)
		return ERR_PTR(-EPROBE_DEFER);
	ret = devm_add_action_or_reset(dev, nfc_put_device, &pdev->dev);
	if (ret)
		return ERR_PTR(ret);
	pins = platform_get_drvdata(pdev);
	if (!pins)
		return ERR_PTR(-EPROBE_DEFER);
	if (!device_link_add(dev, &pdev->dev, DL_FLAG_AUTOREMOVE_CONSUMER))
		return ERR_PTR(dev_err_probe(dev, -EINVAL, "cannot link to %s\n",
					     dev_name(&pdev->dev)));
	return pins;
}

static int nfc_i2c_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct nfc_dev *nfc;
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return dev_err_probe(dev, -ENODEV, "adapter lacks I2C_FUNC_I2C\n");

	nfc = devm_kzalloc(dev, sizeof(*nfc), GFP_KERNEL);
	if (!nfc)
		return -ENOMEM;
	nfc->pins = nfc_get_pins(dev);
	if (IS_ERR(nfc->pins))
		return PTR_ERR(nfc->pins);
	nfc->read_kbuf = devm_kzalloc(dev, MAX_NCI_BUFFER_SIZE, GFP_KERNEL);
	nfc->write_kbuf = devm_kzalloc(dev, MAX_DL_BUFFER_SIZE, GFP_KERNEL);
	if (!nfc->read_kbuf || !nfc->write_kbuf)
		return -ENOMEM;

	nfc->client = client;
	init_waitqueue_head(&nfc->read_wq);
	mutex_init(&nfc->read_mutex);
	mutex_init(&nfc->write_mutex);
	mutex_init(&nfc->dev_ref_mutex);
	spin_lock_init(&nfc->irq_enabled_lock);

	ret = gpiod_to_irq(nfc->pins->irq_gpio);
	if (ret < 0)
		return dev_err_probe(dev, ret, "irq gpio has no interrupt\n");
	client->irq = ret;
	nfc->irq_enabled = true;
	ret = devm_request_irq(dev, client->irq, nfc_irq_handler,
			       IRQF_TRIGGER_HIGH, client->name, nfc);
	if (ret)
		return dev_err_probe(dev, ret, "cannot request irq %d\n",
				     client->irq);
	nfc_disable_irq(nfc);

	nfc_set_ven(nfc, true);
	nfc_set_ven(nfc, false);
	nfc_set_dwl(nfc, false);

	i2c_set_clientdata(client, nfc);
	device_init_wakeup(dev, true);

	nfc->misc.minor = MISC_DYNAMIC_MINOR;
	nfc->misc.name = "nxp-nci";
	nfc->misc.fops = &nfc_fops;
	nfc->misc.parent = dev;
	ret = misc_register(&nfc->misc);
	if (ret) {
		device_init_wakeup(dev, false);
		return dev_err_probe(dev, ret, "cannot register nxp-nci\n");
	}
	dev_info(dev, "NFC controller on irq %d\n", client->irq);
	return 0;
}

static void nfc_i2c_remove(struct i2c_client *client)
{
	struct nfc_dev *nfc = i2c_get_clientdata(client);

	misc_deregister(&nfc->misc);
	device_init_wakeup(&client->dev, false);
	nfc_disable_irq(nfc);
	nfc_set_ven(nfc, false);
}

static int nfc_i2c_suspend(struct device *dev)
{
	struct nfc_dev *nfc = dev_get_drvdata(dev);

	if (device_may_wakeup(dev) && nfc->irq_enabled &&
	    !enable_irq_wake(nfc->client->irq))
		nfc->irq_wake_up = true;
	return 0;
}

static int nfc_i2c_resume(struct device *dev)
{
	struct nfc_dev *nfc = dev_get_drvdata(dev);

	if (nfc->irq_wake_up && !disable_irq_wake(nfc->client->irq))
		nfc->irq_wake_up = false;
	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(nfc_i2c_pm_ops, nfc_i2c_suspend,
				nfc_i2c_resume);

static const struct of_device_id nfc_i2c_of_match[] = {
	{ .compatible = "mediatek,nfc" },
	{}
};
MODULE_DEVICE_TABLE(of, nfc_i2c_of_match);

static const struct i2c_device_id nfc_i2c_id[] = {
	{ "nxp-nci" },
	{}
};
MODULE_DEVICE_TABLE(i2c, nfc_i2c_id);

static struct i2c_driver nfc_i2c_driver = {
	.probe = nfc_i2c_probe,
	.remove = nfc_i2c_remove,
	.id_table = nfc_i2c_id,
	.driver = {
		.name = "nxp-nci",
		.of_match_table = nfc_i2c_of_match,
		.pm = pm_sleep_ptr(&nfc_i2c_pm_ops),
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
};

static int nfc_pins_state(struct device *dev, struct nfc_pins *pins,
			  const char *name, struct pinctrl_state **state)
{
	*state = pinctrl_lookup_state(pins->pinctrl, name);
	if (IS_ERR(*state))
		return dev_err_probe(dev, PTR_ERR(*state),
				     "no pinctrl state %s\n", name);
	return 0;
}

static int nfc_pins_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct nfc_pins *pins;
	int gpio;
	int ret;

	pins = devm_kzalloc(dev, sizeof(*pins), GFP_KERNEL);
	if (!pins)
		return -ENOMEM;
	pins->pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR(pins->pinctrl))
		return dev_err_probe(dev, PTR_ERR(pins->pinctrl), "no pinctrl\n");
	ret = nfc_pins_state(dev, pins, "ven_high", &pins->ven_high) ?:
	      nfc_pins_state(dev, pins, "ven_low", &pins->ven_low) ?:
	      nfc_pins_state(dev, pins, "dwn_high", &pins->dwl_high) ?:
	      nfc_pins_state(dev, pins, "dwn_low", &pins->dwl_low) ?:
	      nfc_pins_state(dev, pins, "irq_init", &pins->irq_init);
	if (ret)
		return ret;
	ret = pinctrl_select_state(pins->pinctrl, pins->ven_low) ?:
	      pinctrl_select_state(pins->pinctrl, pins->dwl_low) ?:
	      pinctrl_select_state(pins->pinctrl, pins->irq_init);
	if (ret)
		return dev_err_probe(dev, ret, "cannot set initial pin states\n");

	gpio = of_get_named_gpio(dev->of_node, "gpio-irq-std", 0);
	if (gpio < 0)
		return dev_err_probe(dev, gpio, "no gpio-irq-std\n");
	ret = devm_gpio_request_one(dev, gpio, GPIOF_IN, "nfc_irq");
	if (ret)
		return dev_err_probe(dev, ret, "cannot request gpio %d\n", gpio);
	pins->irq_gpio = gpio_to_desc(gpio);

	platform_set_drvdata(pdev, pins);
	return 0;
}

static const struct of_device_id nfc_pins_of_match[] = {
	{ .compatible = "mediatek,nfc-gpio-v2" },
	{}
};
MODULE_DEVICE_TABLE(of, nfc_pins_of_match);

static struct platform_driver nfc_pins_driver = {
	.probe = nfc_pins_probe,
	.driver = {
		.name = "nxp-nci-pins",
		.of_match_table = nfc_pins_of_match,
	},
};

static int __init nfc_init(void)
{
	int ret;

	ret = platform_driver_register(&nfc_pins_driver);
	if (ret)
		return ret;
	ret = i2c_add_driver(&nfc_i2c_driver);
	if (ret)
		platform_driver_unregister(&nfc_pins_driver);
	return ret;
}
module_init(nfc_init);

static void __exit nfc_exit(void)
{
	i2c_del_driver(&nfc_i2c_driver);
	platform_driver_unregister(&nfc_pins_driver);
}
module_exit(nfc_exit);

MODULE_DESCRIPTION("NXP PN557 NCI controller on I2C, interface of the NXP NFC HAL");
MODULE_AUTHOR("NXP Semiconductors");
MODULE_LICENSE("GPL");

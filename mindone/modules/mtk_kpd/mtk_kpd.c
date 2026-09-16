// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 MediaTek Inc.
 * Author Terry Chang <terry.chang@mediatek.com>
 */
#include <linux/clk.h>
#include <mindone/compat.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioctl.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeup.h>
#include <linux/regmap.h>

#define KPD_NAME	"mtk-kpd"

#define KP_STA			(0x0000)
#define KP_MEM1			(0x0004)
#define KP_MEM2			(0x0008)
#define KP_MEM3			(0x000c)
#define KP_MEM4			(0x0010)
#define KP_MEM5			(0x0014)
#define KP_DEBOUNCE		(0x0018)
#define KP_SEL			(0X0020)
#define KP_EN			(0x0024)

#define KPD_DEBOUNCE_MASK	((1U << 14) - 1)
#define KPD_DOUBLE_KEY_MASK	(1U << 0)

#define KPD_NUM_MEMS	5
#define KPD_MEM5_BITS	8
#define KPD_NUM_KEYS	72	/* 4 * 16 + KPD_MEM5_BITS */

struct mtk_keypad {
	struct input_dev *input_dev;
	struct wakeup_source *suspend_lock;
	struct tasklet_struct tasklet;
	struct clk *clk;
	void __iomem *base;
	unsigned int irqnr;
	u32 key_debounce;
	u32 use_extend_type;
	u32 hw_map_num;
	u32 hw_init_map[KPD_NUM_KEYS];
	u16 keymap_state[KPD_NUM_MEMS];
};

/*
 * MINDONE-HALL: camera flip-module position switch, absent from this port
 * (F3297); reconstructed from the factory mtk-kpd.ko binary (disassembly of
 * hall_eint_register/hall_eint_handler) since no source carries it. Factory
 * reports the two edges as EV_KEY KEY_F2/KEY_F3 press+release (scancodes
 * 60/61), which is what userspace already listens for to fire the
 * camera-flip broadcast.
 */
static int hall_irq;
static unsigned int hall_irq_dt_type;
static int hall_gpio = -1;
static int hall_state;
static bool hall_irq_disabled;

static irqreturn_t hall_eint_handler(int irq, void *dev_id)
{
	struct input_dev *input = dev_id;
	int val;

	if (!hall_irq_disabled) {
		disable_irq_nosync(irq);
		hall_irq_disabled = true;
	}

	val = gpio_get_value(hall_gpio);

	if (hall_state == 0 && val == 0) {
		irq_set_irq_type(irq, IRQ_TYPE_EDGE_RISING);
		hall_state = 1;
		input_report_key(input, KEY_F2, 1);
		input_sync(input);
		input_report_key(input, KEY_F2, 0);
		input_sync(input);
	} else if (hall_state == 1 && val == 1) {
		irq_set_irq_type(irq, IRQ_TYPE_EDGE_FALLING);
		hall_state = 0;
		input_report_key(input, KEY_F3, 1);
		input_sync(input);
		input_report_key(input, KEY_F3, 0);
		input_sync(input);
	}

	pr_info("hall gpio level=%d\n", val);

	if (hall_irq_disabled) {
		enable_irq(irq);
		hall_irq_disabled = false;
	}

	return IRQ_HANDLED;
}

static void hall_eint_register(struct input_dev *input)
{
	struct device_node *node;
	u32 debounce = 0;
	int ret;
	int val;

	node = of_find_compatible_node(NULL, NULL, "mediatek,hall");
	if (!node) {
		pr_err("%s: no mediatek,hall node\n", __func__);
		return;
	}

	hall_irq = irq_of_parse_and_map(node, 0);
	if (!hall_irq) {
		pr_err("%s: hall irq not available\n", __func__);
		return;
	}
	/* The edge handler retargets the trigger below; remember the DT type so
	 * unregister can restore it, otherwise a module reload fails to map the
	 * same hwirq ("irq: type mismatch, failed to map hwirq-7 for pinctrl").
	 */
	hall_irq_dt_type = irq_get_trigger_type(hall_irq);

	__set_bit(KEY_F2, input->keybit);
	__set_bit(KEY_F3, input->keybit);

	ret = request_threaded_irq(hall_irq, NULL, hall_eint_handler,
			IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
			"hall-eint", input);
	if (ret < 0) {
		pr_err("%s: unable to request hall irq[%d]\n", __func__, ret);
		return;
	}

	ret = irq_set_irq_wake(hall_irq, 1);
	if (ret < 0)
		pr_err("%s: irq %d enable irq wake fail\n", __func__, hall_irq);

	hall_gpio = of_get_named_gpio(node, "cfg-pin", 0);
	if (hall_gpio < 0) {
		pr_err("%s: cfg-pin gpio not provided\n", __func__);
		return;
	}

	if (of_property_read_u32(node, "debounce", &debounce))
		debounce = 0;

	ret = gpio_request(hall_gpio, "hall gpio");
	if (ret < 0) {
		pr_err("%s: unable to request hall gpio[%d]\n", __func__, hall_gpio);
		return;
	}
	MINDONE_GPIO_SET_DEBOUNCE(hall_gpio, debounce);

	val = gpio_get_value(hall_gpio);
	if (val == 0) {
		hall_state = 1;
		irq_set_irq_type(hall_irq, IRQ_TYPE_EDGE_RISING);
	} else {
		hall_state = 0;
		irq_set_irq_type(hall_irq, IRQ_TYPE_EDGE_FALLING);
	}
}

static void hall_eint_unregister(struct input_dev *input)
{
	if (hall_irq) {
		free_irq(hall_irq, input);
		irq_set_irq_type(hall_irq, hall_irq_dt_type);
	}
	if (hall_gpio >= 0)
		gpio_free(hall_gpio);
}

/*
 * MINDONE-HALL: current position as the sysfs attribute "hall_position"
 * on the keypad platform device (/sys/devices/platform/soc/10010000.kp/
 * hall_position; the userspace key handler reads it once at boot). KEY_F2/KEY_F3 only mark the edges, so userspace that
 * starts after boot has no way to learn which side the camera module is
 * on until the first flip; this attribute closes that gap. Value is the
 * same state variable the edge handler keeps: 1 = GPIO low (the KEY_F2
 * side), 0 = GPIO high (the KEY_F3 side), -1 = hall switch not registered.
 * Which side is "front" is decided in userspace, in one place, together
 * with the KEY_F2 mapping.
 */
static ssize_t hall_position_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	int state = hall_gpio >= 0 ? hall_state : -1;

	return scnprintf(buf, PAGE_SIZE, "%d\n", state);
}
static DEVICE_ATTR_RO(hall_position);

/*
 * MINDONE-TOUCHKEY: capacitive side key ("mediatek,touch_key": eint on cfg-pin,
 * power on pwr-pin), absent from this port (F3476); reconstructed from the factory
 * mtk-kpd.ko (touch_key_eint_register/handler, kpd_pdrv_suspend/resume). Factory
 * reports EV_KEY KEY_F4 (scancode 62) held while touched on the same "mtk-kpd"
 * input device, powers the key IC down across suspend, no wake source.
 */
/*
 * F4430 (14.09): the key IC's output flaps in sub-millisecond pulses whenever a
 * hand rests on the side of the phone (223k EINTs in 18 h, 97 % of them read
 * back the unchanged level), and nothing in this ROM consumes KEY_F4. Keep the
 * IC powered down unless asked for; when enabled, sample after the pulse and
 * report only real level changes.
 */
static bool touch_key_enable;
module_param(touch_key_enable, bool, 0444);
MODULE_PARM_DESC(touch_key_enable, "power the capacitive side key IC and report it as KEY_F4 (default: off)");

static int touch_key_irq;
static unsigned int touch_key_irq_dt_type;
static int touch_key_gpio = -1;
static int touch_key_pwr_gpio = -1;
static bool touch_key_state;
static bool touch_key_irq_disabled;

static irqreturn_t touch_key_eint_handler(int irq, void *dev_id)
{
	struct input_dev *input = dev_id;
	int val;

	/* Let the pulse pass, then read the settled level (F4430). */
	usleep_range(30000, 35000);
	val = gpio_get_value(touch_key_gpio);

	if (val == 0 && !touch_key_state) {
		touch_key_state = true;
		input_report_key(input, KEY_F4, 1);
		input_sync(input);
	} else if (val == 1 && touch_key_state) {
		touch_key_state = false;
		input_report_key(input, KEY_F4, 0);
		input_sync(input);
	}
	pr_debug("touch_key_eint_handler:pio level=%d\n", val);

	return IRQ_HANDLED;
}

static void touch_key_eint_register(struct input_dev *input)
{
	struct device_node *node;
	u32 debounce = 0;
	int ret;
	int val;

	node = of_find_compatible_node(NULL, NULL, "mediatek,touch_key");
	if (!node) {
		pr_info("%s: no mediatek,touch_key node\n", __func__);
		return;
	}
	if (!touch_key_enable) {
		int pwr = of_get_named_gpio(node, "pwr-pin", 0);

		if (pwr >= 0 && !gpio_request(pwr, "touch_key pwr gpio")) {
			gpio_direction_output(pwr, 0);
			touch_key_pwr_gpio = pwr;
		}
		pr_info("%s: capacitive side key off (mtk_kpd.touch_key_enable=0), key IC powered down\n",
			__func__);
		return;
	}

	touch_key_irq = irq_of_parse_and_map(node, 0);
	if (!touch_key_irq) {
		pr_err("%s: touch_key irq not available\n", __func__);
		return;
	}
	touch_key_irq_dt_type = irq_get_trigger_type(touch_key_irq);

	__set_bit(KEY_F4, input->keybit);

	ret = request_threaded_irq(touch_key_irq, NULL, touch_key_eint_handler,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
			"touch_key-eint", input);
	pr_info("touch_key request_irq ret=%d\n", ret);
	if (ret < 0) {
		touch_key_irq = 0;
		return;
	}

	touch_key_gpio = of_get_named_gpio(node, "cfg-pin", 0);
	if (touch_key_gpio < 0) {
		pr_err("%s: cfg-pin gpio not provided\n", __func__);
		return;
	}

	if (of_property_read_u32(node, "debounce", &debounce))
		debounce = 0;

	ret = gpio_request(touch_key_gpio, "touch_key gpio");
	if (ret < 0) {
		pr_err("%s: unable to request touch_key gpio[%d]\n", __func__,
		       touch_key_gpio);
		touch_key_gpio = -1;
		return;
	}
	MINDONE_GPIO_SET_DEBOUNCE(touch_key_gpio, debounce);

	val = gpio_get_value(touch_key_gpio);
	touch_key_state = (val == 0);
	msleep(5);

	touch_key_pwr_gpio = of_get_named_gpio(node, "pwr-pin", 0);
	if (touch_key_pwr_gpio < 0) {
		pr_err("%s: touch_key pwr gpio not provided\n", __func__);
		return;
	}
	ret = gpio_request(touch_key_pwr_gpio, "touch_key pwr gpio");
	if (ret < 0) {
		pr_err("%s: unable to request touch_key pwr gpio[%d]\n", __func__,
		       touch_key_pwr_gpio);
		touch_key_pwr_gpio = -1;
		return;
	}
	gpio_direction_output(touch_key_pwr_gpio, 1);
}

static void touch_key_eint_unregister(struct input_dev *input)
{
	if (touch_key_irq) {
		free_irq(touch_key_irq, input);
		irq_set_irq_type(touch_key_irq, touch_key_irq_dt_type);
	}
	if (touch_key_gpio >= 0)
		gpio_free(touch_key_gpio);
	if (touch_key_pwr_gpio >= 0) {
		gpio_direction_output(touch_key_pwr_gpio, 0);
		gpio_free(touch_key_pwr_gpio);
	}
}

static void touch_key_suspend(void)
{
	if (touch_key_irq && !touch_key_irq_disabled) {
		disable_irq_nosync(touch_key_irq);
		touch_key_irq_disabled = true;
	}
	if (touch_key_pwr_gpio >= 0)
		gpio_direction_output(touch_key_pwr_gpio, 0);
}

static void touch_key_resume(void)
{
	if (touch_key_pwr_gpio >= 0) {
		gpio_direction_output(touch_key_pwr_gpio, 1);
		msleep(5);
	}
	if (touch_key_irq && touch_key_irq_disabled) {
		enable_irq(touch_key_irq);
		touch_key_irq_disabled = false;
	}
}

static void kpd_get_keymap_state(void __iomem *kp_base, u16 state[])
{
	state[0] = readw(kp_base + KP_MEM1);
	state[1] = readw(kp_base + KP_MEM2);
	state[2] = readw(kp_base + KP_MEM3);
	state[3] = readw(kp_base + KP_MEM4);
	state[4] = readw(kp_base + KP_MEM5);
}

static void kpd_double_key_enable(void __iomem *kp_base, int en)
{
	u16 tmp;

	tmp = *(u16*)KP_SEL;
	if (en)
		writew((u16)(tmp | KPD_DOUBLE_KEY_MASK), kp_base + KP_SEL);
	else
		writew((u16)(tmp & ~KPD_DOUBLE_KEY_MASK), kp_base + KP_SEL);
}

static void enable_kpd(void __iomem *kp_base, int en)
{
	writew((u16)(en), kp_base + KP_EN);
}

static void kpd_keymap_handler(unsigned long data)
{
	int i, j;
	int pressed;
	u16 new_state[KPD_NUM_MEMS], change, mask;
	u16 hw_keycode, keycode;
	void *dest;
	struct mtk_keypad *keypad = (struct mtk_keypad *)data;

	kpd_get_keymap_state(keypad->base, new_state);

	__pm_wakeup_event(keypad->suspend_lock, 500);

	for (i = 0; i < KPD_NUM_MEMS; i++) {
		change = new_state[i] ^ keypad->keymap_state[i];
		if (!change)
			continue;

		for (j = 0; j < 16U; j++) {
			mask = (u16) 1 << j;
			if (!(change & mask))
				continue;

			hw_keycode = (i << 4) + j;

			if (hw_keycode >= KPD_NUM_KEYS)
				continue;

			/* bit is 1: not pressed, 0: pressed */
			pressed = (new_state[i] & mask) == 0U;
			pr_info("(%s) HW keycode = %d\n",
				(pressed) ? "pressed" : "released",
					hw_keycode);

			keycode = keypad->hw_init_map[hw_keycode];
			if (!keycode)
				continue;
			input_report_key(keypad->input_dev, keycode, pressed);
			input_sync(keypad->input_dev);
			pr_info("report Linux keycode = %d\n", keycode);
		}
	}

	dest = memcpy(keypad->keymap_state, new_state, sizeof(new_state));
	enable_irq(keypad->irqnr);
}

static irqreturn_t kpd_irq_handler(int irq, void *dev_id)
{
	/* use _nosync to avoid deadlock */
	struct mtk_keypad *keypad = dev_id;

	disable_irq_nosync(keypad->irqnr);
	tasklet_schedule(&keypad->tasklet);
	return IRQ_HANDLED;
}

static int kpd_get_dts_info(struct mtk_keypad *keypad,
				struct device_node *node)
{
	int ret;

	ret = of_property_read_u32(node, "mediatek,key-debounce-ms",
		&keypad->key_debounce);
	if (ret) {
		pr_err("read mediatek,key-debounce-ms error.\n");
		return ret;
	}

	ret = of_property_read_u32(node, "mediatek, use-extend-type",
		&keypad->use_extend_type);
	if (ret) {
		pr_err("read mediatek,use-extend-type error.\n");
		keypad->use_extend_type = 0;
	}

	ret = of_property_read_u32(node, "mediatek,hw-map-num",
		&keypad->hw_map_num);
	if (ret) {
		pr_err("read mediatek,hw-map-num error.\n");
		return ret;
	}

	if (keypad->hw_map_num > KPD_NUM_KEYS) {
		pr_err("hw-map-num error, it cannot bigger than %d.\n",
			KPD_NUM_KEYS);
		return -EINVAL;
	}

	ret = of_property_read_u32_array(node, "mediatek,hw-init-map",
		keypad->hw_init_map, keypad->hw_map_num);

	if (ret) {
		pr_err("hw-init-map was not defined in dts.\n");
		return ret;
	}

	pr_debug("deb= %d\n", keypad->key_debounce);

	return 0;
}

static int kpd_pdrv_probe(struct platform_device *pdev)
{
	struct mtk_keypad *keypad;
	struct resource *res;
	int i;
	int ret;

	keypad = devm_kzalloc(&pdev->dev, sizeof(*keypad), GFP_KERNEL);
	if (!keypad)
		return -ENOMEM;

	keypad->clk = devm_clk_get(&pdev->dev, "kpd");
	if (IS_ERR(keypad->clk))
		return PTR_ERR(keypad->clk);

	ret = clk_prepare_enable(keypad->clk);
	if (ret) {
		pr_err("cannot prepare/enable keypad clock\n");
		return ret;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		ret = -ENODEV;
		goto err_unprepare_clk;
	}

	keypad->base = devm_ioremap(&pdev->dev, res->start,
			resource_size(res));
	if (!keypad->base) {
		pr_err("KP iomap failed\n");
		ret = -EBUSY;
		goto err_unprepare_clk;
	}

	keypad->irqnr = irq_of_parse_and_map(pdev->dev.of_node, 0);
	if (!keypad->irqnr) {
		pr_err("KP get irqnr failed\n");
		ret = -ENODEV;
		goto err_unprepare_clk;
	}

	ret = kpd_get_dts_info(keypad, pdev->dev.of_node);
	if (ret) {
		pr_err("get dts info failed.\n");
		goto err_unprepare_clk;
	}

	memset(keypad->keymap_state, 0xff, sizeof(keypad->keymap_state));

	keypad->input_dev = devm_input_allocate_device(&pdev->dev);
	if (!keypad->input_dev) {
		pr_err("input allocate device fail.\n");
		ret = -ENOMEM;
		goto err_unprepare_clk;
	}

	keypad->input_dev->name = KPD_NAME;
	keypad->input_dev->id.bustype = BUS_HOST;
	keypad->input_dev->dev.parent = &pdev->dev;

	__set_bit(EV_KEY, keypad->input_dev->evbit);

	if (!keypad->use_extend_type) {
		for (i = 17; i < KPD_NUM_KEYS; i += 9)
			keypad->hw_init_map[i] = 0;
	}

	for (i = 0; i < KPD_NUM_KEYS; i++) {
		if (keypad->hw_init_map[i])
			__set_bit(keypad->hw_init_map[i],
				keypad->input_dev->keybit);
	}

	if (keypad->use_extend_type)
		kpd_double_key_enable(keypad->base, 1);

	ret = input_register_device(keypad->input_dev);
	if (ret) {
		pr_err("register input device failed (%d)\n", ret);
		goto err_unprepare_clk;
	}

	input_set_drvdata(keypad->input_dev, keypad);

	keypad->suspend_lock = wakeup_source_register(NULL, "kpd wakelock");
	if (!keypad->suspend_lock) {
		pr_err("wakeup source init failed.\n");
		goto err_unregister_device;
	}

	tasklet_init(&keypad->tasklet, kpd_keymap_handler,
					(unsigned long)keypad);

	writew((u16)(keypad->key_debounce & KPD_DEBOUNCE_MASK),
			keypad->base + KP_DEBOUNCE);

	/* register IRQ */
	ret = request_irq(keypad->irqnr, kpd_irq_handler, IRQF_TRIGGER_NONE,
			KPD_NAME, keypad);
	if (ret) {
		pr_err("register IRQ failed (%d)\n", ret);
		goto err_irq;
	}

	ret = enable_irq_wake(keypad->irqnr);
	if (ret < 0)
		pr_err("irq %d enable irq wake fail\n", keypad->irqnr);

	/* last step on purpose: only reachable once the main matrix IRQ is
	 * live, so a probe failure above never leaves hall resources
	 * registered against an input_dev that devm is about to free.
	 */
	hall_eint_register(keypad->input_dev);
	touch_key_eint_register(keypad->input_dev);
	if (device_create_file(&pdev->dev, &dev_attr_hall_position))
		dev_warn(&pdev->dev, "hall_position attribute not created\n");

	platform_set_drvdata(pdev,keypad);
	enable_kpd(keypad->base, 1);

	return 0;

err_irq:
	tasklet_kill(&keypad->tasklet);

err_unregister_device:
	input_unregister_device(keypad->input_dev);

err_unprepare_clk:
	clk_disable_unprepare(keypad->clk);

	return ret;
}

static void kpd_pdrv_remove(struct platform_device *pdev)
{
	struct mtk_keypad *keypad = platform_get_drvdata(pdev);

	device_remove_file(&pdev->dev, &dev_attr_hall_position);
	touch_key_eint_unregister(keypad->input_dev);
	hall_eint_unregister(keypad->input_dev);
	free_irq(keypad->irqnr, keypad);
	tasklet_kill(&keypad->tasklet);
	wakeup_source_unregister(keypad->suspend_lock);
	input_unregister_device(keypad->input_dev);
	clk_disable_unprepare(keypad->clk);

	return;
}

static int kpd_pdrv_suspend(struct platform_device *pdev, pm_message_t state)
{
	touch_key_suspend();
	return 0;
}

static int kpd_pdrv_resume(struct platform_device *pdev)
{
	touch_key_resume();
	return 0;
}

static const struct of_device_id kpd_of_match[] = {
	{.compatible = "mediatek,mt6779-keypad"},
	{.compatible = "mediatek,kp"},
	{}
};

static struct platform_driver kpd_pdrv = {
	.probe = kpd_pdrv_probe,
	.remove_new = kpd_pdrv_remove,
	.suspend = kpd_pdrv_suspend,
	.resume = kpd_pdrv_resume,
	.driver = {
		   .name = KPD_NAME,
		   .of_match_table = kpd_of_match,
	},
};
module_platform_driver(kpd_pdrv);

MODULE_AUTHOR("Mediatek Corporation");
MODULE_DESCRIPTION("MTK Keypad (KPD) Driver");
MODULE_LICENSE("GPL");

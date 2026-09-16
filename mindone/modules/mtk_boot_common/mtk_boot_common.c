// SPDX-License-Identifier: GPL-2.0
/*
 * mtk_boot_common — minimal /sys/class/BOOT/BOOT/boot/boot_mode node.
 *
 * mind_one HAL PQ on this device tries to open
 * /sys/class/BOOT/BOOT/boot/boot_mode at startup and prints
 * "fail to open" when the node is missing. On stock MediaTek trees this
 * node is created by a proprietary "get bootmode" driver that is not part
 * of any vendor source tree we have searched (this repository and
 * kernel612-common included — none contain it).
 * This is a from-scratch minimal replacement: same class/device/attribute
 * layout, small enough to review at a glance.
 *
 * Value source: LK is known to inject extra "atag,*" properties under
 * /chosen at runtime that are not present in the statically compiled DTS
 * (see the fact log — DRAM parameters arrive the same way, and the
 * stock DTS decompile at device/dts/vendor_boot_b-platform.dts:37-43 shows
 * several "atag,videolfb-*" properties with no static counterpart in the
 * source .dts). If LK also injects an "atag,boot_mode" (or a "boot"
 * sub-node with a "boot_mode" property) this driver picks it up; if not,
 * it falls back to a hardcoded BOOT_MODE_NORMAL(0). Either way the sysfs
 * node exists, which is the actual HAL PQ complaint — a wrong-but-present
 * "normal" is what every real boot of this device needs anyway.
 *
 * NOT verified against real /chosen content on this device (device is
 * off-limits for this task — logs only). Documented as best-effort in
 * the project issue registry .
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/err.h>
#include <mindone/compat.h>

/* Common MediaTek boot-mode encoding (subset actually distinguishable
 * without vendor headers; anything unrecognised reports NORMAL).
 */
#define BOOT_MODE_NORMAL	0
#define BOOT_MODE_META		1
#define BOOT_MODE_RECOVERY	2
#define BOOT_MODE_FACTORY	4

static struct class *boot_class;
static struct device *boot_dev;

static u32 mtk_boot_mode_from_dt(void)
{
	struct device_node *chosen, *sub;
	u32 val;

	const u8 *tag;
	int len = 0;

	chosen = of_find_node_by_path("/chosen");
	if (!chosen)
		return BOOT_MODE_NORMAL;

	/* Verified on the device on 12.09 (F4193): LK puts an "atag,boot" tag into /chosen =
	 * struct tag_bootmode { u32 size; u32 tag; u32 bootmode; u32 boottype; } in
	 * little-endian (bytes 10000000 02080041 00000000 02000000: size=0x10,
	 * tag=0x41000802, bootmode=0 NORMAL, boottype=2). The value is the third word.
	 */
	tag = of_get_property(chosen, "atag,boot", &len);
	if (tag && len >= 12) {
		val = le32_to_cpup((const __le32 *)(tag + 8));
		goto out_put_chosen;
	}

	/* Fallback: a flat "atag,boot_mode" (not seen on this device). */
	if (!of_property_read_u32(chosen, "atag,boot_mode", &val))
		goto out_put_chosen;

	/* Second guess: a "boot" sub-node mirroring the sysfs layout this
	 * driver exposes ("boot_mode" attribute inside a "boot" group).
	 */
	sub = of_get_child_by_name(chosen, "boot");
	if (sub) {
		if (of_property_read_u32(sub, "boot_mode", &val))
			val = BOOT_MODE_NORMAL;
		of_node_put(sub);
		of_node_put(chosen);
		return val;
	}

	val = BOOT_MODE_NORMAL;

out_put_chosen:
	of_node_put(chosen);
	return val;
}

static ssize_t boot_mode_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%u\n", mtk_boot_mode_from_dt());
}
static DEVICE_ATTR_RO(boot_mode);

static struct attribute *boot_attrs[] = {
	&dev_attr_boot_mode.attr,
	NULL,
};

static const struct attribute_group boot_attr_group = {
	.name = "boot",
	.attrs = boot_attrs,
};

static const struct attribute_group *boot_attr_groups[] = {
	&boot_attr_group,
	NULL,
};

static int __init mtk_boot_common_init(void)
{
	boot_class = MINDONE_CLASS_CREATE("BOOT");
	if (IS_ERR(boot_class)) {
		pr_notice("mtk_boot_common: class_create failed, errno=%ld\n",
			  PTR_ERR(boot_class));
		return PTR_ERR(boot_class);
	}

	boot_dev = device_create_with_groups(boot_class, NULL, 0, NULL,
					      boot_attr_groups, "BOOT");
	if (IS_ERR(boot_dev)) {
		pr_notice("mtk_boot_common: device_create failed, errno=%ld\n",
			  PTR_ERR(boot_dev));
		class_destroy(boot_class);
		return PTR_ERR(boot_dev);
	}

	return 0;
}

static void __exit mtk_boot_common_exit(void)
{
	device_destroy(boot_class, 0);
	class_destroy(boot_class);
}

module_init(mtk_boot_common_init);
module_exit(mtk_boot_common_exit);

MODULE_DESCRIPTION("mind_one: minimal /sys/class/BOOT/BOOT/boot/boot_mode");
MODULE_LICENSE("GPL");

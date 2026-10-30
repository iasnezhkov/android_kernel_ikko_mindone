// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 MediaTek Inc. */
/* Copyright (C) 2026 The LineageOS Project */
#include <asm/barrier.h>
#include <asm/sysreg.h>
#include <linux/atomic.h>
#include <linux/bitops.h>
#include <linux/bug.h>
#include <linux/cpu.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/ratelimit.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

#define ECC_UE_BIT BIT_ULL(29)
#define ECC_DE_BIT BIT_ULL(23)
#define ECC_IRQ_TRIGGER_THRESHOLD 1

struct cache_parity_irq {
	struct device *dev;
	int irq;
	int cpu;
	bool dsu;
	atomic_t nr_err;
};

struct cache_parity {
	atomic_t nr_err;
	atomic64_t timestamp;
	struct cache_parity_irq irqs[];
};

static ssize_t cache_status_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct cache_parity *parity = dev_get_drvdata(dev);
	unsigned int count = atomic_read(&parity->nr_err);

	if (!count)
		return sysfs_emit(buf, "False\n");

	return sysfs_emit(buf, "True, %u times (%llu ns)\n", count,
			  (unsigned long long)atomic64_read(&parity->timestamp));
}
static DEVICE_ATTR_RO(cache_status);

static struct attribute *cache_parity_attrs[] = {
	&dev_attr_cache_status.attr,
	NULL,
};
ATTRIBUTE_GROUPS(cache_parity);

static irqreturn_t cache_parity_isr(int irq, void *data)
{
	struct cache_parity_irq *record = data;
	struct cache_parity *parity = dev_get_drvdata(record->dev);
	u64 misc0, status;
	int cpu = raw_smp_processor_id();

	if (!record->dsu && cpu != record->cpu) {
		dev_err_ratelimited(record->dev,
			"irq%d reached cpu%d instead of cpu%d; disabling\n",
			irq, cpu, record->cpu);
		disable_irq_nosync(irq);
		return IRQ_HANDLED;
	}

	write_sysreg_s(record->dsu ? 1 : 0, SYS_ERRSELR_EL1);
	isb();
	misc0 = read_sysreg_s(SYS_ERXMISC0_EL1);
	status = read_sysreg_s(SYS_ERXSTATUS_EL1);
	write_sysreg_s(status, SYS_ERXSTATUS_EL1);
	if (!misc0 && !status)
		write_sysreg_s(0xFFC00000, SYS_ERXSTATUS_EL1);
	dsb(sy);

	atomic64_cmpxchg(&parity->timestamp, 0, ktime_get_mono_fast_ns());
	atomic_inc(&parity->nr_err);
	dev_err_ratelimited(record->dev,
		"Cache ECC error: irq%d cpu%d %s misc0_el1=0x%016llx status_el1=0x%016llx\n",
		irq, cpu, record->dsu ? "DSU" : "CPU", misc0, status);
	WARN_ON_ONCE(status & (ECC_UE_BIT | ECC_DE_BIT));

	if (atomic_inc_return(&record->nr_err) > ECC_IRQ_TRIGGER_THRESHOLD) {
		disable_irq_nosync(irq);
		dev_err_ratelimited(record->dev,
			"disabling irq%d after repeated ECC errors\n", irq);
	}

	return IRQ_HANDLED;
}

static int cache_parity_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cache_parity *parity;
	u32 dsu_hwirq;
	int count, i, cpu = 0, ret;
	bool found_dsu = false;

	ret = of_property_read_u32(dev->of_node, "arm-dsu-ecc-hwirq",
				   &dsu_hwirq);
	if (ret)
		return dev_err_probe(dev, ret, "missing arm-dsu-ecc-hwirq\n");

	count = platform_irq_count(pdev);
	if (count < 0)
		return count;
	if (!count)
		return -EINVAL;

	parity = devm_kzalloc(dev, struct_size(parity, irqs, count), GFP_KERNEL);
	if (!parity)
		return -ENOMEM;

	atomic_set(&parity->nr_err, 0);
	atomic64_set(&parity->timestamp, 0);
	platform_set_drvdata(pdev, parity);

	cpus_read_lock();
	for (i = 0; i < count; i++) {
		struct cache_parity_irq *record = &parity->irqs[i];
		struct irq_data *irq_data;

		record->dev = dev;
		record->irq = platform_get_irq(pdev, i);
		if (record->irq < 0) {
			ret = record->irq;
			goto unlock;
		}
		irq_data = irq_get_irq_data(record->irq);
		if (!irq_data) {
			ret = -EINVAL;
			goto unlock;
		}
		record->dsu = irqd_to_hwirq(irq_data) == dsu_hwirq;
		record->cpu = -1;
		atomic_set(&record->nr_err, 0);
		if (record->dsu) {
			if (found_dsu) {
				ret = -EINVAL;
				goto unlock;
			}
			found_dsu = true;
		} else {
			if (cpu >= nr_cpu_ids || !cpu_possible(cpu)) {
				ret = -EINVAL;
				goto unlock;
			}
			if (!cpu_online(cpu)) {
				ret = -EPROBE_DEFER;
				goto unlock;
			}
			record->cpu = cpu++;
		}

		ret = devm_request_irq(dev, record->irq, cache_parity_isr,
				       IRQF_NO_AUTOEN | IRQF_NOBALANCING,
				       "cache_parity", record);
		if (ret)
			goto unlock;
		if (!record->dsu) {
			ret = irq_set_affinity(record->irq, cpumask_of(record->cpu));
			if (ret)
				goto unlock;
		}
	}

	if (!found_dsu) {
		ret = -EINVAL;
		goto unlock;
	}
	for (i = 0; i < count; i++)
		enable_irq(parity->irqs[i].irq);
	ret = 0;
unlock:
	cpus_read_unlock();
	return ret;
}

static const struct of_device_id cache_parity_of_match[] = {
	{ .compatible = "mediatek,mt6873-cache-parity" },
	{}
};
MODULE_DEVICE_TABLE(of, cache_parity_of_match);

static struct platform_driver cache_parity_driver = {
	.probe = cache_parity_probe,
	.driver = {
		.name = "cache_parity",
		.of_match_table = cache_parity_of_match,
		.dev_groups = cache_parity_groups,
	},
};
module_platform_driver(cache_parity_driver);

MODULE_DESCRIPTION("MediaTek CPU and DSU cache ECC driver");
MODULE_LICENSE("GPL v2");

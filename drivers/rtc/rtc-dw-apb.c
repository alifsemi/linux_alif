// SPDX-License-Identifier: GPL-2.0+
/*
 * The hardware IP in the Alif Ensemble SoC is really a Synopsys DW-APB timer.
 * This code is based on APM X-Gene SoC Real Time Clock Driver. The X-gene
 * soc also is likely using the same IP. The Linux kernel did have an RTC
 * driver for this IP but at some point that code was deprecated, and it was
 * classified as a clockevent/clocksource (drivers/clocksource/dw_apb_timer.c)
 * since the IP does not maintain date and time like other RTC devices and is
 * just a counter in hardware. The date/time maintenance is done in software
 * for this driver.
 *
 * Copyright (c) 2021-2025, Alif Semicondutor
 * Author: Harith George <harith.g@alifsemi.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/slab.h>
#include <linux/capability.h>
#include <linux/kstrtox.h>

/* RTC CSR Registers */
#define RTC_CCVR			0x00
#define RTC_CMR				0x04
#define RTC_CLR				0x08
#define RTC_CCR				0x0C
#define RTC_CCR_IE			BIT(0)
#define RTC_CCR_MASK			BIT(1)
#define RTC_CCR_EN			BIT(2)
#define RTC_CCR_WEN			BIT(3)
#define RTC_CCR_PSCLREN			BIT(4)
#define RTC_STAT			0x10
#define RTC_STAT_BIT			BIT(0)
#define RTC_RSTAT			0x14
#define RTC_EOI				0x18
#define RTC_COMP_VERSION		0x1C
#define RTC_CPSR			0x20

/* Prescaler: 16-bit field (bits 15:0), valid range 1-65535 */
#define RTC_CPSR_MASK			0xFFFF
#define COUNTER_PRESCALER_MIN		1
#define COUNTER_PRESCALER_MAX		65535

struct ensemble_rtc_dev {
	struct rtc_device *rtc;
	void __iomem *csr_base;
	struct clk *clk;
	struct mutex lock;	/* Serializes RTC_CCR and RTC_CPSR access */
};

/*
 * ensemble_rtc_set_prescaler - Safely update the prescaler register.
 *
 * Disables the prescaler, writes the new value (masked to 16 bits),
 * then re-enables prescaler and counter.
 *
 * Caller must hold pdata->lock.
 */
static void ensemble_rtc_set_prescaler(struct ensemble_rtc_dev *pdata, u32 val)
{
	u32 ccr;

	/* Disable prescaler and counter */
	ccr = readl(pdata->csr_base + RTC_CCR);
	ccr &= ~(RTC_CCR_PSCLREN | RTC_CCR_EN);
	writel(ccr, pdata->csr_base + RTC_CCR);

	/* Write new prescaler value, masked to 16-bit register width */
	writel(val & RTC_CPSR_MASK, pdata->csr_base + RTC_CPSR);

	/* Re-enable prescaler and counter */
	ccr |= RTC_CCR_PSCLREN | RTC_CCR_EN;
	writel(ccr, pdata->csr_base + RTC_CCR);
}

static ssize_t prescaler_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct ensemble_rtc_dev *pdata = dev_get_drvdata(dev->parent);
	u32 val;

	mutex_lock(&pdata->lock);
	/* Read prescaler value directly from hardware register */
	val = readl(pdata->csr_base + RTC_CPSR) & RTC_CPSR_MASK;
	mutex_unlock(&pdata->lock);

	return sysfs_emit(buf, "%u\n", val);
}

static ssize_t prescaler_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct ensemble_rtc_dev *pdata = dev_get_drvdata(dev->parent);
	u32 val;
	int ret;

	/* Changing the prescaler alters RTC tick rate (timekeeping) */
	if (!capable(CAP_SYS_TIME))
		return -EPERM;

	ret = kstrtou32(buf, 0, &val);
	if (ret)
		return ret;

	if (val < COUNTER_PRESCALER_MIN || val > COUNTER_PRESCALER_MAX) {
		dev_dbg(dev, "prescaler value %u out of range [%u-%u]\n",
			val, COUNTER_PRESCALER_MIN, COUNTER_PRESCALER_MAX);
		return -EINVAL;
	}

	mutex_lock(&pdata->lock);
	ensemble_rtc_set_prescaler(pdata, val);
	mutex_unlock(&pdata->lock);
	return count;
}

static DEVICE_ATTR_RW(prescaler);

static ssize_t counter_wrap_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct ensemble_rtc_dev *pdata = dev_get_drvdata(dev->parent);
	u32 val;

	mutex_lock(&pdata->lock);
	val = !!(readl(pdata->csr_base + RTC_CCR) & RTC_CCR_WEN);
	mutex_unlock(&pdata->lock);

	return sysfs_emit(buf, "%u\n", val);
}

static ssize_t counter_wrap_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct ensemble_rtc_dev *pdata = dev_get_drvdata(dev->parent);
	u32 ccr, val;
	int ret;

	/* Changing wrap mode alters counter behavior on alarm match */
	if (!capable(CAP_SYS_TIME))
		return -EPERM;

	ret = kstrtou32(buf, 0, &val);
	if (ret)
		return ret;

	if (val > 1) {
		dev_dbg(dev, "counter_wrap: invalid value %u (use 0 or 1)\n",
			val);
		return -EINVAL;
	}

	mutex_lock(&pdata->lock);
	ccr = readl(pdata->csr_base + RTC_CCR);
	if (val)
		ccr |= RTC_CCR_WEN;
	else
		ccr &= ~RTC_CCR_WEN;
	writel(ccr, pdata->csr_base + RTC_CCR);
	mutex_unlock(&pdata->lock);

	return count;
}

static DEVICE_ATTR_RW(counter_wrap);

static struct attribute *ensemble_rtc_attrs[] = {
	&dev_attr_prescaler.attr,
	&dev_attr_counter_wrap.attr,
	NULL,
};

static const struct attribute_group ensemble_rtc_attr_group = {
	.attrs = ensemble_rtc_attrs,
};

static int ensemble_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct ensemble_rtc_dev *pdata = dev_get_drvdata(dev);

	rtc_time64_to_tm(readl(pdata->csr_base + RTC_CCVR), tm);
	return 0;
}

static int ensemble_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct ensemble_rtc_dev *pdata = dev_get_drvdata(dev);
	unsigned int value = 0;

	/*
	 * NOTE: After the following write, the RTC_CCVR is only reflected
	 *       after the update cycle of 1 seconds.
	 */
	writel((u32)rtc_tm_to_time64(tm), pdata->csr_base + RTC_CLR);
	value = readl(pdata->csr_base + RTC_CLR); /* Force a barrier */

	return 0;
}

static int ensemble_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct ensemble_rtc_dev *pdata = dev_get_drvdata(dev);
	u32 ccr, cmr;

	mutex_lock(&pdata->lock);
	cmr = readl(pdata->csr_base + RTC_CMR);
	ccr = readl(pdata->csr_base + RTC_CCR);
	mutex_unlock(&pdata->lock);

	rtc_time64_to_tm(cmr, &alrm->time);
	alrm->enabled = !!(ccr & RTC_CCR_IE);

	return 0;
}

/* Caller must hold pdata->lock */
static void ensemble_rtc_alarm_irq_set(struct ensemble_rtc_dev *pdata,
				       u32 enabled)
{
	u32 ccr;

	ccr = readl(pdata->csr_base + RTC_CCR);
	if (enabled) {
		ccr &= ~RTC_CCR_MASK;
		ccr |= RTC_CCR_IE;
	} else {
		ccr &= ~RTC_CCR_IE;
		ccr |= RTC_CCR_MASK;
	}
	writel(ccr, pdata->csr_base + RTC_CCR);
}

static int ensemble_rtc_alarm_irq_enable(struct device *dev, u32 enabled)
{
	struct ensemble_rtc_dev *pdata = dev_get_drvdata(dev);

	mutex_lock(&pdata->lock);
	ensemble_rtc_alarm_irq_set(pdata, enabled);
	mutex_unlock(&pdata->lock);

	return 0;
}

static int ensemble_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct ensemble_rtc_dev *pdata = dev_get_drvdata(dev);

	mutex_lock(&pdata->lock);
	writel((u32)rtc_tm_to_time64(&alrm->time), pdata->csr_base + RTC_CMR);
	ensemble_rtc_alarm_irq_set(pdata, alrm->enabled);
	mutex_unlock(&pdata->lock);

	return 0;
}

static const struct rtc_class_ops ensemble_rtc_ops = {
	.read_time	= ensemble_rtc_read_time,
	.set_time	= ensemble_rtc_set_time,
	.read_alarm	= ensemble_rtc_read_alarm,
	.set_alarm	= ensemble_rtc_set_alarm,
	.alarm_irq_enable = ensemble_rtc_alarm_irq_enable,
};

static irqreturn_t ensemble_rtc_interrupt(int irq, void *id)
{
	struct ensemble_rtc_dev *pdata = id;
	/* Check if interrupt asserted */
	if (!(readl(pdata->csr_base + RTC_STAT) & RTC_STAT_BIT))
		return IRQ_NONE;

	/* Clear interrupt */
	readl(pdata->csr_base + RTC_EOI);

	rtc_update_irq(pdata->rtc, 1, RTC_IRQF | RTC_AF);

	return IRQ_HANDLED;
}

static int ensemble_rtc_probe(struct platform_device *pdev)
{
	struct ensemble_rtc_dev *pdata;
	struct resource *res;
	int ret;
	int irq;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;
	platform_set_drvdata(pdev, pdata);
	mutex_init(&pdata->lock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pdata->csr_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pdata->csr_base))
		return PTR_ERR(pdata->csr_base);

	pdata->rtc = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(pdata->rtc))
		return PTR_ERR(pdata->rtc);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;
	ret = devm_request_irq(&pdev->dev, irq, ensemble_rtc_interrupt, 0,
			       dev_name(&pdev->dev), pdata);
	if (ret) {
		dev_err(&pdev->dev, "Could not request IRQ\n");
		return ret;
	}

	pdata->clk = devm_clk_get(&pdev->dev, "timer");
	if (IS_ERR(pdata->clk)) {
		dev_err(&pdev->dev, "Couldn't  why get the clock for RTC\n");
		return -ENODEV;
	}
	ret = clk_prepare_enable(pdata->clk);
	if (ret)
		return ret;

	ret = device_init_wakeup(&pdev->dev, 1);
	if (ret) {
		clk_disable_unprepare(pdata->clk);
		return ret;
	}

	pdata->rtc->ops = &ensemble_rtc_ops;
	pdata->rtc->range_max = U32_MAX;

	/* Add sysfs attributes (prescaler, counter_wrap) to RTC class device */
	ret = rtc_add_group(pdata->rtc, &ensemble_rtc_attr_group);
	if (ret) {
		dev_err(&pdev->dev, "Failed to create sysfs group: %d\n", ret);
		device_init_wakeup(&pdev->dev, 0);
		clk_disable_unprepare(pdata->clk);
		return ret;
	}

	dev_info(&pdev->dev, "DW APB RTC version: 0x%08x\n",
		 readl(pdata->csr_base + RTC_COMP_VERSION));

	ret = devm_rtc_register_device(pdata->rtc);
	if (ret) {
		device_init_wakeup(&pdev->dev, 0);
		clk_disable_unprepare(pdata->clk);
		return ret;
	}

	return 0;
}

static void __exit ensemble_rtc_remove(struct platform_device *pdev)
{
	struct ensemble_rtc_dev *pdata = platform_get_drvdata(pdev);

	ensemble_rtc_alarm_irq_enable(&pdev->dev, 0);
	device_init_wakeup(&pdev->dev, 0);
	clk_disable_unprepare(pdata->clk);
}

#ifdef CONFIG_OF
static const struct of_device_id ensemble_rtc_of_match[] = {
	{.compatible = "snps,designware-rtc" },
	{ }
};
MODULE_DEVICE_TABLE(of, ensemble_rtc_of_match);
#endif

static struct platform_driver ensemble_rtc_driver = {
	.probe		= ensemble_rtc_probe,
	.remove_new     = __exit_p(ensemble_rtc_remove),
	.driver		= {
		.name	= "dw-apb-rtc",
		.of_match_table	= of_match_ptr(ensemble_rtc_of_match),
	},
};

module_platform_driver(ensemble_rtc_driver);

MODULE_DESCRIPTION("Synopsys DW APB RTC driver");
MODULE_AUTHOR("Harith George <harith.g@alifsemi.com>");
MODULE_LICENSE("GPL");

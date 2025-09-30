// SPDX-License-Identifier: GPL-2.0
/*
 * pci-sky1 - PCIe controller driver for CIX's sky1 SoCs
 *
 * Author: Zichar Zhang <zichar.zhang@cixtech.com>
 */
#include <linux/acpi.h>
#include <linux/gpio/driver.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/reset.h>
#include <linux/pm.h>

struct wake_gpio_data {
	struct device *dev;
	int irq;
	atomic_t irq_count;
};

static irqreturn_t wake_gpio_irq(int irq, void *data)
{
	struct wake_gpio_data *wgd = data;
	int  count;

	atomic_inc(&wgd->irq_count);
	count = atomic_read(&wgd->irq_count);
	if (count > 10000) {
		dev_err(wgd->dev, "too many irqs count %d,"
				" please check wake pin status\n", count);
		disable_irq_nosync(irq);
	}

	dev_dbg(wgd->dev,  "gpio wake handle\n");

	return IRQ_HANDLED;
}

static int wake_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct wake_gpio_data *wgd;
	struct gpio_desc *gpiodesc;
	int ret;

	wgd = devm_kzalloc(dev, sizeof(*wgd), GFP_KERNEL);
	if (!wgd)
		return -ENOMEM;
	wgd->dev = &pdev->dev;
	platform_set_drvdata(pdev, wgd);

	gpiodesc = devm_gpiod_get_optional(dev, "wake", GPIOD_IN);
	if (IS_ERR(gpiodesc))
		return -EINVAL;

	wgd->irq = gpiod_to_irq(gpiodesc);
	if (wgd->irq < 0)
		return -EINVAL;

	atomic_set(&wgd->irq_count, 0);
	ret = devm_request_threaded_irq(dev, wgd->irq, NULL,
			wake_gpio_irq, IRQF_TRIGGER_LOW | IRQF_ONESHOT,
			"wake-gpio", wgd);
	if (ret) {
		dev_err(dev, "unable to request IRQ\n");
		return ret;
	}

	disable_irq(wgd->irq); /* used for wakeup only */

	device_set_wakeup_capable(dev, true);
	if (!device_property_present(dev, "default_wake_disable"))
		device_wakeup_enable(dev);

	return 0;
}

static int wake_gpio_remove(struct platform_device *pdev)
{
	struct wake_gpio_data *wgd = dev_get_drvdata(&pdev->dev);

	if (wgd->irq > 0)
		free_irq(wgd->irq, wgd);

	return 0;
}

static const struct of_device_id of_wake_gpio_match[] = {
	{ .compatible = "cix,wake-gpio", },
	{},
};

static int wake_gpio_suspend(struct device *dev)
{
	struct wake_gpio_data *wgd = dev_get_drvdata(dev);
	struct irq_desc *desc;

	if (wgd->irq > 0 && device_may_wakeup(wgd->dev)) {
		desc = irq_to_desc(wgd->irq);
		if (desc && desc->depth)
			enable_irq(wgd->irq); /* handle to clear irq */
		enable_irq_wake(wgd->irq);
	}

	return 0;
}

static int wake_gpio_resume(struct device *dev)
{
	struct wake_gpio_data *wgd = dev_get_drvdata(dev);

	if (wgd->irq > 0 && device_may_wakeup(wgd->dev)) {
		disable_irq(wgd->irq);
		disable_irq_wake(wgd->irq);
	}
	atomic_set(&wgd->irq_count, 0);

	return 0;
}

const struct dev_pm_ops wake_gpio_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(wake_gpio_suspend, wake_gpio_resume)
};

static void wake_gpio_shutdown(struct platform_device *pdev)
{
	struct wake_gpio_data *wgd = dev_get_drvdata(&pdev->dev);
	struct irq_desc *desc;

	if (wgd->irq > 0 && device_may_wakeup(wgd->dev)) {
		desc = irq_to_desc(wgd->irq);
		if (desc && desc->depth)
			enable_irq(wgd->irq);
		enable_irq_wake(wgd->irq);
	}
}

static struct platform_driver wake_gpio_driver = {
	.probe  = wake_gpio_probe,
	.remove = wake_gpio_remove,
	.driver = {
		.name	= "wake-gpio",
		.of_match_table = of_wake_gpio_match,
		.pm	= &wake_gpio_pm_ops,
	},
	.shutdown = wake_gpio_shutdown,
};

module_platform_driver(wake_gpio_driver);

MODULE_AUTHOR("Zichar Zhang <zichar@cixtech.com>");
MODULE_DESCRIPTION("Cix wake GPIO driver");
MODULE_LICENSE("GPL v2");

/*
 * FPC Fingerprint sensor device driver
 *
 * This driver will control the platform resources that the FPC fingerprint
 * sensor needs to operate. The major things are probing the sensor to check
 * that it is actually connected and let the Kernel know this and with that also
 * enabling and disabling of regulators, enabling and disabling of platform
 * clocks.
 * *
 * The driver will expose most of its available functionality in sysfs which
 * enables dynamic control of these features from eg. a user space process.
 *
 * The sensor's IRQ events will be pushed to Kernel's event handling system and
 * are exposed in the drivers event node. This makes it possible for a user
 * space process to poll the input node and receive IRQ events easily. Usually
 * this node is available under /dev/input/eventX where 'X' is a number given by
 * the event system. A user space process will need to traverse all the event
 * nodes and ask for its parent's name (through EVIOCGNAME) which should match
 * the value in device tree named input-device-name.
 *
 *
 * Copyright (c) 2015 Fingerprint Cards AB <tech@fingerprints.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License Version 2
 * as published by the Free Software Foundation.
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/clk.h>

#include "fpc_irq.h"

struct priv_data {
	struct clk *spi_main;
};

static int set_clks(struct fpc_data *fpc, bool enable)
{
	struct priv_data *priv = (struct priv_data *)fpc->hwabs->priv;
	int rc = 0;

	if(fpc->clocks_enabled == enable)
		return rc;

	if (enable) {
		rc = clk_enable(priv->spi_main);
		if (rc) {
			dev_err(fpc->dev,"%s: Error enabling spi-main clk: %d\n", __func__, rc);
			return rc;
		}
		fpc->clocks_enabled = true;
	} else{
		rc = clk_disable(priv->spi_main);
		if (rc) {
			dev_err(fpc->dev,"%s: Error disabling spi-main clk: %d\n", __func__, rc);
			return rc;
		}
		fpc->clocks_enabled = flase;
	}

	return rc;
}
static ssize_t clk_enable_set(struct fpc_data *fpc, const char *buf, size_t count)
{
	return set_clks(fpc, (*buf == '1')) ? : count;
}

static int mtk6797_init(struct fpc_data *fpc)
{
	return 0;
}

static void mtk6797_irq_handler(int irq, struct fpc_data *fpc)
{
	struct device *dev = fpc->dev;
	static int current_level = 0; // We assume low level from start
	current_level = !current_level;

	if (current_level) {
		dev_dbg(dev, "Reconfigure irq to trigger in low level\n");
		irq_set_irq_type(irq, IRQF_TRIGGER_LOW);
	} else {
		dev_dbg(dev, "Reconfigure irq to trigger in high level\n");
		irq_set_irq_type(irq, IRQF_TRIGGER_HIGH);
	}
}

static int mtk6797_configure(struct fpc_data *fpc, int *irq_num, int *irq_flags)
{
	struct device *dev = fpc->dev;
	struct priv_data *priv = (struct priv_data *)fpc->hwabs->priv;
	int rc = 0;

	rc = gpio_direction_input(fpc->irq_gpio);
	if (rc != 0) {
		dev_err(dev, "gpio_direction_input failed for IRQ.\n");
		return rc;
	}

	*irq_num = gpio_to_irq(fpc->irq_gpio);

	rc = gpio_direction_output(fpc->rst_gpio, 1);
	if (rc != 0) {
		dev_err(dev, "gpio_direction_output failed for RST.\n");
		return rc;
	}

	priv->spi_main = clk_get(dev, "spi-main");
	if (IS_ERR(priv->spi_main)) {
		dev_err(dev, "%s: failed to get spi-main\n", __func__);
		return -EINVAL;
	}

	*irq_num = gpio_to_irq(fpc->irq_gpio);

	/* The Mediatek platform does not support triggering at both rising
	 * and falling edge at the same time so we configure to trigger on
	 * high level and then reconfigure the interrupt in the interrupt handler.
	 */
	*irq_flags = IRQF_TRIGGER_HIGH;

	clk_prepare(priv->spi_main);
	fpc->clocks_enabled = flase;

	return rc;
}

static struct fpc_gpio_info mtk6797_ops = {
	.init = mtk6797_init,
	.configure = mtk6797_configure,
	.get_val = gpio_get_value,
	.set_val = gpio_set_value,
	.clk_enable_set = clk_enable_set,
	.irq_handler = mtk6797_irq_handler,
};

static struct of_device_id mt6797_of_match[] = {
	{ .compatible = "fpc,fpc_irq", },
	{}
};
MODULE_DEVICE_TABLE(of, mt6797_of_match);

static int mtk6797_probe(struct platform_device *pldev)
{
	struct fpc_data *fpc;
	int rc;
	mtk6797_ops.priv = devm_kzalloc(&pldev->dev, sizeof(struct priv_data), GFP_KERNEL);

	if (!mtk6797_ops.priv) {
		dev_err(&pldev->dev,
			"failed to allocate memory for struct priv_data\n");
		return -ENOMEM;
	}

	rc = fpc_probe(pldev, &mtk6797_ops);

	if (!rc)
		fpc = dev_get_drvdata(&pldev->dev);

	return rc;
}

static struct platform_driver mtk6797_driver = {
	.driver = {
		.name	= "fpc_irq",
		.owner	= THIS_MODULE,
		.of_match_table = mt6797_of_match,
	},
	.probe	= mtk6797_probe,
	.remove	= fpc_remove
};

module_platform_driver(mtk6797_driver);

MODULE_LICENSE("GPL");

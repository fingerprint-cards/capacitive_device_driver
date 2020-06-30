/*
 * FPC Fingerprint sensor device driver
 *
 * This driver will control the platform resources that the FPC fingerprint
 * sensor needs to operate. The major things are probing the sensor to check
 * that it is actually connected and let the Kernel know this and with that also
 * enabling and disabling of regulators, enabling and disabling of platform
 * clocks, controlling GPIOs such as SPI chip select, sensor reset line, sensor
 * IRQ line, MISO and MOSI lines.
 *
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
 * This driver will NOT send any SPI commands to the sensor it only controls the
 * electrical parts.
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
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/interrupt.h>

#include "fpc_irq.h"

#define FPC_RESET_LOW_US 5000
#define FPC_RESET_HIGH1_US 100
#define FPC_RESET_HIGH2_US 5000

#define FPC_TTW_HOLD_TIME 1000
#define SUPPLY_1V8	1800000UL
#define SUPPLY_3V3	3300000UL
#define SUPPLY_TX_MIN	SUPPLY_3V3
#define SUPPLY_TX_MAX	SUPPLY_3V3

#define DEBUG 1

static ssize_t clk_enable_set(struct fpc_data *fpc, const char *buf, size_t count)
{
	return count;
}

static int hikey_init(struct fpc_data *fpc)
{
	struct device *dev = fpc->dev;
	dev_dbg(dev, "%s", __func__);
	return 0;
}

static int hikey_configure(struct fpc_data *fpc, int *irq_num, int *irq_trig_flags)
{
	struct device *dev = fpc->dev;
	int rc = 0;

	dev_dbg(dev, "%s", __func__);

	rc = gpio_direction_input(fpc->irq_gpio);
	if (rc != 0) {
		dev_err(dev, "gpio_direction_output failed for IRQ.\n");
		return rc;
	}

	*irq_num = gpio_to_irq(fpc->irq_gpio);
	*irq_trig_flags = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING;

	rc = gpio_direction_output(fpc->rst_gpio, 1);
	if (rc != 0) {
		dev_err(dev, "gpio_direction_output failed for RST.\n");
		return rc;
	}

	/* ensure it is high for a while */
	usleep_range(FPC_RESET_HIGH1_US, FPC_RESET_HIGH1_US + 100);

	/* set reset low */
	gpio_set_value(fpc->rst_gpio, 0);
	/* ensure it is low long enough */
	usleep_range(FPC_RESET_LOW_US, FPC_RESET_LOW_US + 100);

	return rc;
}

static struct fpc_gpio_info hikey_ops = {
	.init = hikey_init,
	.configure = hikey_configure,
	.get_val = gpio_get_value,
	.set_val = gpio_set_value,
	.clk_enable_set = clk_enable_set,
	.irq_handler = NULL,
};

static struct of_device_id hikey_of_match[] = {
	{ .compatible = "fpc,fpc_irq", },
	{}
};
MODULE_DEVICE_TABLE(of, hikey_of_match);

static int hikey_probe(struct platform_device *pldev)
{
	return fpc_probe(pldev, &hikey_ops);
}

static struct platform_driver hikey_driver = {
	.driver = {
		.name = "fpc_irq",
		.owner = THIS_MODULE,
		.of_match_table = hikey_of_match,
	},
	.probe = hikey_probe,
	.remove = fpc_remove
};

module_platform_driver(hikey_driver);

MODULE_LICENSE("GPL");

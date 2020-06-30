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
#include <linux/regulator/consumer.h>
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

static ssize_t clk_enable_set(struct fpc_data *fpc, const char *buf, size_t count)
{
	return count;
}

static int nexus_regulator_release(struct fpc_data *fpc)
{
	if (fpc->vdd_tx != NULL) {
		regulator_put(fpc->vdd_tx);
		fpc->vdd_tx = NULL;
	}

	fpc->power_enabled = false;

	return 0;
}

static int nexus_regulator_set(struct fpc_data *fpc, bool enable)
{
	int error = 0;
	struct device *dev = fpc->dev;

	if (fpc->vdd_tx == NULL) {
		dev_err(dev, "Regulators not set\n");
		return -EINVAL;
	}

	if (enable) {
		dev_dbg(dev, "%s on\n", __func__);

		/******
		Do we really need to set current?
		How would it affect the vibrator that shares this regulator?

		regulator_set_optimum_mode(fpc->vcc_spi,
		SUPPLY_SPI_REQ_CURRENT);
		*/

		error = (regulator_is_enabled(fpc->vdd_tx) == 0) ?
				regulator_enable(fpc->vdd_tx) : 0;

		if (error) {
			dev_err(dev,
				"Regulator vdd_tx enable failed, error=%d\n",
				error);
			goto out_err;
		}
	} else {
		dev_dbg(dev, "%s off\n", __func__);

		error = (fpc->power_enabled
			    && regulator_is_enabled(fpc->vdd_tx) > 0) ?
			    regulator_disable(fpc->vdd_tx) : 0;

		if (error) {
			dev_err(dev,
				"Regulator vdd_tx disable failed, error=%d\n",
				error);
			goto out_err;
		}
	}

	fpc->power_enabled = enable;

	return 0;

out_err: nexus_regulator_release(fpc);
	return error;
}

static int nexus_regulator_configure(struct fpc_data *fpc)
{
	int error = 0;
	struct device *dev = fpc->dev;

	dev_dbg(dev, "%s\n", __func__);

	fpc->vdd_tx = regulator_get(dev, "vdd_tx");
	if (IS_ERR(fpc->vdd_tx)) {
		error = PTR_ERR(fpc->vdd_tx);
		dev_err(dev, "Regulator get failed, vdd_tx, error=%d\n", error);
		goto supply_err;
	}

	if (regulator_count_voltages(fpc->vdd_tx) > 0) {
		error = regulator_set_voltage(fpc->vdd_tx,
		SUPPLY_TX_MIN, SUPPLY_TX_MAX);
		if (error) {
			dev_err(dev, "regulator set(tx) failed, error=%d\n",
				error);
			goto supply_err;
		}
	}

	return 0;

supply_err: nexus_regulator_release(fpc);
	return error;
}

static int nexus_bezel_setup(struct fpc_data *fpc)
{
	int error = 0;
	struct device *dev = fpc->dev;

	/* Determine if we should use external regulator for
	   power supply to the bezel. */
	if (fpc->use_regulator_for_bezel) {
		error = nexus_regulator_configure(fpc);
		if (error) {
			dev_err(dev,
				"fpc_probe - regulator configuration failed.\n");
			goto err;
		}

		error = nexus_regulator_set(fpc, true);
		if (error) {
			dev_err(dev,
				"fpc_probe - regulator enable failed.\n");
			goto err;
		}
	}

err:
	/* release reset */
	gpio_set_value(fpc->rst_gpio, 1);
	/* ensure it is high for a while */
	usleep_range(FPC_RESET_HIGH2_US, FPC_RESET_HIGH2_US + 100);

	return error;
}

static int nexus_power_on(struct fpc_data *fpc)
{
	struct device *dev = fpc->dev;
	struct device_node *node = dev->of_node;
	int rc;

	const void *use_regulator_for_bezel_prop = of_get_property(node,
			"vdd_tx-supply", NULL);

	/* TODO: use get named gpio */
	rc = gpio_request(57, NULL);
	if (rc) {
		dev_err(dev, "Requesting GPIO for power (GPIO#57) failed with %d.\n", rc);
		rc = -EIO;
		return rc;
	}
	rc = gpio_direction_output(57, 1);
	if (rc) {
		dev_err(dev, "Could not set GPIO#57 as output.\n");
		rc = -EIO;
		return rc;
	}

	fpc->use_regulator_for_bezel = use_regulator_for_bezel_prop ? 1 : 0;

	return rc;
}

static int nexus_init(struct fpc_data *fpc)
{
	return 0;
}

static int nexus_configure(struct fpc_data *fpc, int *irq_num, int *irq_trig_flags)
{
	struct device *dev = fpc->dev;
	int rc = 0;

	rc = nexus_power_on(fpc);
	if (rc != 0)
		return rc;

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

	rc = nexus_bezel_setup(fpc);

	return rc;
}

static struct fpc_gpio_info nexus_ops = {
	.init = nexus_init,
	.configure = nexus_configure,
	.get_val = gpio_get_value,
	.set_val = gpio_set_value,
	.clk_enable_set = clk_enable_set,
	.irq_handler = NULL,
};

static struct of_device_id nexus_of_match[] = {
	{ .compatible = "fpc,fpc_irq", },
	{}
};
MODULE_DEVICE_TABLE(of, nexus_of_match);

static int nexus_probe(struct platform_device *pldev)
{
	return fpc_probe(pldev, &nexus_ops);
}

static struct platform_driver nexus_driver = {
	.driver = {
		.name	= "fpc_irq",
		.owner	= THIS_MODULE,
		.of_match_table = nexus_of_match,
	},
	.probe	= nexus_probe,
	.remove	= fpc_remove
};

module_platform_driver(nexus_driver);

MODULE_LICENSE("GPL");

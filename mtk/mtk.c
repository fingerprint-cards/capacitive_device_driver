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

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

#include <mach/mt_gpio.h>
#include <mach/eint.h>
#include <cust_eint.h>
#include <mach/mt_clkmgr.h>

#include "fpc_irq.h"

#define GPIO_CONFIGURE_IN(pin)  mt_set_gpio_mode(pin, GPIO_MODE_00) | mt_set_gpio_dir(pin, GPIO_DIR_IN)
#define GPIO_CONFIGURE_OUT(pin, default) mt_set_gpio_mode(pin, GPIO_MODE_00) | mt_set_gpio_dir(pin, GPIO_DIR_OUT) | mt_set_gpio_out(pin, default)

static void mt_gpio_set(unsigned gpio, int val)
{
	mt_set_gpio_out(gpio, val);
}

static int mt_gpio_get(unsigned gpio)
{
	return mt_get_gpio_in(gpio);
}

void mt_spi_enable_clk(void)
{
	enable_clock(MT_CG_PERI_SPI0, "spi");
}

void mt_spi_disable_clk(void)
{
	disable_clock(MT_CG_PERI_SPI0, "spi");
}

static ssize_t clk_enable_set(struct fpc_data *fpc, const char *buf, size_t count)
{
	if (buf[0] == '1')
		mt_spi_enable_clk();
	else if (buf[0] == '0')
		mt_spi_disable_clk();
	return count;
}

static void mtk_irq_handler(int irq, struct fpc_data *fpc)
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

static int mtk_configure(struct fpc_data *fpc, int *irq_num, int *irq_trig_flags)
{
	struct device *dev = fpc->dev;
	int rc = 0;
	int irqf;

	/* Configure the direction of the gpios */
	rc = GPIO_CONFIGURE_IN(fpc->irq_gpio);
		rc = GPIO_CONFIGURE_OUT( fpc->rst_gpio, 1 );

	if (rc < 0) {
		dev_err(&fpc->pldev->dev,
			"gpio_direction_output failed for RST.\n");
		return rc;
	}

	rc = mt_set_gpio_pull_enable(fpc->irq_gpio, GPIO_PULL_DISABLE);
	if (rc != 0)
		printk(KERN_ERR "mt_set_gpio_pull_enable (eint) failed.error=%d\n", rc);

	mt_eint_set_hw_debounce(fpc->irq_gpio, 1);
	*irq_num = mt_gpio_to_irq(fpc->irq_gpio);

	/* The Mediatek platform does not support triggering at both rising
	 * and falling edge at the same time so we configure to trigger on
	 * high level and then reconfigure the interrupt in the interrupt handler.
	 */
	*irq_trig_flags = IRQF_TRIGGER_HIGH;

	return rc;
}

static int mtk_init(struct fpc_data *fpc)
{
	struct device *dev = fpc->dev;

	mt_spi_enable_clk();
	dev_dbg(dev, "spi enabled\n");

	return 0;
}

static struct fpc_gpio_info mtk_ops = {
	.init = mtk_init,
	.configure = mtk_configure,
	.get_val = mt_gpio_get,
	.set_val = mt_gpio_set,
	.clk_enable_set = clk_enable_set,
	.irq_handler = mtk_irq_handler,
};

static struct of_device_id mtk_of_match[] = {
	{ .compatible = "fpc,fpc_irq", },
	{}
};
MODULE_DEVICE_TABLE(of, mtk_of_match);

static int mtk_probe(struct platform_device *pldev)
{
	int rc;

	rc = fpc_probe(pldev, &mtk_ops);

	return rc;
}

static struct platform_driver mtk_driver = {
	.driver = {
		.name	= "fpc_irq",
		.owner	= THIS_MODULE,
		.of_match_table = mtk_of_match,
	},
	.probe	= mtk_probe,
	.remove	= fpc_remove
};

module_platform_driver(mtk_driver);

MODULE_LICENSE("GPL");

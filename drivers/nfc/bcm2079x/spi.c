/*
 * SPI phy driver for Broadcom BCM2079x NFC Chip
 *
 * Copyright (C) 2013 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.
 */

#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/gpio.h>
#include <linux/platform_data/bcm2079x.h>
#include <net/nfc/nfc.h>

#include "bcm2079x.h"

#define STATE_HIGH		1
#define STATE_LOW		0

#define NFC_REQ_ACTIVE_STATE	STATE_LOW

struct bcm2079x_spi_phy {
	struct spi_device *spi;
	struct nci_spi *nspi;
	struct nci_dev *ndev;

	struct mutex rw_mutex;
	struct completion *write_handshake_completion;

	unsigned int en_gpio;
	unsigned int irq_gpio;
	unsigned int wake_gpio;
};

static irqreturn_t bcm2079x_spi_irq_thread_handler(int irq, void *phy_id)
{
	struct bcm2079x_spi_phy *phy = phy_id;
	struct sk_buff *skb;
	int r = 0;

	mutex_lock(&phy->rw_mutex);

	if (phy->write_handshake_completion) {
		/*
		 * If we're currently handshaking signals to synchronize
		 * sending data, this interrupt is the GO.
		 * Note the possible race condition: interrupt can have
		 * occurred before bcm2079x_spi_write was started and thus
		 * be a real receive interrupt. In that case, the NCI spec
		 * says that the host always wins, so we always go with the
		 * send.
		 */
		complete(phy->write_handshake_completion);
		phy->write_handshake_completion = NULL;
	} else {
		skb = nci_spi_read(phy->nspi);
		if (!skb)
			r = -EIO;
		if (r) {
			/* TODO: device should probably go into a dead state */
			dev_err(&phy->spi->dev,
				"%s: nci_spi_recv_frame result=%d\n", __func__,
				r);
		} else {
			/* remove frame type */
			skb_pull(skb, 1);
			r = nci_recv_frame(phy->ndev, skb);
		}
	}

	mutex_unlock(&phy->rw_mutex);

	return IRQ_HANDLED;
}

static int bcm2079x_spi_write(void *phy_id, struct sk_buff *skb)
{
	struct bcm2079x_spi_phy *phy = phy_id;
	struct completion write_handshake_completion;
	int r;

	init_completion(&write_handshake_completion);

	mutex_lock(&phy->rw_mutex);
	phy->write_handshake_completion = &write_handshake_completion;
	mutex_unlock(&phy->rw_mutex);

	*skb_push(skb, 1) = FRAME_TYPE_NCI;
	r = nci_spi_send(phy->nspi, &write_handshake_completion, skb);

	mutex_lock(&phy->rw_mutex);
	phy->write_handshake_completion = NULL;
	mutex_unlock(&phy->rw_mutex);

	return r;
}

static int bcm2079x_spi_enable(void *phy_id)
{
	struct bcm2079x_spi_phy *phy = phy_id;

	gpio_set_value(phy->en_gpio, 1);

	return 0;
}

static void bcm2079x_spi_disable(void *phy_id)
{
	struct bcm2079x_spi_phy *phy = phy_id;

	gpio_set_value(phy->en_gpio, 0);
}

static struct nfc_phy_ops spi_phy_ops = {
	.write = bcm2079x_spi_write,
	.enable = bcm2079x_spi_enable,
	.disable = bcm2079x_spi_disable,
};

static int bcm2079x_spi_probe(struct spi_device *spi)
{
	int r;
	struct bcm2079x_spi_phy *phy;
	struct bcm2079x_platform_data *pdata;
	unsigned int spi_delay_us;

	pdata = spi->dev.platform_data;

	dev_info(&spi->dev, "%s\n", __func__);

	if (pdata == NULL) {
		dev_err(&spi->dev, "%s: no platform data\n", __func__);
		return -ENODEV;
	}

	phy = devm_kzalloc(&spi->dev, sizeof(struct bcm2079x_spi_phy),
			   GFP_KERNEL);
	if (!phy) {
		dev_err(&spi->dev,
			"%s: cannot allocate bcm2079x_spi_phy\n", __func__);
		return -ENOMEM;
	}

	mutex_init(&phy->rw_mutex);

	phy->spi = spi;
	spi_set_drvdata(spi, phy);

	r = devm_gpio_request(&spi->dev, pdata->irq_gpio, "nfc_spi_int");
	if (r) {
		dev_err(&spi->dev,
			"%s: cannot request irq_gpio (r=%d)\n", __func__, r);
		goto exit_free_mutex;
	}

	r = devm_gpio_request(&spi->dev, pdata->en_gpio, "nfc_en");
	if (r) {
		dev_err(&spi->dev,
			"%s: cannot request en_gpio (r=%d)\n", __func__, r);
		goto exit_free_mutex;
	}

	/* Wake pin can be joined with SPI_CSN */
	if (pdata->wake_gpio > 0) {
		r = devm_gpio_request(&spi->dev, pdata->wake_gpio, "nfc_wake");
		if (r) {
			dev_err(&spi->dev,
				"%s: cannot request wake_gpio (r=%d)\n",
				__func__, r);
			goto exit_free_mutex;
		}
		gpio_direction_output(pdata->wake_gpio, 0);
		gpio_set_value(pdata->wake_gpio, 0);
	}

	gpio_direction_output(pdata->en_gpio, 0);
	gpio_direction_input(pdata->irq_gpio);
	gpio_set_value(pdata->en_gpio, 0);

	phy->wake_gpio = pdata->wake_gpio;
	phy->irq_gpio = pdata->irq_gpio;
	phy->en_gpio = pdata->en_gpio;

	r = request_threaded_irq(spi->irq, NULL,
				 bcm2079x_spi_irq_thread_handler,
				 IRQF_TRIGGER_FALLING, spi->modalias, phy);
	if (r < 0) {
		dev_err(&spi->dev,
			"%s: cannot request irq (r=%d)\n", __func__, r);
		goto exit_free_mutex;
	}

	/* tailroom would be NCI_SPI_CRC_LEN if spi ack mode was crc enabled */
	r = bcm2079x_probe(phy, &spi_phy_ops, NCI_SPI_HDR_LEN + 1, 0,
			   &phy->ndev);
	if (r)
		goto exit_free_irq;

	spi_delay_us = spi->max_speed_hz == 0 ? 5 : 1000000 / spi->max_speed_hz;
	if (spi_delay_us == 0)
		spi_delay_us = 1000;

	phy->nspi = nci_spi_allocate_spi(spi, NCI_SPI_CRC_DISABLED,
					 spi_delay_us, phy->ndev);
	if (phy->nspi == NULL) {
		r = -ENOMEM;
		goto exit_remove;
	}

	return 0;

exit_remove:
	bcm2079x_remove(phy->ndev);

exit_free_irq:
	free_irq(spi->irq, phy);

exit_free_mutex:
	mutex_destroy(&phy->rw_mutex);

	return r;
}

static int bcm2079x_spi_remove(struct spi_device *spi)
{
	struct bcm2079x_spi_phy *phy;

	phy = (struct bcm2079x_spi_phy *) spi_get_drvdata(spi);

	bcm2079x_remove(phy->ndev);

	free_irq(spi->irq, phy);

	mutex_destroy(&phy->rw_mutex);

	return 0;
}

static struct spi_driver bcm2079x_driver = {
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "bcm2079x-spi",
		   },
	.probe = bcm2079x_spi_probe,
	.remove = bcm2079x_spi_remove,
};

module_spi_driver(bcm2079x_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(DRIVER_DESC);

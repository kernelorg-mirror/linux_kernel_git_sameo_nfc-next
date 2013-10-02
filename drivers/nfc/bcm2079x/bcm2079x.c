/*
 * NCI based Driver for Broadcom BCM2079x NFC Chip
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

#include <net/nfc/nfc.h>
#include <net/nfc/nci_core.h>

#include "bcm2079x.h"

struct bcm2079x_info {
	struct nfc_phy_ops *phy_ops;
	void *phy_id;

	struct nci_dev *ndev;
};

static int bcm2079x_open(struct nci_dev *ndev)
{
	struct bcm2079x_info *info = nci_get_drvdata(ndev);

	return info->phy_ops->enable(info->phy_id);
}

static int bcm2079x_close(struct nci_dev *ndev)
{
	struct bcm2079x_info *info = nci_get_drvdata(ndev);

	info->phy_ops->disable(info->phy_id);

	return 0;
}

static int bcm2079x_send(struct nci_dev *ndev, struct sk_buff *skb)
{
	struct bcm2079x_info *info = nci_get_drvdata(ndev);

	return info->phy_ops->write(info->phy_id, skb);
}

static struct nci_ops bcm2079x_ops = {
	.open = bcm2079x_open,
	.close = bcm2079x_close,
	.send = bcm2079x_send,
};

int bcm2079x_probe(void *phy_id, struct nfc_phy_ops *phy_ops,
		   int phy_headroom, int phy_tailroom, struct nci_dev **ndev)
{
	struct bcm2079x_info *info;
	u32 protocols;
	int r;

	info = kzalloc(sizeof(struct bcm2079x_info), GFP_KERNEL);
	if (!info) {
		pr_err("Cannot allocate memory for bcm2079x_info.\n");
		r = -ENOMEM;
		goto err_info_alloc;
	}

	info->phy_ops = phy_ops;
	info->phy_id = phy_id;

	protocols = NFC_PROTO_JEWEL_MASK |
		    NFC_PROTO_MIFARE_MASK |
		    NFC_PROTO_FELICA_MASK |
		    NFC_PROTO_ISO14443_MASK |
		    NFC_PROTO_ISO14443_B_MASK |
		    NFC_PROTO_NFC_DEP_MASK;

	info->ndev = nci_allocate_device(&bcm2079x_ops, protocols, phy_headroom,
					 phy_tailroom);
	if (!info->ndev) {
		pr_err("Cannot allocate nfc ndev.\n");
		r = -ENOMEM;
		goto err_alloc_ndev;
	}

	nci_set_drvdata(info->ndev, info);

	r = nci_register_device(info->ndev);
	if (r)
		goto err_regdev;

	*ndev = info->ndev;

	return 0;

err_regdev:
	nci_free_device(info->ndev);

err_alloc_ndev:
	kfree(info);

err_info_alloc:
	return r;
}
EXPORT_SYMBOL(bcm2079x_probe);

void bcm2079x_remove(struct nci_dev *ndev)
{
	struct bcm2079x_info *info = nci_get_drvdata(ndev);

	nci_unregister_device(ndev);
	nci_free_device(ndev);
	kfree(info);
}
EXPORT_SYMBOL(bcm2079x_remove);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(DRIVER_DESC);

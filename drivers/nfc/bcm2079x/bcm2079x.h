/*
 * Copyright (C) 2013 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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

#ifndef __LOCAL_BCM2079X_H_
#define __LOCAL_BCM2079X_H_

#include <net/nfc/nci_core.h>

#define DRIVER_DESC "NFC driver for BCM2079x"

#define FRAME_TYPE_NCI 0x10

int bcm2079x_probe(void *phy_id, struct nfc_phy_ops *phy_ops,
		   int phy_headroom, int phy_tailroom, struct nci_dev **ndev);

void bcm2079x_remove(struct nci_dev *ndev);

#endif /* __LOCAL_BCM2079X_H_ */

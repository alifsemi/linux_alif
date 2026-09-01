/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * cdc_lvdscon.h  --  CDC Display Controller LVDS Connector
 *
 * Copyright (C) 2017 TES Electronic Solutions GmbH
 * Author: Christian Thaler <christian.thaler@tes-dst.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef CDC_LVDSCON_H_
#define CDC_LVDSCON_H_

#include "cdc_encoder.h"

int cdc_lvds_connector_init(struct cdc_device *cdc, struct cdc_encoder *enc,
			    struct device_node *np);

#endif /* CDC_LVDSCON_H_ */

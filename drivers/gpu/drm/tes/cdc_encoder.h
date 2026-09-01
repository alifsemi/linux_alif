/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * cdc_encoder.h  --  CDC Display Controller Encoder
 *
 * Copyright (C) 2017 TES Electronic Solutions GmbH
 * Author: Christian Thaler <christian.thaler@tes-dst.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __CDC_ENCODER_H__
#define __CDC_ENCODER_H__

#include <drm/drm_crtc.h>
#include <drm/drm_encoder.h>

struct cdc_device;
struct cdc_lvdsenc;

struct cdc_encoder {
	struct drm_encoder base;
	struct cdc_lvdsenc *lvds;
};

#define to_cdc_encoder(e) \
	container_of(e, struct cdc_encoder, base)
#define cdc_encoder_to_drm_encoder(e)  (&(e)->base)

struct cdc_connector {
	struct drm_connector connector;
	struct cdc_encoder *encoder;
	struct cdc_device *cdc;
};

#define to_cdc_connector(c) \
	container_of(c, struct cdc_connector, connector)

int cdc_encoder_init(struct cdc_device *cdc, u32 enc_type,
		     struct device_node *enc_node, struct device_node *con_node);

#endif

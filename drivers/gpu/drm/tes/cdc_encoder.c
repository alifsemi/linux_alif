// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * cdc_encoder.c  --  CDC Display Controller Encoder
 *
 * Copyright (C) 2017 TES Electronic Solutions GmbH
 * Author: Christian Thaler <christian.thaler@tes-dst.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_encoder.h>

#include "cdc_regs.h"
#include "cdc_drv.h"
#include "cdc_kms.h"
#include "cdc_lvdscon.h"
#include "cdc_encoder.h"

static void cdc_encoder_enable(struct drm_encoder *encoder)
{
}

static void cdc_encoder_disable(struct drm_encoder *encoder)
{
}

static int cdc_encoder_atomic_check(struct drm_encoder *encoder,
				    struct drm_crtc_state *crtc_state,
	struct drm_connector_state *conn_state)
{
	return 0;
}

static const struct drm_encoder_helper_funcs encoder_helper_funcs = {
	.enable = cdc_encoder_enable,
	.disable = cdc_encoder_disable,
	.atomic_check =	cdc_encoder_atomic_check,
};

static const struct drm_encoder_funcs encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

int cdc_encoder_init(struct cdc_device *cdc, u32 enc_type,
		     struct device_node *enc_node, struct device_node *con_node)
{
	struct cdc_encoder *enc;
	struct drm_encoder *encoder;
	struct drm_bridge *bridge = NULL;
	int ret = 0;

	enc = devm_kzalloc(cdc->dev, sizeof(*enc), GFP_KERNEL);
	if (!enc)
		return -ENOMEM;

	encoder = cdc_encoder_to_drm_encoder(enc);

	if (enc_node) {
		dev_dbg(cdc->dev, "initializing encoder %pOF for output %u\n",
			enc_node, enc_type);

		/* Locate the DRM bridge from the encoder DT node. */
		bridge = of_drm_find_bridge(enc_node);
		if (!bridge) {
			dev_err(cdc->dev, "could not find bridge for %pOF\n",
				enc_node);
			ret = -EPROBE_DEFER;
			goto done;
		}

		dev_dbg(cdc->dev, "found bridge %pOF for encoder %pOF\n",
			bridge->of_node, enc_node);
	} else {
		dev_dbg(cdc->dev,
			"initializing internal encoder for output %u\n",
			enc_type);
	}

	encoder->possible_crtcs = 1;
	encoder->possible_clones = 0;

	ret = drm_encoder_init(cdc->ddev, encoder, &encoder_funcs,
			       DRM_MODE_ENCODER_DPI, NULL);
	if (ret < 0)
		goto done;

	dev_dbg(cdc->dev, "initialized encoder %s\n", encoder->name);
	drm_encoder_helper_add(encoder, &encoder_helper_funcs);

	if (bridge) {
		/*
		 * Attach the bridge to the encoder. The bridge will create the
		 * connector.
		 */
		ret = drm_bridge_attach(encoder, bridge, NULL, 0);
		if (ret) {
			dev_err(cdc->dev, "could not attach bridge to encoder(err=%d)\n", ret);
			drm_encoder_cleanup(encoder);
			return ret;
		}
	} else {
		/* There's no bridge, create the connector manually. */
		ret = cdc_lvds_connector_init(cdc, enc, con_node);
	}

done:
	if (ret < 0) {
		dev_err(cdc->dev, "could not initialize encoder %pOF\n",
			enc_node);
		if (encoder->dev)
			drm_encoder_cleanup(encoder);
		devm_kfree(cdc->dev, enc);
	}

	return ret;
}

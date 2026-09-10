// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * cdc_kms.c  --  CDC Display Controller Mode Setting
 *
 * Copyright (C) 2017 TES Electronic Solutions GmbH
 * Author: Christian Thaler <christian.thaler@tes-dst.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/clk.h>
#include <linux/mutex.h>

#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#include <video/display_timing.h>
#include <video/of_display_timing.h>
#include <video/videomode.h>

#include <linux/of_graph.h>
#include <linux/wait.h>

#include "cdc_regs.h"
#include "cdc_drv.h"
#include "cdc_kms.h"
#include "cdc_crtc.h"
#include "cdc_plane.h"
#include "cdc_encoder.h"

/*******************************************************************************
 * Format helper
 *
 * Note that the format id is configuration dependent!
 */
static const struct cdc_format cdc_formats[] = {
	{ 0, DRM_FORMAT_ARGB8888, 32 },
	{ 0, DRM_FORMAT_XRGB8888, 32 },
	{ 1, DRM_FORMAT_RGB888, 24 },
	{ 2, DRM_FORMAT_RGB565, 16 },
	{ 3, DRM_FORMAT_RGBA8888, 32},
	{ 6, DRM_FORMAT_ARGB1555, 16 },
	{ 7, DRM_FORMAT_ARGB4444, 16 },
};

const struct cdc_format *
cdc_format_info(__u32 drm_fourcc)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cdc_formats); ++i) {
		if (cdc_formats[i].fourcc == drm_fourcc)
			return &cdc_formats[i];
	}
	return NULL;
}

static struct drm_framebuffer *cdc_fb_create(struct drm_device *dev,
					     struct drm_file *file_priv,
					     const struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_framebuffer *fb;
	struct drm_gem_dma_object *gem;
	const struct cdc_format *format;

	dev_dbg(dev->dev, "creating frame buffer %dx%d(%08x)\n",
		mode_cmd->width, mode_cmd->height, mode_cmd->pixel_format);

	format = cdc_format_info(mode_cmd->pixel_format);
	if (!format) {
		dev_err(dev->dev, "requested unsupported pixel format %08x\n",
			mode_cmd->pixel_format);
		return ERR_PTR(-EINVAL);
	}

	if (mode_cmd->pitches[0] >= CDC_MAX_PITCH) {
		dev_err(dev->dev, "requested too large pitch of %u\n",
			mode_cmd->pitches[0]);
		return ERR_PTR(-EINVAL);
	}

	fb = drm_gem_fb_create(dev, file_priv, mode_cmd);
	if (IS_ERR(fb))
		return fb;

	gem = drm_fb_dma_get_gem_obj(fb, 0);
	if (gem)
		dev_dbg(dev->dev, "FB addr is %pad\n", &gem->dma_addr);

	return fb;
}

static const struct drm_mode_config_helper_funcs cdc_mode_config_helpers = {
	/*
	 * cdc_hw_set_timing() clears layer enables. Planes must be
	 * programmed after CRTC enable, which commit_tail_rpm does.
	 * Scanout itself is started from cdc_crtc_atomic_flush().
	 */
	.atomic_commit_tail = drm_atomic_helper_commit_tail_rpm,
};

static const struct drm_mode_config_funcs cdc_mode_config_funcs = {
	.fb_create = cdc_fb_create,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static int cdc_encoders_find_and_init(struct cdc_device *cdc,
				      struct of_endpoint *ep)
{
	__u32 enc_type = DRM_MODE_ENCODER_NONE;
	struct device_node *connector = NULL;
	struct device_node *encoder = NULL;
	struct device_node *ep_node = NULL;
	struct device_node *entity_ep_node;
	struct device_node *entity;
	int ret;

	/*
	 * Locate the connected entity and infer its type from the number of
	 * endpoints.
	 */
	entity = of_graph_get_remote_port_parent(ep->local_node);
	if (!entity) {
		dev_err(cdc->dev, "unconnected endpoint %s, skipping\n",
			ep->local_node->full_name);
		return -ENODEV;
	}

	if (!of_device_is_available(entity)) {
		dev_dbg(cdc->dev,
			"connected entity %pOF is disabled, skipping\n",
			entity);
		return -ENODEV;
	}

	dev_dbg(cdc->dev, "endpoint is connected to %s\n", entity->full_name);

	entity_ep_node = of_graph_get_remote_endpoint(ep->local_node);

	for_each_endpoint_of_node(entity, ep_node) {
		if (ep_node == entity_ep_node)
			continue;

		/*
		 * We've found one endpoint other than the input, this must
		 * be an encoder. Locate the connector.
		 */
		encoder = entity;
		connector = of_graph_get_remote_port_parent(ep_node);
		of_node_put(ep_node);

		if (!connector) {
			dev_warn(cdc->dev,
				 "no connector for encoder %s, skipping\n",
				 encoder->full_name);
			of_node_put(entity_ep_node);
			of_node_put(encoder);
			return -ENODEV;
		}

		break;
	}

	of_node_put(entity_ep_node);

	if (!encoder) {
		/*
		 * If no encoder has been found the entity must be the
		 * connector.
		 */
		connector = entity;
	}

	ret = cdc_encoder_init(cdc, enc_type, encoder, connector);

	if (ret && ret != -EPROBE_DEFER)
		dev_warn(cdc->dev,
			 "failed to initialize encoder %s(%d), skipping\n",
			 encoder->full_name, ret);

	of_node_put(encoder);
	of_node_put(connector);

	return ret;
}

static int cdc_encoders_init(struct cdc_device *cdc)
{
	struct device_node *np = cdc->dev->of_node;
	struct device_node *ep_node;

	dev_dbg(cdc->dev, "initializing encoder for %s\n", np->full_name);

	/*
	 * CDC only has one endpoint. Now create the encoder for it.
	 */
	for_each_endpoint_of_node(np, ep_node) {
		struct of_endpoint ep;
		int ret;

		ret = of_graph_parse_endpoint(ep_node, &ep);
		if (ret < 0) {
			of_node_put(ep_node);
			return ret;
		}

		/* Process output pipeline. */
		ret = cdc_encoders_find_and_init(cdc, &ep);
		if (ret < 0) {
			if (ret == -EPROBE_DEFER) {
				of_node_put(ep_node);
				return ret;
			}

			continue;
		}
	}

	return 0;
}

int cdc_modeset_init(struct cdc_device *cdc)
{
	struct drm_device *dev = cdc->ddev;
	int ret;

	drm_mode_config_init(dev);

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;
	dev->mode_config.max_width = CDC_MAX_WIDTH;
	dev->mode_config.max_height = CDC_MAX_HEIGHT;
	dev->mode_config.funcs = &cdc_mode_config_funcs;
	dev->mode_config.helper_private = &cdc_mode_config_helpers;

	/* Initialize vertical blanking interrupts handling. Start with vblank
	 * disabled for all CRTCs.
	 */
	ret = drm_vblank_init(dev, 1 /* num_crtcs */);
	if (ret < 0) {
		dev_err(cdc->dev, "failed to initialize vblank\n");
		goto err_config;
	}

	ret = cdc_planes_init(cdc);
	if (ret)
		goto err_config;

	ret = cdc_crtc_create(cdc);
	if (ret)
		goto err_config;

	ret = cdc_encoders_init(cdc);
	if (ret)
		goto err_config;

	drm_mode_config_reset(dev);
	drm_kms_helper_poll_init(dev);

	return 0;

err_config:
	drm_mode_config_cleanup(dev);
	return ret;
}

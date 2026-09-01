/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * cdc_drv.h  --  CDC Display Controller DRM driver
 *
 * Copyright (C) 2017 TES Electronic Solutions GmbH
 * Author: Christian Thaler <christian.thaler@tes-dst.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __CDC_DRV_H__
#define __CDC_DRV_H__
#include <linux/kernel.h>
#include <video/videomode.h>
#include <drm/drm_crtc.h>
#include <drm/drm_device.h>

#include "cdc_deswizzle.h"

/* CDC HW limitations */
#define CDC_MAX_WIDTH  2047u
#define CDC_MAX_HEIGHT 2047u
#define CDC_MAX_PITCH  8192u
/* HWRM DPI pixel clock maximum (not SYST_ACLK). */
#define CDC_MAX_PIXCLK_KHZ 50000u
#define CDC_OFFSET_LAYER 0x40

struct cdc_device;
struct cdc_format;
struct drm_pending_vblank_event;
struct altera_pll;

struct cdc_plane {
	struct drm_plane plane;
	struct cdc_device *cdc;
	int hw_idx;
	bool enabled;
	bool used;

	u8 pixel_format;
	u16 fb_width;
	u16 fb_height;
	s32 fb_pitch;
	u16 window_width;
	u16 window_height;
	u16 window_x;
	u16 window_y;
	u32 control;
	u8 alpha;
};

struct cdc_device {
	struct drm_crtc crtc;
	struct device *dev;
	struct drm_device *ddev;

	void __iomem *mmio;

	/* HW context */
	struct {
		int layer_count;
		bool enabled;
		bool shadow_regs;
		u32 irq_enabled;
		u32 bus_width; /* bus width in bytes */
	} hw;

	struct clk *pclk;
	struct clk *pix_clk;
	struct drm_pending_vblank_event *event;
	wait_queue_head_t flip_wait;
	struct cdc_plane *planes;

	int dpms;
	bool wait_for_vblank;
	bool early_poll; // did a poll occur before FBDEV was setup?
	bool started;
	bool neg_blank;
	bool neg_pixclk;

	bool fifo_underrun;

	// plane properties
	struct drm_property *alpha;

	struct {
		wait_queue_head_t wait;
		u32 pending;
	} commit;

	struct dswz_device *dswz; // NULL if deswizzler is not available

	/* FIXME HACK for MesseDemo */
	unsigned int irq_stat;
	spinlock_t irq_slck; /* protects irq_stat */
	wait_queue_head_t irq_waitq;
};

int cdc_init_irq(struct cdc_device *cdc);

#endif

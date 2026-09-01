/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * cdc_kms.h  --  CDC Display Controller Mode Setting
 *
 * Copyright (C) 2017 TES Electronic Solutions GmbH
 * Author: Christian Thaler <christian.thaler@tes-dst.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __CDC_KMS_H__
#define __CDC_KMS_H__
#include <linux/types.h>

struct cdc_device;
struct drm_device;
struct drm_file;
struct drm_mode_create_dumb;

struct cdc_format {
	unsigned int cdc_hw_format;
	u32 fourcc;
	unsigned int bpp;
};

int cdc_modeset_init(struct cdc_device *cdc);
int cdc_dumb_create(struct drm_device *dev, struct drm_file *file,
		    struct drm_mode_create_dumb *args);
const struct cdc_format *cdc_format_info(__u32 drm_fourcc);

#endif

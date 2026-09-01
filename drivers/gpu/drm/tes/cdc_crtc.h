/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * cdc_crtc.h  --  CDC Display Controller CRTC
 *
 * Copyright (C) 2017 TES Electronic Solutions GmbH
 * Author: Christian Thaler <christian.thaler@tes-dst.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef CDC_CRTC_H_
#define CDC_CRTC_H_

struct cdc_device;
struct drm_crtc;
struct drm_file;
struct drm_display_mode;
enum drm_mode_status;

int
cdc_crtc_create(struct cdc_device *cdc);
int
cdc_crtc_start(struct drm_crtc *crtc);
void
cdc_crtc_stop(struct drm_crtc *crtc);
void
cdc_crtc_set_vblank(struct cdc_device *cdc, bool enable);
void
cdc_crtc_irq(struct drm_crtc *crtc);
void
cdc_crtc_cancel_page_flip(struct drm_crtc *crtc, struct drm_file *file);
enum drm_mode_status
cdc_crtc_mode_valid(struct drm_crtc *crtc, const struct drm_display_mode *mode);

#endif /* CDC_CRTC_H_ */

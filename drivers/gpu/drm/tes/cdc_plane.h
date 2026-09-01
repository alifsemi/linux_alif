/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * cdc_plane.h  --  CDC Display Controller Plane
 *
 * Copyright (C) 2017 TES Electronic Solutions GmbH
 * Author: Christian Thaler <christian.thaler@tes-dst.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef CDC_PLANE_H_
#define CDC_PLANE_H_

struct cdc_device;
struct cdc_plane;
struct drm_plane;

struct cdc_plane_state {
	struct drm_plane_state state;

	unsigned int alpha;
};

static inline struct cdc_plane_state
*to_cdc_plane_state(struct drm_plane_state *state)
{
	return container_of(state, struct cdc_plane_state, state);
}

int cdc_planes_init(struct cdc_device *cdc);
void cdc_plane_setup_fb(struct cdc_plane *plane);
void cdc_plane_setup_window(struct drm_plane *plane);
int cdc_plane_disable(struct drm_plane *plane);

#endif /* CDC_PLANE_H_ */

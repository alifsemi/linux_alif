/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * cdc_hw_helpers.h  --  CDC Display Controller Hardware Interface
 *
 * Copyright (C) 2017 TES Electronic Solutions GmbH
 * Author: Christian Thaler <christian.thaler@tes-dst.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef CDC_HW_HELPERS_H_
#define CDC_HW_HELPERS_H_

#include <linux/types.h>

#include "cdc_drv.h"
#include "cdc_regs.h"

void cdc_hw_set_pixel_format(struct cdc_device *cdc, int layer, u8 format);
void cdc_hw_set_blend_mode(struct cdc_device *cdc, int layer,
			   enum cdc_blend_factor a_factor1, enum cdc_blend_factor a_factor2);
void cdc_hw_set_window(struct cdc_device *cdc, int layer, u16 start_x,
		       u16 start_y, u16 width, u16 height, s16 pitch);
void cdc_hw_set_cb_address(struct cdc_device *cdc, int layer, dma_addr_t address);
void cdc_hw_layer_set_enabled(struct cdc_device *cdc, int layer, bool enable);
void cdc_hw_reset_registers(struct cdc_device *cdc);
bool cdc_hw_trigger_shadow_reload(struct cdc_device *cdc, bool in_vblank);
void cdc_hw_set_timing(struct cdc_device *cdc, u16 a_h_sync, u16 a_h_b_porch,
		       u16 a_h_width, u16 a_h_f_porch, u16 a_v_sync, u16 a_v_b_porch,
	u16 a_v_width, u16 a_v_f_porch, bool a_neg_hsync, bool a_neg_vsync,
	bool a_neg_blank, bool a_inv_clk);
void cdc_hw_set_enabled(struct cdc_device *cdc, bool enable);
void cdc_hw_set_background_color(struct cdc_device *cdc, u32 color);
void cdc_hw_layer_set_cb_size(struct cdc_device *cdc, int layer, u16 width,
			      u16 height, s16 pitch);
void cdc_hw_layer_set_constant_alpha(struct cdc_device *cdc, int layer, u8 alpha);

#endif /* CDC_HW_HELPERS_H_ */

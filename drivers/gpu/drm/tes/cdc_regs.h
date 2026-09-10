/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * cdc_regs.h  --  CDC Display Controller register definitions
 *
 * Copyright (C) 2007 - 2017 TES Electronic Solutions GmbH
 * Author: Christian Thaler <christian.thaler@tes-dst.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _CDC_REGS_H_
#define _CDC_REGS_H_

#include <linux/types.h>

// global registers
#define CDC_REG_GLOBAL_HW_REVISION          0x00
#define CDC_REG_GLOBAL_LAYER_COUNT          0x01
#define CDC_REG_GLOBAL_SYNC_SIZE            0x02
#define CDC_REG_GLOBAL_BACK_PORCH           0x03
#define CDC_REG_GLOBAL_ACTIVE_WIDTH         0x04
#define CDC_REG_GLOBAL_TOTAL_WIDTH          0x05
#define CDC_REG_GLOBAL_CONTROL              0x06
#define CDC_REG_GLOBAL_CONFIG1              0x07
#define CDC_REG_GLOBAL_CONFIG2              0x08
#define CDC_REG_GLOBAL_SHADOW_RELOAD        0x09
#define CDC_REG_GLOBAL_GAMMA                0x0a
#define CDC_REG_GLOBAL_BG_COLOR             0x0b
#define CDC_REG_GLOBAL_IRQ_POLARITY         0x0c
#define CDC_REG_GLOBAL_IRQ_ENABLE           0x0d
#define CDC_REG_GLOBAL_IRQ_STATUS           0x0e
#define CDC_REG_GLOBAL_IRQ_CLEAR            0x0f
#define CDC_REG_GLOBAL_LINE_IRQ_POSITION    0x10
#define CDC_REG_GLOBAL_POSITION             0x11
#define CDC_REG_GLOBAL_SYNC_STATUS          0x12
#define CDC_REG_GLOBAL_BG_LAYER_BASE        0x13
#define CDC_REG_GLOBAL_BG_LAYER_INC         0x14
#define CDC_REG_GLOBAL_BG_LAYER_ADDR        0x15
#define CDC_REG_GLOBAL_BG_LAYER_DATA        0x16
#define CDC_REG_GLOBAL_SLAVE_TIMING_STATUS  0x17
#define CDC_REG_GLOBAL_EXT_DISPLAY          0x18

//control bits
#define CDC_REG_GLOBAL_CONTROL_HSYNC            0x80000000u
#define CDC_REG_GLOBAL_CONTROL_VSYNC            0x40000000u
#define CDC_REG_GLOBAL_CONTROL_BLANK            0x20000000u
#define CDC_REG_GLOBAL_CONTROL_CLK_POL          0x10000000u
#define CDC_REG_GLOBAL_CONTROL_STREAM_ENABLE    0x00100000u
#define CDC_REG_GLOBAL_CONTROL_SLAVE_TIMING     0x00040000u
#define CDC_REG_GLOBAL_CONTROL_BACKGROUND_LAYER 0x00020000u
#define CDC_REG_GLOBAL_CONTROL_DITHERING        0x00010000u
#define CDC_REG_GLOBAL_CONTROL_ENABLE           0x00000001u

// Layer span(in words)
#define CDC_LAYER_SPAN 0x40

// per layer registers
#define CDC_REG_LAYER_CONFIG_1                     0x00
#define CDC_REG_LAYER_CONFIG_2                     0x01
#define CDC_REG_LAYER_RELOAD                       0x02
#define CDC_REG_LAYER_CONTROL                      0x03
#define CDC_REG_LAYER_WINDOW_H                     0x04
#define CDC_REG_LAYER_WINDOW_V                     0x05
#define CDC_REG_LAYER_COLOR_KEY                    0x06
#define CDC_REG_LAYER_PIXEL_FORMAT                 0x07
#define CDC_REG_LAYER_ALPHA                        0x08
#define CDC_REG_LAYER_COLOR                        0x09
#define CDC_REG_LAYER_BLENDING                     0x0a
#define CDC_REG_LAYER_FB_BUS_CONTROL               0x0b
#define CDC_REG_LAYER_AUX_FB_CONTROL               0x0c
#define CDC_REG_LAYER_FB_START                     0x0d
#define CDC_REG_LAYER_FB_LENGTH                    0x0e
#define CDC_REG_LAYER_FB_LINES                     0x0f
#define CDC_REG_LAYER_AUX_FB_START                 0x10
#define CDC_REG_LAYER_AUX_FB_LENGTH                0x11
#define CDC_REG_LAYER_AUX_FB_LINES                 0x12
#define CDC_REG_LAYER_CLUT                         0x13

#define CDC_REG_LAYER_SCALER_INPUT_SIZE            0x13
#define CDC_REG_LAYER_SCALER_OUTPUT_SIZE           0x14
#define CDC_REG_LAYER_SCALER_V_SCALING_FACTOR      0x15
#define CDC_REG_LAYER_SCALER_V_SCALING_PHASE       0x16
#define CDC_REG_LAYER_SCALER_H_SCALING_FACTOR      0x17
#define CDC_REG_LAYER_SCALER_H_SCALING_PHASE       0x18
#define CDC_REG_LAYER_YCBCR_SCALE_1                0x19
#define CDC_REG_LAYER_YCBCR_SCALE_2                0x1a

#define SCALER_FRACTION 13
//layer config bits
#define CDC_REG_LAYER_CONFIG_ALPHA_PLANE  0x00000008u
//layer config 2 bits
#define CDC_REG_LAYER_CONFIG_SCALER_ENABLED 0x80000000u
#define CDC_REG_LAYER_CONFIG_YCBCR_ENABLED  0x40000000u

//layer control bits
#define CDC_REG_LAYER_CONTROL_DEFAULT_COLOR_BLENDING  0x00000200u
#define CDC_REG_LAYER_CONTROL_MIRRORING_ENABLE        0x00000100u
#define CDC_REG_LAYER_CONTROL_INSERTION_MODE          0x000000c0u
#define CDC_REG_LAYER_CONTROL_COLOR_KEY_REPLACE       0x00000020u
#define CDC_REG_LAYER_CONTROL_CLUT_ENABLE             0x00000010u
#define CDC_REG_LAYER_CONTROL_H_DUPLICATION           0x00000008u
#define CDC_REG_LAYER_CONTROL_V_DUPLICATION           0x00000004u
#define CDC_REG_LAYER_CONTROL_COLOR_KEY_ENABLE        0x00000002u
#define CDC_REG_LAYER_CONTROL_ENABLE                  0x00000001u

union cdc_hw_revision {
	u32 m_data;
	struct {
		u32 m_revision : 8;
		u32 m_minor : 8;
		u32 m_major : 8;
	} bits;
};

union cdc_config1 {
	u32 m_data;
	struct {
		u32 m_out_width_blue : 4;
		u32 m_out_width_green : 4;
		u32 m_out_width_red : 4;
		u32 m_precise_blending : 1; /* precise blending enabled */
		u32 m_pad0 : 1;
		u32 m_dithering : 2; /* dithering technique */
		u32 m_pad1 : 1;
		u32 m_gamma : 3; /* gamma correction technique */
		u32 m_pad2 : 1;
		u32 m_shadow_regs : 1; /* shadow registers enabled */
		u32 m_bg_color : 1; /* background color programmable */
		u32 m_bg_blending : 1; /* background blending enabled */
		u32 m_line_irq_pos : 1; /* line IRQ position programmable */
		u32 m_timing : 1; /* timing programmable */
		u32 m_irq_pol : 1; /* IRQ polarity programmable */
		u32 m_sync_pol : 1; /* sync polarity programmable */
		u32 m_dither_width : 1; /* dither width programmable */
		u32 m_status_regs : 1; /* status registers enabled */
		u32 m_config_reading : 1; /* config reading mode enabled */
		u32 m_blind_mode : 1; /* blind mode enabled */
	} bits;
};

union cdc_config2 {
	u32 m_data;
	struct {
		u32 m_bg_layer : 1; /* background layer ability enabled */
		u32 m_sync : 1; /* synchronization ability enabled */
		u32 m_dual_view : 1; /* dual view ability enabled */
		u32 m_out2 : 1; /* secondary RGB output port enabled */
		u32 m_bus_width : 3; /* bus width (log2 of number of bytes) */
		u32 m_ext_ctrl : 1; /* external display control ability enabled */
	} bits;
};

/*--------------------------------------------------------------------------
 * Enum: cdc_irq_type
 *  IRQ type(see <cdc_registerISR>)
 *
 *  CDC_IRQ_LINE                   - Programmable Scanline Interrupt
 *  CDC_IRQ_FIFO_UNDERRUN_WARN     - Indicates a possible upcoming fifo underrun
 *  CDC_IRQ_BUS_ERROR              - Indicates a bus error
 *  CDC_IRQ_RELOAD                 - Issued on every shadow reload
 *  CDC_IRQ_SLAVE_TIMING_NO_SIGNAL - Issued if slave timing mode is enabled, but
 *                                   no signal is detected
 *  CDC_IRQ_SLAVE_TIMING_NO_SYNC   - Issued if slave timing mode is enabled, but
 *                                   CDC is currently not in sync with external
 *                                   sync source
 *  CDC_IRQ_FIFO_UNDERRUN          - Indicates a fifo underrun
 *  CDC_IRQ_CRC_ERROR              - Indicates CRC mismatch of the frame
 */
enum cdc_irq_type {
	CDC_IRQ_LINE = 0x01,
	CDC_IRQ_FIFO_UNDERRUN_WARN = 0x02,
	CDC_IRQ_BUS_ERROR = 0x04,
	CDC_IRQ_RELOAD = 0x08,
	CDC_IRQ_SLAVE_TIMING_NO_SIGNAL = 0x10,
	CDC_IRQ_SLAVE_TIMING_NO_SYNC = 0x20,
	CDC_IRQ_FIFO_UNDERRUN = 0x40,
	CDC_IRQ_CRC_ERROR = 0x80,
};

/*--------------------------------------------------------------------------
 * Enum: cdc_blend_factor
 *  Blend factor used for blending between layers(see <cdc_layer_setBlendMode>)
 *
 *  CDC_BLEND_ONE                           - Blend factor of 1.0
 *  CDC_BLEND_ZERO                          - Blend factor of 0.0
 *  CDC_BLEND_PIXEL_ALPHA                   - Blend factor is taken from pixel's alpha value
 *  CDC_BLEND_PIXEL_ALPHA_INV               - Blend factor of 1.0 - pixel's alpha value
 *  CDC_BLEND_CONST_ALPHA                   - Constant alpha as blend factor
 *  CDC_BLEND_CONST_ALPHA_INV               - 1.0 - Constant alpha as blend factor
 *  CDC_BLEND_PIXEL_ALPHA_X_CONST_ALPHA     - Pixel alpha * constant alpha as blend factor
 *  CDC_BLEND_PIXEL_ALPHA_X_CONST_ALPHA_INV - 1.0 - (Pixel alpha * constant alpha as blend factor)
 */
enum cdc_blend_factor {
	CDC_BLEND_ONE = 0,
	CDC_BLEND_ZERO = 1,
	CDC_BLEND_PIXEL_ALPHA = 2,
	CDC_BLEND_PIXEL_ALPHA_INV = 3,
	CDC_BLEND_CONST_ALPHA = 4,
	CDC_BLEND_CONST_ALPHA_INV = 5,
	CDC_BLEND_PIXEL_ALPHA_X_CONST_ALPHA = 6,
	CDC_BLEND_PIXEL_ALPHA_X_CONST_ALPHA_INV = 7,
};

static const u8 cdc_formats_bpp[] __maybe_unused = { 4, 3, 2, 2, 2, 2, 1, 1 };

#endif // _CDC_REGS_H_

/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * cdc_hw.h  --  CDC Display Controller Hardware Interface
 *
 * Copyright (C) 2017 TES Electronic Solutions GmbH
 * Author: Christian Thaler <christian.thaler@tes-dst.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef CDC_HW_H_
#define CDC_HW_H_

#include "cdc_regs.h"

u32 cdc_read_reg(struct cdc_device *cdc, u32 reg);
void cdc_write_reg(struct cdc_device *cdc, u32 reg, u32 val);
u32 cdc_read_layer_reg(struct cdc_device *cdc, int layer, u32 reg);
void cdc_write_layer_reg(struct cdc_device *cdc, int layer, u32 reg, u32 val);
void cdc_irq_set(struct cdc_device *cdc, enum cdc_irq_type irq, bool enable);

#endif /* CDC_HW_H_ */

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __CDC_DESWIZZLE_H__
#define __CDC_DESWIZZLE_H__

#define DSWZ_MODE_DISABLED  0
#define DSWZ_MODE_TEST      1
#define DSWZ_MODE_LINEAR    2
#define DSWZ_MODE_DESWIZZLE 3

struct dswz_device;

void dswz_set_mode(struct dswz_device *dswz, int mode);
void dswz_set_fb_addr(struct dswz_device *dswz, u32 addr);
void dswz_set_fb_config(struct dswz_device *dswz, u16 width, u16 height, u32 pitch, u8 bpp);
void dswz_stop(struct dswz_device *dswz);
void dswz_trigger(struct dswz_device *dswz);
void dswz_retrigger(struct dswz_device *dswz);

#endif /* __CDC_DESWIZZLE_H__ */

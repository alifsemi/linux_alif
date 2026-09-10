// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/module.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>

#include "cdc_deswizzle.h"

#define DSWZ_REG_ID         0x00
#define DSWZ_REG_MODE       0x01
#define DSWZ_REG_FB_ADDR    0x02
#define DSWZ_REG_FB_DIM     0x03
#define DSWZ_REG_FB_PITCH   0x04
#define DSWZ_REG_IRQ_ENABLE 0x05
#define DSWZ_REG_IRQ_STATUS 0x06

struct dswz_device {
	struct device *dev;

	void __iomem *mmio;

	u32 fb_addr;
	u16 fb_width;
	u16 fb_height;
	u32 fb_pitch; /* byte pitch */
	u8  fb_bpp;   /* bytes per pixel */
	int mode;
	int mode_update; /* boolean: mode needs update */
};

static u32 dswz_read_reg(struct dswz_device *dswz, u32 reg)
{
	u32 val;

	val = readl(dswz->mmio + (reg << 2));

	return val;
}

static void dswz_write_reg(struct dswz_device *dswz, u32 reg, u32 val)
{
	writel(val, dswz->mmio + (reg << 2));
}

void dswz_set_mode(struct dswz_device *dswz, int mode)
{
	if (dswz->mode != mode) {
		dswz_write_reg(dswz, DSWZ_REG_MODE, DSWZ_MODE_DISABLED);
		dswz->mode_update = 1;
	}
	dswz->mode = mode;
}

void dswz_set_fb_addr(struct dswz_device *dswz, u32 addr)
{
	dswz->fb_addr = addr;
}

void dswz_set_fb_config(struct dswz_device *dswz, u16 width, u16 height, u32 pitch, u8 bpp)
{
	dswz->fb_width = width;
	dswz->fb_height = height;
	dswz->fb_pitch = pitch;
	dswz->fb_bpp = bpp;
}

void dswz_stop(struct dswz_device *dswz)
{
	dswz->mode = DSWZ_MODE_DISABLED;
	dswz_write_reg(dswz, DSWZ_REG_MODE, dswz->mode);
}

void dswz_trigger(struct dswz_device *dswz)
{
	/* Keep the mode from dswz_set_mode() (e.g. DESWIZZLE). LINEAR is
	 * only the default when nothing has been selected yet.
	 */
	if (dswz->mode == DSWZ_MODE_DISABLED)
		dswz->mode = DSWZ_MODE_LINEAR;

	dswz_write_reg(dswz, DSWZ_REG_FB_DIM, (dswz->fb_height << 16) | dswz->fb_width);
	dswz_write_reg(dswz, DSWZ_REG_FB_PITCH, (dswz->fb_bpp << 24) | dswz->fb_pitch);
	dswz_write_reg(dswz, DSWZ_REG_MODE, dswz->mode);
	dswz_write_reg(dswz, DSWZ_REG_FB_ADDR, dswz->fb_addr);
}

void dswz_retrigger(struct dswz_device *dswz)
{
	if (dswz->mode_update) {
		dswz->mode_update = 0;
		dswz_write_reg(dswz, DSWZ_REG_MODE, dswz->mode);
	}

	/* In test mode, we have to retrigger the core by a write to the mode
	 * register. For linear/deswizzle the core is triggered by writing
	 * the FB address.
	 */
	if (dswz->mode == DSWZ_MODE_TEST)
		dswz_write_reg(dswz, DSWZ_REG_MODE, dswz->mode);
	else
		dswz_write_reg(dswz, DSWZ_REG_FB_ADDR, dswz->fb_addr);
}

static void dswz_remove(struct platform_device *pdev)
{
	struct dswz_device *dswz = platform_get_drvdata(pdev);

	dswz_write_reg(dswz, DSWZ_REG_IRQ_ENABLE, 0);
}

static irqreturn_t dswz_irq(int irq, void *arg)
{
	struct dswz_device *dswz = (struct dswz_device *)arg;
	u32 status;

	status = dswz_read_reg(dswz, DSWZ_REG_IRQ_STATUS);
	dswz_write_reg(dswz, DSWZ_REG_IRQ_STATUS, status);

	//dswz_retrigger(dswz);

	return IRQ_HANDLED;
}

static int dswz_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct dswz_device *dswz;
	struct resource *mem;
	u32 reg_id;
	int irq;
	int ret;

	if (!np) {
		dev_err(&pdev->dev, "no platform data\n");
		return -ENODEV;
	}

	dswz = devm_kzalloc(&pdev->dev, sizeof(*dswz), GFP_KERNEL);
	if (!dswz)
		return -ENOMEM;

	dswz->dev = &pdev->dev;

	platform_set_drvdata(pdev, dswz);

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dswz->mmio = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(dswz->mmio))
		return PTR_ERR(dswz->mmio);
	dev_dbg(&pdev->dev, "Mapped IO from 0x%x to 0x%p\n", mem->start,
		dswz->mmio);

	reg_id = dswz_read_reg(dswz, DSWZ_REG_ID);
	dev_info(&pdev->dev, "TES Deswizzler rev. %x\n", reg_id);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dswz->dev, "Could not get platform IRQ number\n");
		return irq;
	}

	ret = devm_request_irq(dswz->dev, irq, dswz_irq, 0, dev_name(dswz->dev),
			       dswz);
	if (ret < 0) {
		dev_err(dswz->dev, "Failed to register IRQ\n");
		return ret;
	}

	dswz_write_reg(dswz, DSWZ_REG_IRQ_ENABLE, 1);
	dswz_write_reg(dswz, DSWZ_REG_IRQ_STATUS, 1);

	return 0;
}

static const struct of_device_id dswz_of_table[] = {
	{ .compatible = "tes,dswz" },
	{ /* end node */ }
};
MODULE_DEVICE_TABLE(of, dswz_of_table);

struct platform_driver dswz_driver = {
	.probe = dswz_probe,
	.remove = dswz_remove,
	.driver = {
		.name = "tes-dswz",
		.of_match_table = dswz_of_table,
	},
};

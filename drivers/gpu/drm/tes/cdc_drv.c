// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * cdc_drv.c  --  CDC Display Controller DRM driver
 *
 * Copyright (C) 2017 TES Electronic Solutions GmbH
 * Author: Christian Thaler <christian.thaler@tes-dst.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <linux/clk.h>

#include <linux/seq_file.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_debugfs.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_fbdev_dma.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_modeset_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#include "cdc_regs.h"

#include "cdc_drv.h"
#include "cdc_kms.h"
#include "cdc_crtc.h"
#include "cdc_deswizzle.h"
#include "cdc_hw.h"
#include "cdc_hw_helpers.h"
#include "cdc_ioctl.h"

extern struct platform_driver dswz_driver;

static const struct platform_device_id cdc_id_table[] = {
	{ "cdc", 0 },
	{ },
};

MODULE_DEVICE_TABLE(platform, cdc_id_table);

static const struct of_device_id cdc_of_table[] = {
	{ .compatible = "tes,cdc-2.1", .data = NULL },
	{ },
};

MODULE_DEVICE_TABLE(of, cdc_of_table);

static int cdc_layer_init(struct cdc_device *cdc)
{
	int i;

	dev_info(cdc->dev, "%s: layer_count=%d\n",
		 __func__, cdc->hw.layer_count);

	if (cdc->hw.layer_count == 0 || cdc->hw.layer_count > 16) {
		dev_err(cdc->dev, "%s: bogus layer_count %d\n",
			__func__, cdc->hw.layer_count);
		return -EINVAL;
	}

	cdc->planes = devm_kzalloc(cdc->dev,
				   sizeof(*cdc->planes) * cdc->hw.layer_count,
				   GFP_KERNEL);
	if (!cdc->planes)
		return -ENOMEM;

	for (i = 0; i < cdc->hw.layer_count; ++i) {
		dev_dbg(cdc->dev, "Initializing layer %d\n", i);
		cdc->planes[i].hw_idx = i;
		cdc->planes[i].cdc = cdc;
		cdc->planes[i].used = false;
		cdc->planes[i].control = 0;
		cdc_hw_layer_set_enabled(cdc, i, false);
		dev_dbg(cdc->dev, "layer %d initialized\n", i);
	}

	return 0;
}

static irqreturn_t cdc_irq(int irq, void *arg)
{
	struct cdc_device *cdc = (struct cdc_device *)arg;
	u32 status;

	status = cdc_read_reg(cdc, CDC_REG_GLOBAL_IRQ_STATUS);
	if (!status)
		return IRQ_NONE;

	cdc_write_reg(cdc, CDC_REG_GLOBAL_IRQ_CLEAR, status);

	if (status & CDC_IRQ_LINE) {
		cdc_crtc_irq(&cdc->crtc);
		if (cdc->dswz)
			dswz_retrigger(cdc->dswz);
	}
	if (status & CDC_IRQ_BUS_ERROR)
		dev_err_ratelimited(cdc->dev, "BUS error IRQ triggered\n");
	if (status & CDC_IRQ_FIFO_UNDERRUN_WARN) {
		// disable underrun IRQ to prevent IRQ flooding
		cdc_irq_set(cdc, CDC_IRQ_FIFO_UNDERRUN_WARN, false);

		dev_err_ratelimited(cdc->dev, "FIFO underrun warn\n");
	}
	if (status & CDC_IRQ_SLAVE_TIMING_NO_SIGNAL)
		dev_err_ratelimited(cdc->dev, "SLAVE no signal\n");
	if (status & CDC_IRQ_SLAVE_TIMING_NO_SYNC)
		dev_err_ratelimited(cdc->dev, "SLAVE no sync\n");
	if (status & CDC_IRQ_FIFO_UNDERRUN) {
		// disable underrun IRQ to prevent IRQ flooding
		cdc_irq_set(cdc, CDC_IRQ_FIFO_UNDERRUN, false);

		dev_err_ratelimited(cdc->dev, "FIFO underrun\n");
	}
	if (status & CDC_IRQ_CRC_ERROR) {
		// disable underrun IRQ to prevent IRQ flooding
		cdc_irq_set(cdc, CDC_IRQ_CRC_ERROR, false);

		dev_err_ratelimited(cdc->dev, "CRC error\n");
	}

	return IRQ_HANDLED;
}

int cdc_init_irq(struct cdc_device *cdc)
{
	struct platform_device *pdev = to_platform_device(cdc->dev);
	int i, irq, ret, nirq = 0;

	/*
	 * Alif Ensemble delivers each CDC IRQ_STATUS bit on a separate
	 * GIC SPI (HWRM Table 18-3). Request every DT interrupt so FIFO
	 * underrun / bus-error handling actually runs.
	 */
	cdc_write_reg(cdc, CDC_REG_GLOBAL_IRQ_ENABLE, cdc->hw.irq_enabled);
	cdc_write_reg(cdc, CDC_REG_GLOBAL_IRQ_CLEAR, 0xff);

	for (i = 0; ; i++) {
		irq = platform_get_irq_optional(pdev, i);
		if (irq < 0) {
			if (irq == -EPROBE_DEFER)
				return irq;
			break;
		}

		ret = devm_request_irq(cdc->dev, irq, cdc_irq, 0,
				       dev_name(cdc->dev), cdc);
		if (ret) {
			dev_err(cdc->dev, "Failed to register IRQ %d\n", irq);
			return ret;
		}
		nirq++;
	}

	if (!nirq) {
		dev_err(cdc->dev, "Could not get platform IRQ number\n");
		return -ENODEV;
	}

	return 0;
}

/* TODO: remove when cdc is fixed
 * Enforcing 256 byte pitch
 */
static int cdc_gem_dma_dumb_create(struct drm_file *file_priv,
				   struct drm_device *drm, struct drm_mode_create_dumb *args)
{
	args->pitch = (args->pitch + 255) & ~255;
	args->size = args->pitch * args->height;

	return drm_gem_dma_dumb_create_internal(file_priv, drm, args);
}

#ifdef CONFIG_DEBUG_FS
static int cdc_regs_show(struct seq_file *m, void *arg)
{
	struct drm_debugfs_entry *entry = m->private;
	struct drm_device *dev = entry->dev;
	struct cdc_device *cdc = dev->dev_private;
	unsigned int i;

	pm_runtime_get_sync(dev->dev);

	/* Show the first few registers */
	for (i = 0; i < (CDC_LAYER_SPAN + CDC_LAYER_SPAN * cdc->hw.layer_count);
	     i += 4) {
		u32 reg = cdc_read_reg(cdc, i);

		if (i == 0)
			seq_puts(m, "Global:\n");
		else if (i % CDC_LAYER_SPAN == 0)
			seq_printf(m, "Layer %d:\n", i / CDC_LAYER_SPAN);

		seq_printf(m, "%03x: %08x", i * 4, reg);
		reg = cdc_read_reg(cdc, i + 1);
		seq_printf(m, " %08x", reg);
		reg = cdc_read_reg(cdc, i + 2);
		seq_printf(m, " %08x", reg);
		reg = cdc_read_reg(cdc, i + 3);
		seq_printf(m, " %08x\n", reg);
	}

	pm_runtime_put_sync(dev->dev);

	return 0;
}

static int cdc_mm_show(struct seq_file *m, void *arg)
{
	struct drm_debugfs_entry *entry = m->private;
	struct drm_device *dev = entry->dev;
	struct drm_printer p = drm_seq_file_printer(m);

	drm_mm_print(&dev->vma_offset_manager->vm_addr_space_mm, &p);

	return 0;
}

static const struct drm_debugfs_info cdc_debugfs_list[] = {
	{ "regs", cdc_regs_show, 0 },
	{ "mm", cdc_mm_show, 0 },
};

static void cdc_debugfs_init(struct drm_minor *minor)
{
	drm_debugfs_add_files(minor->dev, cdc_debugfs_list,
			      ARRAY_SIZE(cdc_debugfs_list));
}
#endif

static long cdc_ioctl(struct file *file_ptr, unsigned int cmd, unsigned long arg)
{
	struct drm_file *file_priv = file_ptr->private_data;
	struct drm_device *dev;
	struct cdc_device *cdc;
	char stack_data[128];
	unsigned int nr = HACK_IOCTL_NR(cmd);
	unsigned int size;

	dev = file_priv->minor->dev;
	cdc = dev->dev_private;

	if (nr >= HACK_CMD_SET_CB) {
		size = _IOC_SIZE(cmd);

		if (size > sizeof(union cdc_ioctl_arg))
			return -ENOTTY;

		if (cmd & IOC_IN) {
			if (copy_from_user(stack_data, (void __user *)arg, size) != 0)
				return -EFAULT;
		}

		/* TODO: Dispatching */
		switch (nr) {
		case HACK_CMD_SET_CB: {
			struct hack_set_cb *set_cb =
				(struct hack_set_cb *)stack_data;

			cdc_hw_layer_set_cb_size(cdc, 0, set_cb->width,
						 set_cb->height, set_cb->pitch);
			cdc_hw_set_cb_address(cdc, 0,
					      (unsigned int)set_cb->phy_addr);
			if (cdc->dswz) {
				dswz_set_fb_addr(cdc->dswz, (u32)set_cb->phy_addr);
				dswz_set_mode(cdc->dswz, DSWZ_MODE_DESWIZZLE);
				dswz_retrigger(cdc->dswz);
			}
			cdc_hw_trigger_shadow_reload(cdc, true);
			break;
		}

		case HACK_CMD_SET_WINPOS: {
			struct hack_set_winpos *winpos =
				(struct hack_set_winpos *)stack_data;

			cdc_hw_set_window(cdc, 0, winpos->x, winpos->y,
					  winpos->width, winpos->height,
				winpos->width * 4);
			cdc_hw_layer_set_enabled(cdc, 0, true);
			cdc_hw_trigger_shadow_reload(cdc, true);
			break;
		}

		case HACK_CMD_SET_ALPHA: {
			struct hack_set_alpha *alpha =
				(struct hack_set_alpha *)stack_data;

			cdc_hw_set_blend_mode(cdc, 0,
					      CDC_BLEND_PIXEL_ALPHA_X_CONST_ALPHA,
				CDC_BLEND_PIXEL_ALPHA_X_CONST_ALPHA_INV);
			cdc_hw_layer_set_constant_alpha(cdc, 0, alpha->alpha);
			cdc_hw_trigger_shadow_reload(cdc, true);
			break;
		}

		case HACK_CMD_WAIT_VSYNC: {
			unsigned long flags;

			drm_crtc_vblank_get(&cdc->crtc);
			spin_lock_irqsave(&cdc->irq_slck, flags);
			cdc->irq_stat = 0;
			spin_unlock_irqrestore(&cdc->irq_slck, flags);
			wait_event_interruptible(cdc->irq_waitq, cdc->irq_stat);
			drm_crtc_vblank_put(&cdc->crtc);

			break;
		}

		case HACK_CMD_GET_FBDEV_FB: {
			struct hack_get_fbdev_fb_info *args =
				(struct hack_get_fbdev_fb_info *)stack_data;

			struct drm_fb_helper *fb_helper = dev->fb_helper;
			struct drm_framebuffer *fb;
			struct drm_gem_dma_object *dma_obj;

			if (!fb_helper)
				return -EINVAL;

			fb = fb_helper->buffer->fb;
			dma_obj = drm_fb_dma_get_gem_obj(fb, 0);
			if (dma_obj) {
				args->phys_addr	= dma_obj->dma_addr;
				args->width	= fb->width;
				args->height	= fb->height;
				args->pitch	= fb->pitches[0];
				args->bpp	= fb->format->cpp[0] * 8;
			}
			break;
		}

		default:
			pr_info("Unknown IOCTL(nr = %u)!\n", nr);
		}

		if (cmd & IOC_OUT) {
			if (copy_to_user((void __user *)arg,
					 stack_data, size) != 0)
				return -EFAULT;
		}

		return 0;
	}

	return drm_ioctl(file_ptr, cmd, arg);
}

static const struct file_operations cdc_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = cdc_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = drm_compat_ioctl,
#endif
	.poll = drm_poll,
	.read = drm_read,
	.llseek = noop_llseek,
	.mmap = drm_gem_mmap,
	.fop_flags = FOP_UNSIGNED_OFFSET,
};

static const struct drm_driver cdc_driver = {
	.driver_features = DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	DRM_GEM_DMA_DRIVER_OPS_WITH_DUMB_CREATE(cdc_gem_dma_dumb_create),
#ifdef CONFIG_DEBUG_FS
	.debugfs_init = cdc_debugfs_init,
#endif
	.fops = &cdc_fops,
	.name = "tes-cdc",
	.desc = "TES CDC Display Controller",
	.date = "20190902",
	.major = 1,
	.minor = 0,
};

static int __maybe_unused cdc_pm_suspend(struct device *dev)
{
	struct cdc_device *cdc = dev_get_drvdata(dev);
	int ret;

	ret = drm_mode_config_helper_suspend(cdc->ddev);
	if (ret)
		return ret;

	/* PIXEL_CLK then APB: CDC_EN is already clear from CRTC disable. */
	clk_disable_unprepare(cdc->pix_clk);
	clk_disable_unprepare(cdc->pclk);

	return 0;
}

static int __maybe_unused cdc_pm_resume(struct device *dev)
{
	struct cdc_device *cdc = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(cdc->pclk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(cdc->pix_clk);
	if (ret)
		goto err_pclk;

	ret = drm_mode_config_helper_resume(cdc->ddev);
	if (ret)
		goto err_pix_clk;

	return 0;

err_pix_clk:
	clk_disable_unprepare(cdc->pix_clk);
err_pclk:
	clk_disable_unprepare(cdc->pclk);
	return ret;
}

static const struct dev_pm_ops cdc_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(cdc_pm_suspend, cdc_pm_resume)
};

static void cdc_remove(struct platform_device *pdev)
{
	struct cdc_device *cdc = platform_get_drvdata(pdev);
	struct drm_device *ddev = cdc->ddev;

	if (!ddev)
		return;

	/* Turn off vblank processing and irq */
	drm_crtc_vblank_off(&cdc->crtc);

	/* Turn off CRTC */
	cdc_hw_set_enabled(cdc, false);

	drm_dev_unregister(ddev);

	drm_kms_helper_poll_fini(ddev);
	drm_mode_config_cleanup(ddev);

	cdc_write_reg(cdc, CDC_REG_GLOBAL_IRQ_ENABLE, 0x0);
	cdc_hw_set_enabled(cdc, false);

	drm_dev_put(ddev);

	of_reserved_mem_device_release(&pdev->dev);
	clk_disable_unprepare(cdc->pix_clk);
	clk_disable_unprepare(cdc->pclk);
}

static int compare_name_dswz(struct device *dev, void *data)
{
	if (!dev->driver)
		return 0;

	return !!strstr(dev->driver->name, "dswz");
}

static int cdc_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device *dswz_dev;
	struct cdc_device *cdc;
	struct drm_device *ddev;
	struct resource *mem;
	int ret = 0;

	if (!np) {
		dev_err(&pdev->dev, "no platform data\n");
		return -ENODEV;
	}

	cdc = devm_kzalloc(&pdev->dev, sizeof(*cdc), GFP_KERNEL);
	if (!cdc)
		return -ENOMEM;

	init_waitqueue_head(&cdc->commit.wait);

	/* FIXME HACK for MesseDemo   */
	spin_lock_init(&cdc->irq_slck);
	init_waitqueue_head(&cdc->irq_waitq);

	cdc->dev = &pdev->dev;

	platform_set_drvdata(pdev, cdc);

	cdc->pclk = devm_clk_get(&pdev->dev, "apb_pclk");
	if (IS_ERR(cdc->pclk)) {
		dev_err(&pdev->dev, "failed to initialize APB clock\n");
		return PTR_ERR(cdc->pclk);
	}

	cdc->pix_clk = devm_clk_get(&pdev->dev, "pix_clk");
	if (IS_ERR(cdc->pix_clk)) {
		dev_err(&pdev->dev, "failed to initialize Pixel Clock\n");
		return PTR_ERR(cdc->pix_clk);
	}

	ret = clk_prepare_enable(cdc->pclk);
	if (ret) {
		dev_err(&pdev->dev, "Failed to enable APB clock\n");
		return ret;
	}

	ret = clk_prepare_enable(cdc->pix_clk);
	if (ret) {
		dev_err(&pdev->dev, "Failed to enable Pixel clock\n");
		goto err_pclk;
	}

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	cdc->mmio = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(cdc->mmio)) {
		ret = PTR_ERR(cdc->mmio);
		goto err_pix_clk;
	}
	dev_dbg(&pdev->dev, "Mapped IO %pR\n", mem);

	np = of_parse_phandle(pdev->dev.of_node, "memory-region", 0);
	if (np) {
		dev_dbg(&pdev->dev, "Using reserved memory as CMA pool\n");
		ret = of_reserved_mem_device_init(&pdev->dev);
		of_node_put(np);
		if (ret) {
			dev_err(&pdev->dev, "Could not get reserved memory\n");
			goto err_pix_clk;
		}
	} else {
		dev_dbg(&pdev->dev, "Using default CMA pool\n");
	}

	/* DRM/KMS objects */
	ddev = drm_dev_alloc(&cdc_driver, &pdev->dev);
	if (IS_ERR(ddev)) {
		ret = PTR_ERR(ddev);
		goto err_rmem;
	}

	cdc->ddev = ddev;
	ddev->dev_private = cdc;

	cdc_crtc_set_vblank(cdc, false);
	cdc->hw.enabled = false;
	cdc->hw.irq_enabled = 0;

	/* get the hw configuration */
	{
		union cdc_hw_revision hwrev;
		union cdc_config1 conf1;
		union cdc_config2 conf2;
		u32 layer_count;

		hwrev.m_data = cdc_read_reg(cdc, CDC_REG_GLOBAL_HW_REVISION);
		layer_count = cdc_read_reg(cdc, CDC_REG_GLOBAL_LAYER_COUNT);
		conf1.m_data = cdc_read_reg(cdc, CDC_REG_GLOBAL_CONFIG1);
		conf2.m_data = cdc_read_reg(cdc, CDC_REG_GLOBAL_CONFIG2);

		/* CDC_LCNT: LNUM field is bits[7:0] only */
		cdc->hw.layer_count = layer_count & 0xFF;
		cdc->hw.shadow_regs = conf1.bits.m_shadow_regs;
		cdc->hw.bus_width = 1 << conf2.bits.m_bus_width;

		dev_info(&pdev->dev, "CDC HW ver. %u.%u(rev. %u):\n",
			 hwrev.bits.m_major, hwrev.bits.m_minor,
			 hwrev.bits.m_revision);
		dev_info(&pdev->dev, "\tlayer count: %u\n", cdc->hw.layer_count);
		dev_info(&pdev->dev, "\tbus width: %u byte\n", cdc->hw.bus_width);
	}

	/* Spawn stream sub devices if available */
	ret = devm_of_platform_populate(cdc->dev);
	if (ret)
		goto err_drm;

	dswz_dev = device_find_child(cdc->dev, NULL, compare_name_dswz);
	if (dswz_dev) {
		dev_info(&pdev->dev, "\tdeswizzler: yes\n");
		cdc->dswz = (struct dswz_device *)dev_get_drvdata(dswz_dev);
		put_device(dswz_dev);
		/* todo: build an aggregate driver? */
	} else {
		dev_info(&pdev->dev, "\tdeswizzler: no\n");
	}

	ret = cdc_layer_init(cdc);
	if (ret)
		goto err_drm;

	cdc_hw_reset_registers(cdc);

	ret = cdc_init_irq(cdc);
	if (ret)
		goto err_drm;

	ret = cdc_modeset_init(cdc);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to initialize CDC Modeset\n");
		goto err_drm;
	}

	/* Register the DRM device with the core and the connectors with
	 * sysfs.
	 */
	ret = drm_dev_register(ddev, 0);
	if (ret)
		goto err_kms;

	DRM_INFO("Device %s probed\n", dev_name(&pdev->dev));

	/* DSWZ driver needs retriggering in every frame. So we increase
	 * the use counter of the vblank.
	 */
	if (cdc->dswz)
		drm_crtc_vblank_get(&cdc->crtc);

	/* Setup framebuffer with 16 bits/pixel */
	drm_fbdev_dma_setup(ddev, 16);
	return 0;

err_kms:
	drm_kms_helper_poll_fini(ddev);
	drm_mode_config_cleanup(ddev);
err_drm:
	drm_dev_put(ddev);
err_rmem:
	of_reserved_mem_device_release(&pdev->dev);
err_pix_clk:
	clk_disable_unprepare(cdc->pix_clk);
err_pclk:
	clk_disable_unprepare(cdc->pclk);
	return ret;
}

static struct platform_driver cdc_platform_driver = {
	.probe = cdc_probe,
	.remove = cdc_remove,
	.driver = {
		.name = "tes-cdc",
		.pm = &cdc_pm_ops,
		.of_match_table = cdc_of_table,
	},
	.id_table = cdc_id_table,
};

static struct platform_driver * const drivers[] = {
	&dswz_driver,
	&cdc_platform_driver,
};

static int cdc_drv_init(void)
{
	return platform_register_drivers(drivers, ARRAY_SIZE(drivers));
}
module_init(cdc_drv_init);

static void cdc_drv_exit(void)
{
	platform_unregister_drivers(drivers, ARRAY_SIZE(drivers));
}
module_exit(cdc_drv_exit);

MODULE_AUTHOR("Christian Thaler <christian.thaler@tes-dst.com>");
MODULE_AUTHOR("Yogender Arya <yogender.kumar@alifsemi.com>");
MODULE_DESCRIPTION("TES CDC Display Controller DRM Driver");
MODULE_LICENSE("GPL");


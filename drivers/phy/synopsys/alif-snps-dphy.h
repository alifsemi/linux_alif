/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Alif SoC-specific Synopsys D-PHY vendor ops
 *
 * Copyright (C) 2026 Alif Semiconductor
 * Author: Yogender Kumar Arya <yogender.kumar@alifsemi.com>
 */

#ifndef _ALIF_SNPS_DPHY_H_
#define _ALIF_SNPS_DPHY_H_

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include "snps-dphy-priv.h"

#define DSI_PHY_RSTZ		0xA0

#define DPHY_PLL_CTRL0		0x10
#define DPHY_PLL_CTRL1		0x14
#define DPHY_PLL_CTRL2		0x18
#define TX_DPHY_CTRL0		0x30
#define TX_DPHY_CTRL1		0x34
#define RX_DPHY_CTRL0		0x38
#define RX_DPHY_CTRL1		0x3C

#define DPHY_PLL_CTRL0_INIT	0x10000
#define DPHY_PLL_CTRL1_INIT	0xF040
#define DPHY_PLL_CTRL2_INIT	0x03100400

#define DPHY_CFG_CLK_MIN_MHZ	17
#define DPHY_CFG_CLK_RANGE_MULT	4

#define DSI_CTRL_CAM2_EN		9
#define DPHY_PLL_CTRL0_SHADOW_CONTROL	4
#define DPHY_PLL_CTRL0_SHADOW_CLR	12
#define DPHY_PLL_CTRL0_UPDATE_PLL	8

#define DPHY_CTRL0_CFG_CLK_FREQ_RANGE_SHIFT	24
#define DPHY_CTRL0_BASE_DIR_SHIFT		12
#define DPHY_CTRL0_TXRXZ			8

#define DPHY_CTRL1_FORCE_RX_MODE_SHIFT		0

#define DSI_PHY_RSTZ_PHY_SHUTDOWNZ	0
#define DSI_PHY_RSTZ_PHY_RSTZ		1
#define DSI_PHY_RSTZ_PHY_ENABLECLK	2
#define DSI_PHY_RSTZ_PHY_FORCEPLL	3

/* SoC control register for camera/DSI mux */
#define DSI_CTRL			0x44

/* CSI-2 PHY stopstate status register */
#define CSI_PHY_RX			0x48
#define CSI_PHY_STOPSTATE		0x4C
#define CSI_PHY_SHUTDOWNZ		0x40
#define CSI_DPHY_RSTZ			0x44

#define CSI_PHY_SHUTDOWNZ_PHY_SHUTDOWNZ	0
#define CSI_DPHY_RSTZ_DPHY_RSTZ		0

#define CSI_PHY_STOPSTATE_PHY_STOPSTATECLK	BIT(16)
#define CSI_PHY_STOPSTATE_PHY_STOPSTATEDATA_0	BIT(0)
#define CSI_PHY_STOPSTATE_PHY_STOPSTATEDATA_1	BIT(1)

#define CSI_PHY_RX_CLK_ULPS		BIT(16)
#define CSI_PHY_RX_CLK_ACTIVE_HS	BIT(17)

/* VBAT PWR_CTRL register (offset 0x8 from vbat base) */
#define VBAT_PWR_CTRL			0x08
#define VBAT_TX_DPHY_PWR_MASK		BIT(0)
#define VBAT_TX_DPHY_ISO		BIT(1)
#define VBAT_RX_DPHY_PWR_MASK		BIT(4)
#define VBAT_RX_DPHY_ISO		BIT(5)
#define VBAT_DPHY_PLL_PWR_MASK		BIT(8)
#define VBAT_DPHY_PLL_ISO		BIT(9)
#define VBAT_DPHY_VPH_1P8_PWR_BYP_EN	BIT(12)
#define VBAT_DPHY_VPH_1P8_PWR_BYP_VAL	BIT(13)
#define VBAT_UPHY_PWR_MASK		BIT(16)
#define VBAT_UPHY_ISO			BIT(17)
#define VBAT_VREG_AUX_1_1V8_EN		BIT(24)
#define VBAT_VREG_AUX_2_1V8_EN		BIT(25)

#define VBAT_PWR_ON_INIT \
	(VBAT_DPHY_VPH_1P8_PWR_BYP_EN | VBAT_DPHY_VPH_1P8_PWR_BYP_VAL | \
	 VBAT_UPHY_PWR_MASK | VBAT_UPHY_ISO | \
	 VBAT_VREG_AUX_1_1V8_EN | VBAT_VREG_AUX_2_1V8_EN)

struct alif_snps_dphy {
	void __iomem *soc_regs;
	void __iomem *vbat_regs;
};

static int alif_dphy_ops_power_on(struct device *dev,
				  enum snps_dphy_block block,
				  enum snps_phy_submode submode)
{
	struct snps_dphy *dphy = dev_get_drvdata(dev);
	struct alif_snps_dphy *vend_priv = dphy->vend_priv;
	void __iomem *ctrl0;
	void __iomem *ctrl1;
	struct testctrl ifx;
	u32 temp;
	u32 stop_mask;
	u32 val;
	int ret;
	unsigned long rx_clk_rate;

	if (block == SNPS_DPHY_RX) {
		temp = readl(vend_priv->vbat_regs + VBAT_PWR_CTRL);
		temp &= ~(VBAT_RX_DPHY_ISO | VBAT_RX_DPHY_PWR_MASK |
			  VBAT_DPHY_PLL_ISO | VBAT_DPHY_PLL_PWR_MASK |
			  VBAT_DPHY_VPH_1P8_PWR_BYP_EN);
		writel(temp, vend_priv->vbat_regs + VBAT_PWR_CTRL);
		fsleep(10);

		ret = clk_prepare_enable(dphy->pll_ref_clk);
		if (ret)
			return ret;

		ret = clk_prepare_enable(dphy->rx_dphy_clk);
		if (ret) {
			clk_disable_unprepare(dphy->pll_ref_clk);
			return ret;
		}

		rx_clk_rate = clk_get_rate(dphy->rx_dphy_clk);

		ctrl0 = vend_priv->soc_regs + RX_DPHY_CTRL0;
		ctrl1 = vend_priv->soc_regs + RX_DPHY_CTRL1;
		dphy->phy_ctrl0 = ctrl0;

		dw_dphy_write_msk(vend_priv->soc_regs + DSI_CTRL, 0, DSI_CTRL_CAM2_EN, 1);

		dw_dphy_write_msk(dphy->csi_regs + CSI_DPHY_RSTZ, 0,
				  CSI_DPHY_RSTZ_DPHY_RSTZ, 1);
		dw_dphy_write_msk(dphy->csi_regs + CSI_PHY_SHUTDOWNZ, 0,
				  CSI_PHY_SHUTDOWNZ_PHY_SHUTDOWNZ, 1);

		dw_dphy_write_msk(ctrl0, 0, DPHY_CTRL0_TXRXZ, 1);

		dw_dphy_write_msk(ctrl0, 1, DPHY_CTRL0_TESTPORT_SEL, 1);
		dw_dphy_write_msk(dphy->csi_regs + CSI_TSTCTRL0, 1, 0, 1);
		dw_dphy_write_msk(ctrl0, 0, DPHY_CTRL0_TESTPORT_SEL, 1);
		dw_dphy_write_msk(dphy->csi_regs + CSI_TSTCTRL0, 1, 0, 1);

		fsleep(1);

		dw_dphy_write_msk(ctrl0, 1, DPHY_CTRL0_TESTPORT_SEL, 1);
		dw_dphy_write_msk(dphy->csi_regs + CSI_TSTCTRL0, 0, 0, 1);
		dw_dphy_write_msk(ctrl0, 0, DPHY_CTRL0_TESTPORT_SEL, 1);
		dw_dphy_write_msk(dphy->csi_regs + CSI_TSTCTRL0, 0, 0, 1);

		dw_dphy_write_msk(ctrl0, dphy->freq_settings->hsfreqrange,
				  16, 7);

		{
			u32 cfg_clk_mhz = (u32)(dphy->cfg_clk_freq / HZ_PER_MHZ);
			u32 cfgclk = (cfg_clk_mhz > DPHY_CFG_CLK_MIN_MHZ) ?
			     ((cfg_clk_mhz - DPHY_CFG_CLK_MIN_MHZ) * DPHY_CFG_CLK_RANGE_MULT) : 0;

			dw_dphy_write_msk(ctrl0, cfgclk, DPHY_CTRL0_CFG_CLK_FREQ_RANGE_SHIFT, 8);
		}

		ifx.ctrl0 = dphy->csi_regs + CSI_TSTCTRL0;
		ifx.ctrl1 = dphy->csi_regs + CSI_TSTCTRL1;

		dw_dphy_write_msk(ctrl0, 1, DPHY_CTRL0_TESTPORT_SEL, 1);

		snps_dphy_slave_setup(dphy, &ifx);

		dw_dphy_write_msk(ctrl0,
				  (1 << dphy->inst[block].num_lanes) - 1,
				  DPHY_CTRL0_BASE_DIR_SHIFT, 2);

		/*
		 * Force RX mode while releasing reset, wait for STOPSTATE, then
		 * clear the force so the PHY can track real lane states.
		 */
		{
			u32 force_mask = (1 << dphy->inst[block].num_lanes) - 1;

			dw_dphy_write_msk(ctrl1, force_mask, DPHY_CTRL1_FORCE_RX_MODE_SHIFT, 4);
			dw_dphy_write_msk(dphy->csi_regs + CSI_PHY_SHUTDOWNZ, 1,
					  CSI_PHY_SHUTDOWNZ_PHY_SHUTDOWNZ, 1);
			dw_dphy_write_msk(dphy->csi_regs + CSI_DPHY_RSTZ, 1,
					  CSI_DPHY_RSTZ_DPHY_RSTZ, 1);

			usleep_range(100, 200);

			stop_mask = CSI_PHY_STOPSTATE_PHY_STOPSTATECLK |
				    CSI_PHY_STOPSTATE_PHY_STOPSTATEDATA_0;
			if (dphy->inst[block].num_lanes == 2)
				stop_mask |= CSI_PHY_STOPSTATE_PHY_STOPSTATEDATA_1;

			val = readl(dphy->csi_regs + CSI_PHY_STOPSTATE);
			ret = readl_poll_timeout(dphy->csi_regs + CSI_PHY_STOPSTATE, val,
						 (val & stop_mask) == stop_mask, 100, 200000);
			if (ret) {
				dev_warn(dphy->dev,
					 "STOPSTATE timeout: 0x%x (keeping force-rx-mode)\n",
					 val);
			} else {
				dw_dphy_write_msk(ctrl1, 0, DPHY_CTRL1_FORCE_RX_MODE_SHIFT, 4);
			}
		}
		return 0;

	} else {
		ret = clk_prepare_enable(dphy->tx_dphy_clk);
		if (ret)
			return ret;

		ctrl0 = vend_priv->soc_regs + TX_DPHY_CTRL0;
		ctrl1 = vend_priv->soc_regs + TX_DPHY_CTRL1;

		dw_dphy_write_msk(dphy->dsi_regs + DSI_PHY_RSTZ, 0,
				  DSI_PHY_RSTZ_PHY_RSTZ, 1);
		dw_dphy_write_msk(dphy->dsi_regs + DSI_PHY_RSTZ, 0,
				  DSI_PHY_RSTZ_PHY_SHUTDOWNZ, 1);

		writel(DPHY_PLL_CTRL0_INIT, vend_priv->soc_regs + DPHY_PLL_CTRL0);
		dw_dphy_write_msk(vend_priv->soc_regs + DPHY_PLL_CTRL0, 1,
				  DPHY_PLL_CTRL0_SHADOW_CONTROL, 1);
		usleep_range(100, 500);
		dw_dphy_write_msk(vend_priv->soc_regs + DPHY_PLL_CTRL0, 1,
				  DPHY_PLL_CTRL0_SHADOW_CLR, 1);
		usleep_range(100, 500);
		dw_dphy_write_msk(vend_priv->soc_regs + DPHY_PLL_CTRL0, 0,
				  DPHY_PLL_CTRL0_SHADOW_CLR, 1);
		usleep_range(100, 500);
		writel(DPHY_PLL_CTRL1_INIT, vend_priv->soc_regs + DPHY_PLL_CTRL1);
		writel(DPHY_PLL_CTRL2_INIT, vend_priv->soc_regs + DPHY_PLL_CTRL2);
		dw_dphy_write_msk(vend_priv->soc_regs + DPHY_PLL_CTRL0, 1,
				  DPHY_PLL_CTRL0_UPDATE_PLL, 1);
		usleep_range(100, 500);
		dw_dphy_write_msk(vend_priv->soc_regs + DPHY_PLL_CTRL0, 0,
				  DPHY_PLL_CTRL0_UPDATE_PLL, 1);

		dw_dphy_write_msk(vend_priv->soc_regs + DSI_CTRL, 1,
				  DSI_CTRL_CAM2_EN, 1);

		dw_dphy_write_msk(ctrl0, 1, DPHY_CTRL0_TXRXZ, 1);
		dw_dphy_write_msk(ctrl0, 1, DPHY_CTRL0_TESTPORT_SEL, 1);
		dw_dphy_write_msk(ctrl0, 32, DPHY_CTRL0_CFG_CLK_FREQ_RANGE_SHIFT, 8);
		dw_dphy_write_msk(ctrl0,
				  (1 << dphy->inst[block].num_lanes) - 1,
				  DPHY_CTRL0_BASE_DIR_SHIFT, 2);
		dw_dphy_write_msk(ctrl1,
				  (1 << dphy->inst[block].num_lanes) - 1,
				  DPHY_CTRL1_FORCE_RX_MODE_SHIFT, 4);
		return 0;
	}
}

static int alif_dphy_ops_power_off(struct device *dev, enum snps_dphy_block block)
{
	struct snps_dphy *dphy = dev_get_drvdata(dev);
	struct alif_snps_dphy *vend_priv = dphy->vend_priv;
	u32 temp;

	if (block == SNPS_DPHY_RX) {
		clk_disable_unprepare(dphy->rx_dphy_clk);
		clk_disable_unprepare(dphy->pll_ref_clk);

		/* Re-enable isolation and power-mask on power-off */
		temp = readl(vend_priv->vbat_regs + VBAT_PWR_CTRL);
		temp |= VBAT_RX_DPHY_ISO | VBAT_RX_DPHY_PWR_MASK;
		writel(temp, vend_priv->vbat_regs + VBAT_PWR_CTRL);
		fsleep(10);
	} else {
		clk_disable_unprepare(dphy->tx_dphy_clk);
	}

	return 0;
}

static const struct vend_ops alif_dphy_ops = {
	.power_on = alif_dphy_ops_power_on,
	.power_off = alif_dphy_ops_power_off,
};

#endif /* _ALIF_SNPS_DPHY_H_ */

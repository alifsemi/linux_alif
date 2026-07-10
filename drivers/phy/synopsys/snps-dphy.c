// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Alif Semiconductor
 * Author: Yogender Kumar Arya <yogender.kumar@alifsemi.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/phy/phy.h>
#include <linux/phy/phy-mipi-dphy.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include <linux/sizes.h>
#include "alif-snps-dphy.h"

#define MIN_LANES	1
#define MAX_LANES	4

#define PHY_TESTCLK	1

#define PHY_TESTEN	16
#define PHY_TESTDOUT	8
#define PHY_TESTDIN	0

u8 mipi_dphy_read_reg(struct testctrl *ifx, u16 addr)
{
	u8 ret;

	dw_dphy_write_msk(ifx->ctrl0, 0, PHY_TESTCLK, 1);
	dw_dphy_write_msk(ifx->ctrl1, 0, PHY_TESTEN, 1);
	dw_dphy_write_msk(ifx->ctrl1, 1, PHY_TESTEN, 1);
	dw_dphy_write_msk(ifx->ctrl0, 1, PHY_TESTCLK, 1);
	dw_dphy_write_msk(ifx->ctrl1, 0x00, PHY_TESTDIN, 8);
	dw_dphy_write_msk(ifx->ctrl0, 0, PHY_TESTCLK, 1);
	dw_dphy_write_msk(ifx->ctrl1, 0, PHY_TESTEN, 1);
	dw_dphy_write_msk(ifx->ctrl1, (u8)(addr >> 8), PHY_TESTDIN, 8);
	dw_dphy_write_msk(ifx->ctrl0, 1, PHY_TESTCLK, 1);
	dw_dphy_write_msk(ifx->ctrl0, 0, PHY_TESTCLK, 1);
	dw_dphy_write_msk(ifx->ctrl1, 1, PHY_TESTEN, 1);
	dw_dphy_write_msk(ifx->ctrl0, 1, PHY_TESTCLK, 1);
	dw_dphy_write_msk(ifx->ctrl1, (u8)addr, PHY_TESTDIN, 8);
	dw_dphy_write_msk(ifx->ctrl0, 0, PHY_TESTCLK, 1);
	dw_dphy_write_msk(ifx->ctrl1, 0, PHY_TESTEN, 1);
	ret = dw_dphy_read_msk(ifx->ctrl1, PHY_TESTDOUT, 8);

	return ret;
}

void mipi_dphy_write_reg(struct testctrl *ifx, u16 addr, u8 data)
{
	dw_dphy_write_msk(ifx->ctrl1, 0, PHY_TESTEN, 1);
	dw_dphy_write_msk(ifx->ctrl0, 0, PHY_TESTCLK, 1);
	dw_dphy_write_msk(ifx->ctrl1, 1, PHY_TESTEN, 1);
	dw_dphy_write_msk(ifx->ctrl0, 1, PHY_TESTCLK, 1);
	dw_dphy_write_msk(ifx->ctrl1, 0x00, PHY_TESTDIN, 8);
	dw_dphy_write_msk(ifx->ctrl0, 0, PHY_TESTCLK, 1);
	dw_dphy_write_msk(ifx->ctrl1, 0, PHY_TESTEN, 1);
	dw_dphy_write_msk(ifx->ctrl1, (u8)(addr >> 8), PHY_TESTDIN, 8);
	dw_dphy_write_msk(ifx->ctrl0, 1, PHY_TESTCLK, 1);
	dw_dphy_write_msk(ifx->ctrl0, 0, PHY_TESTCLK, 1);
	dw_dphy_write_msk(ifx->ctrl1, 1, PHY_TESTEN, 1);
	dw_dphy_write_msk(ifx->ctrl0, 1, PHY_TESTCLK, 1);
	dw_dphy_write_msk(ifx->ctrl1, (u8)addr, PHY_TESTDIN, 8);
	dw_dphy_write_msk(ifx->ctrl0, 0, PHY_TESTCLK, 1);
	dw_dphy_write_msk(ifx->ctrl1, 0, PHY_TESTEN, 1);
	dw_dphy_write_msk(ifx->ctrl1, (u8)data, PHY_TESTDIN, 8);
	dw_dphy_write_msk(ifx->ctrl0, 1, PHY_TESTCLK, 1);
	dw_dphy_write_msk(ifx->ctrl0, 0, PHY_TESTCLK, 1);
}

int snps_dphy_slave_setup(struct snps_dphy *dphy, struct testctrl *ifx)
{
	u8 temp;

	dw_dphy_write_msk(dphy->phy_ctrl0, 0,
			  DPHY_CTRL0_TESTPORT_SEL, 1);

	mipi_dphy_write_reg(ifx,
			    dphy4txtester_DIG_RDWR_TX_PLL_13,
			    0x03);

	mipi_dphy_write_reg(ifx,
			    dphy4txtester_DIG_RDWR_TX_CB_1,
			    0x06);

	mipi_dphy_write_reg(ifx,
			    dphy4txtester_DIG_RDWR_TX_CB_0,
			    0x53);

	temp = mipi_dphy_read_reg(ifx,
				  dphy4txtester_DIG_RDWR_TX_PLL_9);

	temp |= BIT(3);

	mipi_dphy_write_reg(ifx,
			    dphy4txtester_DIG_RDWR_TX_PLL_9,
			    temp);

	temp = mipi_dphy_read_reg(ifx,
				  dphy4txtester_DIG_RDWR_TX_PLL_9);

	mipi_dphy_write_reg(ifx,
			    dphy4txtester_DIG_RDWR_TX_SLEW_0,
			    0x04);

	dw_dphy_write_msk(dphy->phy_ctrl0, 1,
			  DPHY_CTRL0_TESTPORT_SEL, 1);

	temp = mipi_dphy_read_reg(ifx,
				  dphy4rxtester_DIG_RDWR_RX_CLKLANE_LANE_6);

	temp |= BIT(7);

	mipi_dphy_write_reg(ifx,
			    dphy4rxtester_DIG_RDWR_RX_CLKLANE_LANE_6,
			    temp);

	temp = mipi_dphy_read_reg(ifx,
				  dphy4rxtester_DIG_RDWR_RX_CLKLANE_LANE_6);

	if (dphy->inst[SNPS_DPHY_RX].mbps == DPHY_RX_MIN_MBPS) {
		mipi_dphy_write_reg(ifx,
				    dphy4rxtester_DIG_RD_RX_SYS_1,
				    0x85);

		temp = mipi_dphy_read_reg(ifx,
					  dphy4rxtester_DIG_RD_RX_SYS_1);
	}

	mipi_dphy_write_reg(ifx,
			    dphy4rxtester_DIG_RDWR_RX_RX_STARTUP_OVR_2,
				    (u8)dphy->freq_settings->ddlfreq);

	temp = mipi_dphy_read_reg(ifx,
				  dphy4rxtester_DIG_RDWR_RX_RX_STARTUP_OVR_2);

	temp = mipi_dphy_read_reg(ifx,
				  dphy4rxtester_DIG_RDWR_RX_RX_STARTUP_OVR_3);

	temp |= (dphy->freq_settings->ddlfreq >> 8) & 0x0F;

	mipi_dphy_write_reg(ifx,
			    dphy4rxtester_DIG_RDWR_RX_RX_STARTUP_OVR_3,
				    temp);

	temp = mipi_dphy_read_reg(ifx,
				  dphy4rxtester_DIG_RDWR_RX_RX_STARTUP_OVR_4);

	temp |= BIT(0);

	mipi_dphy_write_reg(ifx,
			    dphy4rxtester_DIG_RDWR_RX_RX_STARTUP_OVR_4,
				    temp);

	return 0;
}

static int snps_dphy_configure(struct phy *phy,
			       union phy_configure_opts *opts)
{
	struct snps_dphy *dphy = phy_get_drvdata(phy);
	enum snps_dphy_block block = (phy == dphy->phy[SNPS_DPHY_TX]) ?
				SNPS_DPHY_TX : SNPS_DPHY_RX;
	dphy->inst[block].num_lanes = opts->mipi_dphy.lanes;
	dphy->inst[block].mbps =
		div_u64(opts->mipi_dphy.hs_clk_rate, HZ_PER_MHZ);

	dphy->freq_settings = dphy_configuration(phy, dphy->inst[block].mbps);
	if (!dphy->freq_settings)
		return -EINVAL;
	return 0;
}

static int snps_dphy_set_phy_state(struct snps_dphy *dphy, struct phy *phy, bool on)
{
	int ret;
	u32 temp;
	enum snps_dphy_block block = (phy == dphy->phy[SNPS_DPHY_TX]) ?
				SNPS_DPHY_TX : SNPS_DPHY_RX;
	enum snps_phy_submode submode = dphy->inst[block].sub_mode;

	if (on) {
		if (dphy->ops->power_on) {
			ret = dphy->ops->power_on(dphy->dev, block, submode);
			if (ret)
				return ret;
		}

		if (block == SNPS_DPHY_TX) {
			temp = readl(dphy->dsi_regs + DSI_PHY_RSTZ);
			temp |= BIT(DSI_PHY_RSTZ_PHY_SHUTDOWNZ) |
				BIT(DSI_PHY_RSTZ_PHY_RSTZ) |
				BIT(DSI_PHY_RSTZ_PHY_FORCEPLL) |
				BIT(DSI_PHY_RSTZ_PHY_ENABLECLK);
			writel(temp, dphy->dsi_regs + DSI_PHY_RSTZ);
		}
	} else {
		if (dphy->ops->power_off) {
			ret = dphy->ops->power_off(dphy->dev, block);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int snps_dphy_power_on(struct phy *phy)
{
	struct snps_dphy *dphy = phy_get_drvdata(phy);

	return snps_dphy_set_phy_state(dphy, phy, true);
}

static int snps_dphy_power_off(struct phy *phy)
{
	struct snps_dphy *dphy = phy_get_drvdata(phy);

	return snps_dphy_set_phy_state(dphy, phy, false);
}

static int snps_dphy_init(struct phy *phy)
{
	return 0;
}

static int snps_dphy_exit(struct phy *phy)
{
	return 0;
}

static int snps_dphy_validate(struct phy *phy, enum phy_mode mode, int submode,
			      union phy_configure_opts *opts)
{
	return 0;
}

static int snps_dphy_reset(struct phy *phy)
{
	return 0;
}

static int snps_dphy_calibrate(struct phy *phy)
{
	return 0;
}

static const struct phy_ops snps_dphy_ops = {
	.init = snps_dphy_init,
	.exit = snps_dphy_exit,
	.configure = snps_dphy_configure,
	.validate = snps_dphy_validate,
	.power_on = snps_dphy_power_on,
	.power_off = snps_dphy_power_off,
	.reset = snps_dphy_reset,
	.calibrate = snps_dphy_calibrate,
};

static void snps_dphy_parse_defaults(struct snps_dphy *dphy)
{
	u32 val;

	if (!of_property_read_u32(dphy->dev->of_node, "snps,tx-dphy-mode",
				  &val))
		dphy->inst[SNPS_DPHY_TX].sub_mode = val;
	else
		dphy->inst[SNPS_DPHY_TX].sub_mode = PHY_SUBMODE_TX;
}

static int snps_alloc_soc(struct platform_device *pdev)
{
	struct alif_snps_dphy *alif;
	struct snps_dphy *dphy;

	dphy = dev_get_drvdata(&pdev->dev);
	if (!dphy)
		return -ENODEV;

	alif = devm_kzalloc(&pdev->dev, sizeof(*alif), GFP_KERNEL);
	if (!alif)
		return -ENOMEM;

	alif->soc_regs = devm_platform_ioremap_resource_byname(pdev, "soc_regs");
	if (IS_ERR(alif->soc_regs))
		return PTR_ERR(alif->soc_regs);

	alif->vbat_regs = devm_platform_ioremap_resource_byname(pdev, "vbat");
	if (IS_ERR(alif->vbat_regs))
		return PTR_ERR(alif->vbat_regs);

	dphy->vend_priv = alif;
	writel(VBAT_PWR_ON_INIT, alif->vbat_regs);

	return 0;
}

static int snps_dphy_probe(struct platform_device *pdev)
{
	struct phy_provider *phy_provider;
	struct snps_dphy *dphy;
	u32 base;
	int ret;

	dphy = devm_kzalloc(&pdev->dev, sizeof(*dphy), GFP_KERNEL);
	if (!dphy)
		return -ENOMEM;
	dphy->dev = &pdev->dev;
	dev_set_drvdata(&pdev->dev, dphy);

	ret = snps_alloc_soc(pdev);
	if (ret)
		return ret;
	dphy->ops = of_device_get_match_data(&pdev->dev);
	if (!dphy->ops)
		return -EINVAL;

	/*
	 * CSI and DSI register regions are shared with their respective
	 * controllers. Use devm_ioremap (no resource reservation) via
	 * custom DT properties to avoid -EBUSY conflicts.
	 */

	if (of_property_read_u32(pdev->dev.of_node, "alif,csi-base", &base)) {
		dev_err(&pdev->dev, "missing alif,csi-base property\n");
		return -EINVAL;
	}
	dphy->csi_regs = devm_ioremap(&pdev->dev, base, SZ_4K);
	if (!dphy->csi_regs) {
		dev_err(&pdev->dev, "failed to ioremap csi regs\n");
		return -ENOMEM;
	}

	if (of_property_read_u32(pdev->dev.of_node, "alif,dsi-base", &base)) {
		dev_err(&pdev->dev, "missing alif,dsi-base property\n");
		return -EINVAL;
	}
	dphy->dsi_regs = devm_ioremap(&pdev->dev, base, SZ_4K);
	if (!dphy->dsi_regs) {
		dev_err(&pdev->dev, "failed to ioremap dsi regs\n");
		return -ENOMEM;
	}

	dphy->rx_dphy_clk = devm_clk_get(&pdev->dev, "rx_clk");
	if (IS_ERR(dphy->rx_dphy_clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(dphy->rx_dphy_clk),
				     "failed to get rx_clk\n");

	dphy->tx_dphy_clk = devm_clk_get(&pdev->dev, "tx_clk");
	if (IS_ERR(dphy->tx_dphy_clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(dphy->tx_dphy_clk),
				     "failed to get tx_clk\n");

	dphy->pll_ref_clk = devm_clk_get(&pdev->dev, "pll_ref_clk");
	if (IS_ERR(dphy->pll_ref_clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(dphy->pll_ref_clk),
				     "failed to get pll_ref_clk\n");

	if (of_property_read_u32(pdev->dev.of_node, "cfg-clk-frequency",
				 &dphy->cfg_clk_freq))
		dphy->cfg_clk_freq = clk_get_rate(dphy->rx_dphy_clk);

	dphy->phy[SNPS_DPHY_TX] = devm_phy_create(&pdev->dev, NULL, &snps_dphy_ops);
	if (IS_ERR(dphy->phy[SNPS_DPHY_TX])) {
		dev_err(&pdev->dev, "failed to create TX PHY\n");
		return PTR_ERR(dphy->phy[SNPS_DPHY_TX]);
	}
	phy_set_drvdata(dphy->phy[SNPS_DPHY_TX], dphy);
	dphy->inst[SNPS_DPHY_TX].phy_caps = PHY_SUBMODE_TX | PHY_SUBMODE_RX;
	snps_dphy_parse_defaults(dphy);

	dphy->phy[SNPS_DPHY_RX] = devm_phy_create(&pdev->dev, NULL, &snps_dphy_ops);
	if (IS_ERR(dphy->phy[SNPS_DPHY_RX])) {
		dev_err(&pdev->dev, "failed to create RX PHY\n");
		return PTR_ERR(dphy->phy[SNPS_DPHY_RX]);
	}
	phy_set_drvdata(dphy->phy[SNPS_DPHY_RX], dphy);
	dphy->inst[SNPS_DPHY_RX].phy_caps = PHY_SUBMODE_RX;
	dphy->inst[SNPS_DPHY_RX].sub_mode = PHY_SUBMODE_RX;

	phy_provider = devm_of_phy_provider_register(&pdev->dev,
						     snps_dphy_xlate);
	return PTR_ERR_OR_ZERO(phy_provider);
}

static void snps_dphy_remove(struct platform_device *pdev)
{
	struct snps_dphy *dphy = dev_get_drvdata(&pdev->dev);

	if (dphy->ops && dphy->ops->power_off) {
		dphy->ops->power_off(dphy->dev, SNPS_DPHY_TX);
		dphy->ops->power_off(dphy->dev, SNPS_DPHY_RX);
	}
}

static const struct of_device_id snps_dphy_of_match[] = {
	{ .compatible = "snps,dphy", .data = &alif_dphy_ops },
	{ .compatible = "alif,dphy", .data = &alif_dphy_ops },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, snps_dphy_of_match);

static struct platform_driver snps_dphy_plat_driver = {
	.probe = snps_dphy_probe,
	.remove_new = snps_dphy_remove,
	.driver = {
		.name = "snps-mipi-dphy",
		.of_match_table = snps_dphy_of_match,
	},
};
module_platform_driver(snps_dphy_plat_driver);

MODULE_AUTHOR("Yogender Kumar Arya <yogender.kumar@alifsemi.com>");
MODULE_DESCRIPTION("Synopsys MIPI D-PHY driver");
MODULE_LICENSE("GPL");

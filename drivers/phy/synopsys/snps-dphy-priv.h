/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _SNPS_DPHY_PRIV_H_
#define _SNPS_DPHY_PRIV_H_

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/types.h>

struct snps_dphy_freq_range {
	u32 bitrate;
	u32 hsfreqrange;
	u32 ddlfreq;
	u16 clk_hs2lp;
	u16 clk_lp2hs;
	u16 lane_hs2lp;
	u16 lane_lp2hs;
};

#define NUM_PHYS	2
#define NUM_LANES_MIN	1
#define NUM_LANES_MAX	4

#define HZ_PER_MHZ		1000000UL
#define DPHY_RX_MIN_MBPS	80

#define CSI_TSTCTRL0	0x50
#define CSI_TSTCTRL1	0x54

#define DSI_TSTCTRL0	0xB4
#define DSI_TSTCTRL1	0xB8

/* DPHY TX Regs */
#define dphy4txtester_DIG_RDWR_TX_SYS_3			0x004
#define dphy4txtester_DIG_RD_TX_SYS_0			0x01e
#define dphy4txtester_DIG_RD_TX_SYS_1			0x01f
#define dphy4txtester_DIG_RDWR_TX_CB_0			0x1aa
#define dphy4txtester_DIG_RDWR_TX_CB_1			0x1ab
#define dphy4txtester_DIG_RDWR_TX_CB_2			0x1ac
#define dphy4txtester_DIG_RDWR_TX_CB_3			0x1ad
#define dphy4txtester_DIG_RDWR_TX_DAC_0			0x1da
#define dphy4txtester_DIG_RD_TX_DAC_0			0x1f2
#define dphy4txtester_DIG_RDWR_TX_SLEW_0		0x26b
#define dphy4txtester_DIG_RDWR_TX_SLEW_5		0x270
#define dphy4txtester_DIG_RDWR_TX_SLEW_6		0x271
#define dphy4txtester_DIG_RDWR_TX_SLEW_7		0x272
#define dphy4txtester_DIG_RDWR_TX_CLK_TERMLOWCAP	0x402
#define dphy4txtester_DIG_RDWR_TX_LANE0_LANE_0		0x501
#define dphy4txtester_DIG_RDWR_TX_LANE1_LANE_0		0x701
#define dphy4txtester_DIG_RDWR_TX_LANE1_SLEWRATE_0	0x70b
#define dphy4txtester_DIG_RDWR_TX_LANE2_SLEWRATE_0	0x90b
#define dphy4txtester_DIG_RDWR_TX_LANE3_SLEWRATE_0	0xb0b

/* TX PLL register index */
#define dphy4txtester_DIG_RDWR_TX_PLL_0			0x15d
#define dphy4txtester_DIG_RDWR_TX_PLL_1			0x15e
#define dphy4txtester_DIG_RDWR_TX_PLL_5			0x162
#define dphy4txtester_DIG_RDWR_TX_PLL_9			0x166
#define dphy4txtester_DIG_RDWR_TX_PLL_10		0x167
#define dphy4txtester_DIG_RDWR_TX_PLL_13		0x16a
#define dphy4txtester_DIG_RDWR_TX_PLL_17		0x16e
#define dphy4txtester_DIG_RDWR_TX_PLL_27		0x178
#define dphy4txtester_DIG_RDWR_TX_PLL_28		0x179
#define dphy4txtester_DIG_RDWR_TX_PLL_29		0x17a
#define dphy4txtester_DIG_RDWR_TX_PLL_30		0x17b
#define dphy4txtester_DIG_RD_TX_PLL_0			0x191

/* DPHY RX Regs */
#define dphy4rxtester_DIG_RD_RX_SYS_1			0x01f
#define dphy4rxtester_DIG_RDWR_RX_RX_STARTUP_OVR_2	0x0e2
#define dphy4rxtester_DIG_RDWR_RX_RX_STARTUP_OVR_3	0x0e3
#define dphy4rxtester_DIG_RDWR_RX_RX_STARTUP_OVR_4	0x0e4
#define dphy4rxtester_DIG_RDWR_RX_RX_STARTUP_OVR_17	0x0f1
#define dphy4rxtester_DIG_RDWR_RX_BIST_3		0x10a
#define dphy4rxtester_DIG_RDWR_RX_CLKLANE_LANE_6	0x307
#define dphy4rxtester_DIG_RD_RX_CLKLANE_OFFSET_CAL_3	0x39c
#define dphy2rxtester_DIG_RD_RX_CLKLANE_OFFSET_CAL_0	0x39d
#define dphy4rxtester_DIG_RDWR_RX_LANE0_LANE_9		0x50a
#define dphy4rxtester_DIG_RDWR_RX_LANE0_LANE_12		0x50d
#define dphy4rxtester_DIG_RD_RX_LANE0_LANE_7		0x532
#define dphy4rxtester_DIG_RD_RX_LANE0_LANE_8		0x533
#define dphy2rxtester_DIG_RD_RX_LANE0_OFFSET_CAL_0	0x58d
#define dphy2rxtester_DIG_RD_RX_LANE0_OFFSET_CAL_2	0x5a1
#define dphy2rxtester_DIG_RD_RX_LANE0_DDL_0		0x5e0
#define dphy2rxtester_DIG_RD_RX_LANE0_DDL_5		0x5e5
#define dphy4rxtester_DIG_RDWR_RX_LANE1_LANE_9		0x70a
#define dphy4rxtester_DIG_RDWR_RX_LANE1_LANE_12		0x70d
#define dphy4rxtester_DIG_RD_RX_LANE1_LANE_7		0x732
#define dphy4rxtester_DIG_RD_RX_LANE1_LANE_8		0x733
#define dphy2rxtester_DIG_RD_RX_LANE1_OFFSET_CAL_0	0x79f
#define dphy2rxtester_DIG_RD_RX_LANE1_OFFSET_CAL_2	0x7a1
#define dphy4rxtester_DIG_RD_RX_LANE1_DDL_0		0x7e0
#define dphy2rxtester_DIG_RD_RX_LANE1_DDL_5		0x7e5

#define DPHY_CTRL0_TESTPORT_SEL	4

enum snps_dphy_block {
	SNPS_DPHY_TX = 0,
	SNPS_DPHY_RX,
};

enum snps_phy_submode {
	PHY_SUBMODE_TX = BIT(0),
	PHY_SUBMODE_RX = BIT(1),
};

struct vend_ops {
	int (*init)(struct device *dev);
	int (*power_on)(struct device *dev,
			enum snps_dphy_block block,
			enum snps_phy_submode submode);
	int (*power_off)(struct device *dev,
			 enum snps_dphy_block block);
	int (*post_setup)(struct device *dev,
			  enum snps_dphy_block block);
};

struct snps_dphy_inst {
	u8 phy_caps;
	u8 num_lanes;
	enum snps_phy_submode sub_mode;
	u64 mbps;
};

struct snps_dphy {
	struct device *dev;
	void __iomem *csi_regs;
	void __iomem *dsi_regs;
	void __iomem *phy_ctrl0;

	struct clk *rx_dphy_clk;
	struct clk *tx_dphy_clk;
	struct clk *pll_ref_clk;

	u32 cfg_clk_freq;

	u8 num_lanes;
	u64 mbps;
	const struct snps_dphy_freq_range *freq_settings;
	struct phy *phy[NUM_PHYS];
	struct snps_dphy_inst inst[NUM_PHYS];

	const struct vend_ops *ops;
	void *vend_priv;
};

struct testctrl {
	void __iomem *ctrl0;
	void __iomem *ctrl1;
};

int snps_dphy_slave_setup(struct snps_dphy *dphy, struct testctrl *ifx);

static inline struct phy *snps_dphy_xlate(struct device *dev,
					  const struct of_phandle_args *args)
{
	struct snps_dphy *dphy = dev_get_drvdata(dev);
	u32 phy_id = args->args[0];

	if (phy_id > SNPS_DPHY_RX) {
		dev_err(dev, "invalid PHY ID %u\n", phy_id);
		return ERR_PTR(-EINVAL);
	}

	return dphy->phy[phy_id];
}

static inline void dw_dphy_write_msk(void __iomem *reg, u32 data, u32 shift, u32 width)
{
	u32 val;
	u32 mask = GENMASK(width - 1, 0);

	val = readl(reg);
	val &= ~(mask << shift);
	val |= (data & mask) << shift;
	writel(val, reg);
}

static inline u32 dw_dphy_read_msk(void __iomem *reg, u8 shift, u8 width)
{
	return (readl(reg) >> shift) & GENMASK(width - 1, 0);
}

const struct snps_dphy_freq_range *dphy_configuration(struct phy *phy, u64 mbps);

u8 mipi_dphy_read_reg(struct testctrl *ifx, u16 address);
void mipi_dphy_write_reg(struct testctrl *ifx, u16 address, u8 data);

#endif /* _SNPS_DPHY_PRIV_H_ */

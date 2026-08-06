// SPDX-License-Identifier: GPL-2.0
/*
 * PDM Driver for Alif PDM module
 * Copyright (C) 2021-2025 Alif Semiconductor - All Rights Reserved.
 * Use, distribution and modification of this code is permitted under the
 * terms stated in the Alif Semiconductor Software License Agreement
 *
 * You should have received a copy of the Alif Semiconductor Software
 * License Agreement with this file. If not, please write to:
 * contact@alifsemi.com, or visit: https://alifsemi.com/license
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/dmaengine_pcm.h>
#include <linux/debugfs.h>
#include <linux/pm_runtime.h>

#include "fir_coefficient_defines.h"

#define MODE_FREQ_8K		8000
#define MODE_FREQ_16K		16000
#define MODE_FREQ_32K		32000
#define MODE_FREQ_48K		48000
#define MODE_FREQ_96K		96000
#define MODE_FREQ_192K		192000

#define REGISTER_BITS		32
#define FIFO_WATERMARK_DEFAULT	0x1
#define FIFO_WATERMARK_HIGH	0x6
#define PDM_CTL0_DEFAULT	0x0
#define PDM_CTL1_DEFAULT	(0x1 << BYPASS_IIR_FILTER)
#define PDM_DEFAULT_MODE	0
#define PDM_IRQ_ENABLE_DEFAULT	0x0
#define BYPASS_IIR_FILTER	2
#define PDM_MODE_POS		16
#define PDM_MODE_MASK		(0xF << 16)
#define PDM_CHANNEL_MASK	0xFF
#define MAX_CHANNELS		8
#define MIN_CHANNELS		1
#define CHANNEL_OFFSET		0x100
#define NUM_COEFFICIENTS	18

#define BITS_PER_SAMPLE		16
#define MIN_PERIODS             (4)
#define MAX_PERIODS             (MAX_BUFFER_BYTES / MIN_PERIOD_BYTES)
#define MIN_PERIOD_BYTES	(6400)
#define MAX_BUFFER_BYTES	(2 * MIN_PERIOD_BYTES * MIN_PERIODS * 10)
#define MAX_PERIOD_BYTES	(MAX_BUFFER_BYTES / MIN_PERIODS)

/* Register Offsets */
#define PDM_CTL0_REG			0x0000
#define PDM_CTL1_REG			0x0004
#define PDM_FIFO_WATERMARK_H_REG	0x0008
#define PDM_FIFO_STAT_REG		0x000C
#define PDM_IRQ_ENABLE_REG		0x001C
#define PDM_WARNING_IRQ_REG		0x0014
#define PDM_ERROR_IRQ_REG		0x0010
#define PDM_AUDIOOUT_CH0_CH1_REG	0x0020
#define PDM_AUDIOOUT_CH2_CH3_REG	0x0024
#define PDM_AUDIOOUT_CH4_CH5_REG	0x0028
#define PDM_AUDIOOUT_CH6_CH7_REG	0x002C

#define FIFO_FULL_IRQ_EN		BIT(0)
#define FIFO_OVERFLOW_IRQ_EN		BIT(1)
#define ALL_CH_AUDIO_DETECT_IRQ_EN	0xFF00
#define PDM_MAX_REG			0x7D4

#define EVEN_CH_DATA(n)			((n) & 0xFFFF)
#define ODD_CH_DATA(n)			(((n) >> 16) & 0xFFFF)

#define CHANNEL_0			BIT(0)
#define CHANNEL_1			BIT(1)
#define CHANNEL_2			BIT(2)
#define CHANNEL_3			BIT(3)
#define CHANNEL_4			BIT(4)
#define CHANNEL_5			BIT(5)
#define CHANNEL_6			BIT(6)
#define CHANNEL_7			BIT(7)

/* PDM Operating Modes (encoded in PDM_CTL0[19:16]) */
#define PDM_MODE_1_8K				0x1
#define PDM_MODE_2_16K				0x2
#define PDM_MODE_3_16K				0x3
#define PDM_MODE_4_16K				0x4
#define PDM_MODE_5_32K				0x5
#define PDM_MODE_6_48K				0x6
#define PDM_MODE_7_48K				0x7
#define PDM_MODE_8_96K				0x8
#define PDM_MODE_9_192K				0x9

static const u32 fir_coefficients_even[NUM_COEFFICIENTS] = {
	0x00000000, 0x000007FF, 0x00000000, 0x00000004, 0x00000004,
	0x000007FC, 0x00000000, 0x000007FB, 0x000007E4, 0x00000000,
	0x0000002B, 0x00000009, 0x00000016, 0x00000049, 0x00000793,
	0x000006F8, 0x00000045, 0x00000178
};

static const u32 fir_coefficients_odd[NUM_COEFFICIENTS] = {
	0x00000001, 0x00000003, 0x00000003, 0x000007F4, 0x00000004,
	0x000007ED, 0x000007F5, 0x000007F4, 0x000007D3, 0x000007FE,
	0x000007BC, 0x000007E5, 0x000007D9, 0x00000793, 0x00000029,
	0x0000072C, 0x00000072, 0x000002FD
};

static const u32 iir_coeff_sel = 4;

static const u32 phase_values[MAX_CHANNELS] = {
	0x00000003, 0x0000001F, 0x00000003, 0x0000001F,
	0x0000001F, 0x00000003, 0x0000001F, 0x00000003
};

static const u32 gain_values[MAX_CHANNELS] = {
	0x00000013, 0x0000000D, 0x00000013, 0x0000000D,
	0x0000000D, 0x00000013, 0x0000000D, 0x00000013
};

static const u32 pkdet_th_values = 0x00060002;

static const u32 pkdet_itv_values[MAX_CHANNELS] = {
	0x00020027, 0x0004002D, 0x00020027, 0x0004002D,
	0x0004002D, 0x00020027, 0x0004002D, 0x00020027
};

static ssize_t  modefreq_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t  modefreq_store(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count);
static ssize_t  channelsel_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t  channelsel_store(struct device *dev, struct device_attribute *attr,
				 const char *buf, size_t count);

static DEVICE_ATTR_RW(modefreq);
static DEVICE_ATTR_RW(channelsel);

struct alif_pcm_dev {
	struct device *dev;
	struct regmap *regmap;
	struct clk *pdm_clk;
	unsigned int pdm_mode;
	unsigned char channel;
	snd_pcm_uframes_t pdm_buffer_ptr;
	snd_pcm_uframes_t pdm_buffer_index;
	struct snd_pcm_substream __rcu *pdm_substream;
};

static const struct reg_default alif_pdm_reg_defaults[] = {
	{ PDM_CTL0_REG,	PDM_CTL0_DEFAULT },
	{ PDM_CTL1_REG,	PDM_CTL1_DEFAULT },
	{ PDM_FIFO_WATERMARK_H_REG, FIFO_WATERMARK_DEFAULT },
	{ PDM_IRQ_ENABLE_REG, PDM_IRQ_ENABLE_DEFAULT },
};

static bool alif_pdm_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case PDM_CTL0_REG:
	case PDM_CTL1_REG:
	case PDM_FIFO_WATERMARK_H_REG:
	case PDM_FIFO_STAT_REG:
	case PDM_WARNING_IRQ_REG:
	case PDM_ERROR_IRQ_REG:
	case PDM_AUDIOOUT_CH0_CH1_REG:
	case PDM_AUDIOOUT_CH2_CH3_REG:
	case PDM_AUDIOOUT_CH4_CH5_REG:
	case PDM_AUDIOOUT_CH6_CH7_REG:
		return true;
	default:
		return false;
	}
}

static bool alif_pdm_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case PDM_FIFO_STAT_REG:
	case PDM_WARNING_IRQ_REG:
	case PDM_ERROR_IRQ_REG:
	case PDM_AUDIOOUT_CH0_CH1_REG:
	case PDM_AUDIOOUT_CH2_CH3_REG:
	case PDM_AUDIOOUT_CH4_CH5_REG:
	case PDM_AUDIOOUT_CH6_CH7_REG:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config alif_pdm_regmap_config = {
	.reg_bits = REGISTER_BITS,
	.val_bits = REGISTER_BITS,
	.reg_defaults = alif_pdm_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(alif_pdm_reg_defaults),
	.readable_reg = alif_pdm_readable_reg,
	.volatile_reg = alif_pdm_volatile_reg,
	.max_register = PDM_MAX_REG,
	.cache_type = REGCACHE_FLAT,
};

/* FIR Coefficients Setup */
static void pcm_setup(struct alif_pcm_dev *dev)
{
	for (int ch = 0; ch < MAX_CHANNELS; ch++) {
		const u32 *coefficients = (ch % 2 == 0) ?
				fir_coefficients_even : fir_coefficients_odd;

		for (int i = 0; i < NUM_COEFFICIENTS; i++)
			regmap_write(dev->regmap, PDM_CH0_FIR_COEF_0 + ch * CHANNEL_OFFSET + i * 4,
				     coefficients[i]);

		regmap_write(dev->regmap, PDM_CH0_IIR_COEF_SEL + ch * CHANNEL_OFFSET,
			     iir_coeff_sel);
		regmap_write(dev->regmap, PDM_CH0_PHASE + ch * CHANNEL_OFFSET,
			     phase_values[ch]);
		regmap_write(dev->regmap, PDM_CH0_GAIN + ch * CHANNEL_OFFSET,
			     gain_values[ch]);
		regmap_write(dev->regmap, PDM_CH0_PKDET_TH + ch * CHANNEL_OFFSET,
			     pkdet_th_values);
		regmap_write(dev->regmap, PDM_CH0_PKDET_ITV + ch * CHANNEL_OFFSET,
			     pkdet_itv_values[ch]);
	}
}

/* Sysfs Functions */
static ssize_t modefreq_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct alif_pcm_dev *pdev = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", pdev->pdm_mode);
}

static ssize_t modefreq_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct alif_pcm_dev *pdev = dev_get_drvdata(dev);
	int val;

	if (kstrtoint(buf, 0, &val))
		return -EINVAL;

	pdev->pdm_mode = val;
	return count;
}

static ssize_t channelsel_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct alif_pcm_dev *pdev = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", pdev->channel);
}

static ssize_t channelsel_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct alif_pcm_dev *pdev = dev_get_drvdata(dev);
	unsigned int val;

	if (kstrtouint(buf, 0, &val))
		return -EINVAL;

	pdev->channel = val & PDM_CHANNEL_MASK;
	return count;
}

/* Interrupt Handler */
static irqreturn_t alif_pcm_irq_handler(int irq, void *dev_id)
{
	struct alif_pcm_dev *dev = dev_id;
	unsigned int status;

	regmap_read(dev->regmap, PDM_WARNING_IRQ_REG, &status);
	if (status & FIFO_FULL_IRQ_EN)
		return IRQ_WAKE_THREAD;

	return IRQ_NONE;
}

static irqreturn_t alif_pcm_irq_thread(int irq, void *dev_id)
{
	struct alif_pcm_dev *dev = dev_id;
	struct snd_pcm_substream *substream;
	unsigned int audio_ch01, audio_ch23, audio_ch45, audio_ch67;
	unsigned short *buffer;
	unsigned int fifo_count;
	snd_pcm_uframes_t period_size, buffer_size_bytes;

	rcu_read_lock();
	substream = rcu_dereference(dev->pdm_substream);
	if (!substream)
		goto out;

	buffer = (unsigned short *)substream->runtime->dma_area;
	buffer_size_bytes = substream->runtime->buffer_size *
			    substream->runtime->channels * (BITS_PER_SAMPLE / 8);
	period_size = substream->runtime->period_size;
	regmap_read(dev->regmap, PDM_FIFO_STAT_REG, &fifo_count);

	while (fifo_count) {
		regmap_read(dev->regmap, PDM_AUDIOOUT_CH0_CH1_REG, &audio_ch01);
		regmap_read(dev->regmap, PDM_AUDIOOUT_CH2_CH3_REG, &audio_ch23);
		regmap_read(dev->regmap, PDM_AUDIOOUT_CH4_CH5_REG, &audio_ch45);
		regmap_read(dev->regmap, PDM_AUDIOOUT_CH6_CH7_REG, &audio_ch67);

		if (dev->channel & CHANNEL_0) {
			buffer[dev->pdm_buffer_index++] = EVEN_CH_DATA(audio_ch01);
			dev->pdm_buffer_index %= buffer_size_bytes / 2;
		}
		if (dev->channel & CHANNEL_1) {
			buffer[dev->pdm_buffer_index++] = ODD_CH_DATA(audio_ch01);
			dev->pdm_buffer_index %= buffer_size_bytes / 2;
		}
		if (dev->channel & CHANNEL_2) {
			buffer[dev->pdm_buffer_index++] = EVEN_CH_DATA(audio_ch23);
			dev->pdm_buffer_index %= buffer_size_bytes / 2;
		}
		if (dev->channel & CHANNEL_3) {
			buffer[dev->pdm_buffer_index++] = ODD_CH_DATA(audio_ch23);
			dev->pdm_buffer_index %= buffer_size_bytes / 2;
		}
		if (dev->channel & CHANNEL_4) {
			buffer[dev->pdm_buffer_index++] = EVEN_CH_DATA(audio_ch45);
			dev->pdm_buffer_index %= buffer_size_bytes / 2;
		}
		if (dev->channel & CHANNEL_5) {
			buffer[dev->pdm_buffer_index++] = ODD_CH_DATA(audio_ch45);
			dev->pdm_buffer_index %= buffer_size_bytes / 2;
		}
		if (dev->channel & CHANNEL_6) {
			buffer[dev->pdm_buffer_index++] = EVEN_CH_DATA(audio_ch67);
			dev->pdm_buffer_index %= buffer_size_bytes / 2;
		}
		if (dev->channel & CHANNEL_7) {
			buffer[dev->pdm_buffer_index++] = ODD_CH_DATA(audio_ch67);
			dev->pdm_buffer_index %= buffer_size_bytes / 2;
		}

		fifo_count--;
	}
	dev->pdm_buffer_ptr = bytes_to_frames(substream->runtime,
					      dev->pdm_buffer_index * 2);
	if (((dev->pdm_buffer_ptr % period_size) + MAX_CHANNELS) >= period_size)
		snd_pcm_period_elapsed(substream);
out:
	rcu_read_unlock();
	return IRQ_HANDLED;
}

/* PCM Operations */
static int alif_pcm_hw_params(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct alif_pcm_dev *dev = snd_soc_dai_get_drvdata(dai);
	unsigned int rate = params_rate(params);
	unsigned int reg_val;
	int ret;

	regmap_write(dev->regmap, PDM_CTL1_REG,	PDM_CTL1_DEFAULT);

	/* Use higher watermark for sample rates >= 96KHz */
	if (rate >= MODE_FREQ_96K)
		regmap_write(dev->regmap, PDM_FIFO_WATERMARK_H_REG, FIFO_WATERMARK_HIGH);
	else
		regmap_write(dev->regmap, PDM_FIFO_WATERMARK_H_REG, FIFO_WATERMARK_DEFAULT);

	switch (rate) {
	case MODE_FREQ_8K:
	{
		reg_val = PDM_MODE_1_8K;
		break;
	}
	case MODE_FREQ_16K:
	{
		switch (dev->pdm_mode) {
		case 2:
		{
			reg_val = PDM_MODE_2_16K;
			break;
		}
		case 3:
		{
			reg_val = PDM_MODE_3_16K;
			break;
		}
		case 4:
		{
			reg_val = PDM_MODE_4_16K;
			break;
		}
		default:
		{
			reg_val = PDM_MODE_2_16K;
			break;
		}
		}
		break;
	}
	case MODE_FREQ_32K:
	{
		reg_val = PDM_MODE_5_32K;
		break;
	}
	case MODE_FREQ_48K:
	{
		switch (dev->pdm_mode) {
		case 6:
		{
			reg_val = PDM_MODE_6_48K;
			break;
		}
		case 7:
		{
			reg_val = PDM_MODE_7_48K;
			break;
		}
		default:
		{
			reg_val = PDM_MODE_6_48K;
			break;
		}
		}
		break;
	}
	case MODE_FREQ_96K:
	{
		reg_val = PDM_MODE_8_96K;
		break;
	}
	case MODE_FREQ_192K:
	{
		reg_val = PDM_MODE_9_192K;
		break;
	}
	default:
		dev_err(dev->dev, "Unsupported sample rate: %u\n", rate);
		return -EINVAL;
	}

	dev->pdm_mode = reg_val;
	regmap_update_bits(dev->regmap, PDM_CTL0_REG, PDM_MODE_MASK,
			   reg_val << PDM_MODE_POS);
	regmap_update_bits(dev->regmap, PDM_CTL0_REG,
			   PDM_CHANNEL_MASK, dev->channel);

	pcm_setup(dev);

	ret = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(params));
	if (ret < 0)
		return ret;

	return 0;
}

static int alif_pcm_trigger(struct snd_pcm_substream *substream, int cmd,
			    struct snd_soc_dai *dai)
{
	struct alif_pcm_dev *dev = snd_soc_dai_get_drvdata(dai);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	{
		WRITE_ONCE(dev->pdm_buffer_ptr, 0);
		WRITE_ONCE(dev->pdm_buffer_index, 0);
		rcu_assign_pointer(dev->pdm_substream, substream);
		regmap_write(dev->regmap, PDM_IRQ_ENABLE_REG, FIFO_FULL_IRQ_EN |
			     FIFO_OVERFLOW_IRQ_EN | ALL_CH_AUDIO_DETECT_IRQ_EN);
		break;
	}
	case SNDRV_PCM_TRIGGER_STOP:
	{
		rcu_assign_pointer(dev->pdm_substream, NULL);
		regmap_write(dev->regmap, PDM_IRQ_ENABLE_REG, 0);
		break;
	}
	default:
		return -EINVAL;
	}
	return 0;
}

static snd_pcm_uframes_t alif_pcm_pointer(struct snd_soc_component *component,
					  struct snd_pcm_substream *substream)
{
	struct alif_pcm_dev *dev = substream->runtime->private_data;

	return READ_ONCE(dev->pdm_buffer_ptr);
}

static const struct snd_pcm_hardware alif_pcm_hardware = {
	.info = SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_NONINTERLEAVED,
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.channels_min = MIN_CHANNELS,
	.channels_max = MAX_CHANNELS,
	.rate_min = MODE_FREQ_8K,
	.rate_max = MODE_FREQ_192K,
	.period_bytes_min = MIN_PERIOD_BYTES,
	.period_bytes_max = MAX_PERIOD_BYTES,
	.buffer_bytes_max = MAX_BUFFER_BYTES,
	.periods_min = MIN_PERIODS,
	.periods_max = MAX_PERIODS,
};

static int alif_pcm_open(struct snd_soc_component *component,
			 struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct alif_pcm_dev *dev =
			snd_soc_dai_get_drvdata(snd_soc_rtd_to_cpu(rtd, 0));

	substream->f_flags = 0;
	snd_soc_set_runtime_hwparams(substream, &alif_pcm_hardware);
	runtime->private_data = dev;
	return 0;
}

static int alif_pcm_new(struct snd_soc_component *component,
			struct snd_soc_pcm_runtime *rtd)
{
	size_t size = alif_pcm_hardware.buffer_bytes_max;

	return snd_pcm_set_managed_buffer_all(rtd->pcm,
			SNDRV_DMA_TYPE_CONTINUOUS,
			NULL, size, size);
}

static int alif_pcm_startup(struct snd_pcm_substream *substream,
			    struct snd_soc_dai *dai)
{
	struct alif_pcm_dev *dev = snd_soc_dai_get_drvdata(dai);

	dev_info(dev->dev, "Alif PCM startup\n");
	return 0;
}

static int pcm_dai_probe(struct snd_soc_dai *dai)
{
	return 0;
}

static const struct snd_soc_dai_ops alif_pcm_dai_ops = {
	.startup	= alif_pcm_startup,
	.hw_params	= alif_pcm_hw_params,
	.trigger	= alif_pcm_trigger,
	.probe		= pcm_dai_probe,
};

static struct snd_soc_dai_driver alif_pcm_dai = {
	.name = "alifpcm",
	.capture = {
			.stream_name = "alif-pcm",
			.channels_min = MIN_CHANNELS,
			.channels_max = MAX_CHANNELS,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &alif_pcm_dai_ops,
};

static const struct snd_soc_component_driver alif_pcm_component = {
	.name = "alif-pcm",
	.open = alif_pcm_open,
	.pcm_construct = alif_pcm_new,
	.pointer = alif_pcm_pointer,
};

static int alif_pcm_probe(struct platform_device *pdev)
{
	struct alif_pcm_dev *dev;
	void __iomem *regs;
	int irq, err;

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->dev = &pdev->dev;
	platform_set_drvdata(pdev, dev);

	dev->pdm_clk = devm_clk_get(&pdev->dev, "pdm_clk");
	if (IS_ERR(dev->pdm_clk)) {
		dev_err(&pdev->dev, "failed to get pdm_clk\n");
		return PTR_ERR(dev->pdm_clk);
	}

	err = clk_prepare_enable(dev->pdm_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable pdm_clk\n");
		return err;
	}
	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	dev->regmap = devm_regmap_init_mmio(&pdev->dev, regs,
					    &alif_pdm_regmap_config);
	if (IS_ERR(dev->regmap))
		return PTR_ERR(dev->regmap);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		err = irq;
		goto err_clk;
	}

	dev_set_name(&pdev->dev, "%s", "alifpcm");

	err = devm_request_threaded_irq(&pdev->dev, irq, alif_pcm_irq_handler,
					alif_pcm_irq_thread, IRQF_ONESHOT,
	dev_name(&pdev->dev), dev);
	if (err) {
		dev_err(&pdev->dev, "request_irq failed: %d\n", err);
		goto err_clk;
	}

	dev->channel = PDM_CHANNEL_MASK;	/* Enable all by default */
	dev->pdm_mode = PDM_DEFAULT_MODE;

	err = devm_snd_soc_register_component(&pdev->dev,
					      &alif_pcm_component,
	&alif_pcm_dai, 1);
	if (err)
		goto err_clk;

	err = device_create_file(&pdev->dev, &dev_attr_modefreq);
	if (err) {
		dev_err(&pdev->dev, "Failed to create modefreq sysfs\n");
		goto err_comp;
	}

	err = device_create_file(&pdev->dev, &dev_attr_channelsel);
	if (err) {
		dev_err(&pdev->dev, "Failed to create channelsel sysfs\n");
		goto err_mode;
	}

	return 0;

err_mode:
	device_remove_file(&pdev->dev, &dev_attr_modefreq);
err_comp:
err_clk:
	clk_disable_unprepare(dev->pdm_clk);
	return err;
}

static void alif_pcm_remove(struct platform_device *pdev)
{
	struct alif_pcm_dev *dev = platform_get_drvdata(pdev);

	clk_disable_unprepare(dev->pdm_clk);
}

static const struct of_device_id alif_pcm_of_match[] = {
	{ .compatible = "alif,alif-pcm" },
	{},
};
MODULE_DEVICE_TABLE(of, alif_pcm_of_match);

static struct platform_driver alif_pcm_driver = {
	.probe = alif_pcm_probe,
	.remove = alif_pcm_remove,
	.driver = {
		.name = "alif_pcm",
		.of_match_table = alif_pcm_of_match,
	},
};

module_platform_driver(alif_pcm_driver);

MODULE_AUTHOR("Aravind Krishnan <aravind.krishnan@alifsemi.com>");
MODULE_DESCRIPTION("Alif PDM Audio Driver");
MODULE_LICENSE("GPL");

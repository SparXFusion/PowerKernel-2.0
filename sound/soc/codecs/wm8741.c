/*
 * wm8741.c  --  WM8741 ALSA SoC Audio driver
 *
 * Copyright 2010 Wolfson Microelectronics plc
 *
 * Author: Ian Lartey <ian@opensource.wolfsonmicro.com>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "wm8741.h"

#define WM8741_NUM_SUPPLIES 2
static const char *wm8741_supply_names[WM8741_NUM_SUPPLIES] = {
	"AVDD",
	"DVDD",
};

#define WM8741_NUM_RATES 6

/* codec private data */
struct wm8741_priv {
	enum snd_soc_control_type control_type;
	struct regulator_bulk_data supplies[WM8741_NUM_SUPPLIES];
	unsigned int sysclk;
	struct snd_pcm_hw_constraint_list *sysclk_constraints;
};

static const u16 wm8741_reg_defaults[WM8741_REGISTER_COUNT] = {
	0x0000,     /* R0  - DACLLSB Attenuation */
	0x0000,     /* R1  - DACLMSB Attenuation */
	0x0000,     /* R2  - DACRLSB Attenuation */
	0x0000,     /* R3  - DACRMSB Attenuation */
	0x0000,     /* R4  - Volume Control */
	0x000A,     /* R5  - Format Control */
	0x0000,     /* R6  - Filter Control */
	0x0000,     /* R7  - Mode Control 1 */
	0x0002,     /* R8  - Mode Control 2 */
	0x0000,	    /* R9  - Reset */
	0x0002,     /* R32 - ADDITONAL_CONTROL_1 */
};


static int wm8741_reset(struct snd_soc_codec *codec)
{
	return snd_soc_write(codec, WM8741_RESET, 0);
}

static const DECLARE_TLV_DB_SCALE(dac_tlv_fine, -12700, 13, 0);
static const DECLARE_TLV_DB_SCALE(dac_tlv, -12700, 400, 0);

static const struct snd_kcontrol_new wm8741_snd_controls[] = {
SOC_DOUBLE_R_TLV("Fine Playback Volume", WM8741_DACLLSB_ATTENUATION,
		 WM8741_DACRLSB_ATTENUATION, 1, 255, 1, dac_tlv_fine),
SOC_DOUBLE_R_TLV("Playback Volume", WM8741_DACLMSB_ATTENUATION,
		 WM8741_DACRMSB_ATTENUATION, 0, 511, 1, dac_tlv),
};

static const struct snd_soc_dapm_widget wm8741_dapm_widgets[] = {
SND_SOC_DAPM_DAC("DACL", "Playback", SND_SOC_NOPM, 0, 0),
SND_SOC_DAPM_DAC("DACR", "Playback", SND_SOC_NOPM, 0, 0),
SND_SOC_DAPM_OUTPUT("VOUTLP"),
SND_SOC_DAPM_OUTPUT("VOUTLN"),
SND_SOC_DAPM_OUTPUT("VOUTRP"),
SND_SOC_DAPM_OUTPUT("VOUTRN"),
};

static const struct snd_soc_dapm_route intercon[] = {
	{ "VOUTLP", NULL, "DACL" },
	{ "VOUTLN", NULL, "DACL" },
	{ "VOUTRP", NULL, "DACR" },
	{ "VOUTRN", NULL, "DACR" },
};

static int wm8741_add_widgets(struct snd_soc_codec *codec)
{
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	snd_soc_dapm_new_controls(dapm, wm8741_dapm_widgets,
				  ARRAY_SIZE(wm8741_dapm_widgets));
	snd_soc_dapm_add_routes(dapm, intercon, ARRAY_SIZE(intercon));

	return 0;
}

static struct {
	int value;
	int ratio;
} lrclk_ratios[WM8741_NUM_RATES] = {
	{ 1, 128 },
	{ 2, 192 },
	{ 3, 256 },
	{ 4, 384 },
	{ 5, 512 },
	{ 6, 768 },
};

static unsigned int rates_11289[] = {
	44100, 88200,
};

static struct snd_pcm_hw_constraint_list constraints_11289 = {
	.count	= ARRAY_SIZE(rates_11289),
	.list	= rates_11289,
};

static unsigned int rates_12288[] = {
	32000, 48000, 96000,
};

static struct snd_pcm_hw_constraint_list constraints_12288 = {
	.count	= ARRAY_SIZE(rates_12288),
	.list	= rates_12288,
};

static unsigned int rates_16384[] = {
	32000,
};

static struct snd_pcm_hw_constraint_list constraints_16384 = {
	.count	= ARRAY_SIZE(rates_16384),
	.list	= rates_16384,
};

static unsigned int rates_16934[] = {
	44100, 88200,
};

static struct snd_pcm_hw_constraint_list constraints_16934 = {
	.count	= ARRAY_SIZE(rates_16934),
	.list	= rates_16934,
};

static unsigned int rates_18432[] = {
	48000, 96000,
};

static struct snd_pcm_hw_constraint_list constraints_18432 = {
	.count	= ARRAY_SIZE(rates_18432),
	.list	= rates_18432,
};

static unsigned int rates_22579[] = {
	44100, 88200, 176400
};

static struct snd_pcm_hw_constraint_list constraints_22579 = {
	.count	= ARRAY_SIZE(rates_22579),
	.list	= rates_22579,
};

static unsigned int rates_24576[] = {
	32000, 48000, 96000, 192000
};

static struct snd_pcm_hw_constraint_list constraints_24576 = {
	.count	= ARRAY_SIZE(rates_24576),
	.list	= rates_24576,
};

static unsigned int rates_36864[] = {
	48000, 96000, 192000
};

static struct snd_pcm_hw_constraint_list constraints_36864 = {
	.count	= ARRAY_SIZE(rates_36864),
	.list	= rates_36864,
};


static int wm8741_startup(struct snd_pcm_substream *substream,
			  struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct wm8741_priv *wm8741 = snd_soc_codec_get_drvdata(codec);

	/* The set of sample rates that can be supported depends on the
	 * MCLK supplied to the CODEC - enforce this.
	 */
	if (!wm8741->sysclk) {
		dev_err(codec->dev,
			"No MCLK configured, call set_sysclk() on init\n");
		return -EINVAL;
	}

	snd_pcm_hw_constraint_list(substream->runtime, 0,
				   SNDRV_PCM_HW_PARAM_RATE,
				   wm8741->sysclk_constraints);

	return 0;
}

static int wm8741_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	struct wm8741_priv *wm8741 = snd_soc_codec_get_drvdata(codec);
	u16 iface = snd_soc_read(codec, WM8741_FORMAT_CONTROL) & 0x1FC;
	int i;

	/* Find a supported LRCLK ratio */
	for (i = 0; i < ARRAY_SIZE(lrclk_ratios); i++) {
		if (wm8741->sysclk / params_rate(params) ==
		    lrclk_ratios[i].ratio)
			break;
	}

	/* Should never happen, should be handled by constraints */
	if (i == ARRAY_SIZE(lrclk_ratios)) {
		dev_err(codec->dev, "MCLK/fs ratio %d unsupported\n",
			wm8741->sysclk / params_rate(params));
		return -EINVAL;
	}

	/* bit size */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		iface |= 0x0001;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		iface |= 0x0002;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		iface |= 0x0003;
		break;
	default:
		dev_dbg(codec->dev, "wm8741_hw_params:    Unsupported bit size param = %d",
			params_format(params));
		return -EINVAL;
	}

	dev_dbg(codec->dev, "wm8741_hw_params:    bit size param = %d",
		params_format(params));

	snd_soc_write(codec, WM8741_FORMAT_CONTROL, iface);
	return 0;
}

static int wm8741_set_dai_sysclk(struct snd_soc_dai *codec_dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct wm8741_priv *wm8741 = snd_soc_codec_get_drvdata(codec);

	dev_dbg(codec->dev, "wm8741_set_dai_sysclk info: freq=%dHz\n", freq);

	switch (freq) {
	case 11289600:
		wm8741->sysclk_constraints = &constraints_11289;
		wm8741->sysclk = freq;
		return 0;

	case 12288000:
		wm8741->sysclk_constraints = &constraints_12288;
		wm8741->sysclk = freq;
		return 0;

	case 16384000:
		wm8741->sysclk_constraints = &constraints_16384;
		wm8741->sysclk = freq;
		return 0;

	case 16934400:
		wm8741->sysclk_constraints = &constraints_16934;
		wm8741->sysclk = freq;
		return 0;

	case 18432000:
		wm8741->sysclk_constraints = &constraints_18432;
		wm8741->sysclk = freq;
		return 0;

	case 22579200:
	case 33868800:
		wm8741->sysclk_constraints = &constraints_22579;
		wm8741->sysclk = freq;
		return 0;

	case 24576000:
		wm8741->sysclk_constraints = &constraints_24576;
		wm8741->sysclk = freq;
		return 0;

	case 36864000:
		wm8741->sysclk_constraints = &constraints_36864;
		wm8741->sysclk = freq;
		return 0;
	}
	return -EINVAL;
}

static int wm8741_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 iface = snd_soc_read(codec, WM8741_FORMAT_CONTROL) & 0x1C3;

	/* check master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		return -EINVAL;
	}

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		iface |= 0x0008;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		iface |= 0x0004;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		iface |= 0x0003;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		iface |= 0x0013;
		break;
	default:
		return -EINVAL;
	}

	/* clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_IF:
		iface |= 0x0010;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		iface |= 0x0020;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		iface |= 0x0030;
		break;
	default:
		return -EINVAL;
	}


	dev_dbg(codec->dev, "wm8741_set_dai_fmt:    Format=%x, Clock Inv=%x\n",
				fmt & SND_SOC_DAIFMT_FORMAT_MASK,
				((fmt & SND_SOC_DAIFMT_INV_MASK)));

	snd_soc_write(codec, WM8741_FORMAT_CONTROL, iface);
	return 0;
}

#define WM8741_RATES (SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 | \
			SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 | \
			SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 | \
			SNDRV_PCM_RATE_192000)

#define WM8741_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_ops wm8741_dai_ops = {
	.startup	= wm8741_startup,
	.hw_params	= wm8741_hw_params,
	.set_sysclk	= wm8741_set_dai_sysclk,
	.set_fmt	= wm8741_set_dai_fmt,
};

static struct snd_soc_dai_driver wm8741_dai = {
	.name = "wm8741",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,  /* Mono modes not yet supported */
		.channels_max = 2,
		.rates = WM8741_RATES,
		.formats = WM8741_FORMATS,
	},
	.ops = &wm8741_dai_ops,
};

#ifdef CONFIG_PM
static int wm8741_resume(struct snd_soc_codec *codec)
{
	u16 *cache = codec->reg_cache;
	int i;

	/* RESTORE REG Cache */
	for (i = 0; i < WM8741_REGISTER_COUNT; i++) {
		if (cache[i] == wm8741_reg_defaults[i] || WM8741_RESET == i)
			continue;
		snd_soc_write(codec, i, cache[i]);
	}
	return 0;
}
#else
#define wm8741_suspend NULL
#define wm8741_resume NULL
#endif

static int wm8741_probe(struct snd_soc_codec *codec)
{
	struct wm8741_priv *wm8741 = snd_soc_codec_get_drvdata(codec);
	int ret = 0;

	ret = snd_soc_codec_set_cache_io(codec, 7, 9, wm8741->control_type);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to set cache I/O: %d\n", ret);
		return ret;
	}

	ret = wm8741_reset(codec);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to issue reset\n");
		return ret;
	}

	/* Change some default settings - latch VU */
	snd_soc_update_bits(codec, WM8741_DACLLSB_ATTENUATION,
			    WM8741_UPDATELL, WM8741_UPDATELL);
	snd_soc_update_bits(codec, WM8741_DACLMSB_ATTENUATION,
			    WM8741_UPDATELM, WM8741_UPDATELM);
	snd_soc_update_bits(codec, WM8741_DACRLSB_ATTENUATION,
			    WM8741_UPDATERL, WM8741_UPDATERL);
	snd_soc_update_bits(codec, WM8741_DACRLSB_ATTENUATION,
			    WM8741_UPDATERM, WM8741_UPDATERM);

	snd_soc_add_controls(codec, wm8741_snd_controls,
			     ARRAY_SIZE(wm8741_snd_controls));
	wm8741_add_widgets(codec);

	dev_dbg(codec->dev, "Successful registration\n");
	return ret;
}

static struct snd_soc_codec_driver soc_codec_dev_wm8741 = {
	.probe =	wm8741_probe,
	.resume =	wm8741_resume,
	.reg_cache_size = ARRAY_SIZE(wm8741_reg_defaults),
	.reg_word_size = sizeof(u16),
	.reg_cache_default = wm8741_reg_defaults,
};

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
static int wm8741_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct wm8741_priv *wm8741;
	int ret, i;

	wm8741 = kzalloc(sizeof(struct wm8741_priv), GFP_KERNEL);
	if (wm8741 == NULL)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(wm8741->supplies); i++)
		wm8741->supplies[i].supply = wm8741_supply_names[i];

	ret = regulator_bulk_get(&i2c->dev, ARRAY_SIZE(wm8741->supplies),
				 wm8741->supplies);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to request supplies: %d\n", ret);
		goto err;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(wm8741->supplies),
				    wm8741->supplies);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to enable supplies: %d\n", ret);
		goto err_get;
	}

	i2c_set_clientdata(i2c, wm8741);
	wm8741->control_type = SND_SOC_I2C;

	ret =  snd_soc_register_codec(&i2c->dev,
			&soc_codec_dev_wm8741, &wm8741_dai, 1);
	if (ret < 0)
		goto err_enable;
	return ret;

err_enable:
	regulator_bulk_disable(ARRAY_SIZE(wm8741->supplies), wm8741->supplies);

err_get:
	regulator_bulk_free(ARRAY_SIZE(wm8741->supplies), wm8741->supplies);
err:
	kfree(wm8741);
	return ret;
}

static int wm8741_i2c_remove(struct i2c_client *client)
{
	struct wm8741_priv *wm8741 = i2c_get_clientdata(client);

	snd_soc_unregister_codec(&client->dev);
	regulator_bulk_free(ARRAY_SIZE(wm8741->supplies), wm8741->supplies);
	kfree(i2c_get_clientdata(client));
	return 0;
}

static const struct i2c_device_id wm8741_i2c_id[] = {
	{ "wm8741", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8741_i2c_id);

static struct i2c_driver wm8741_i2c_driver = {
	.driver = {
		.name = "wm8741-codec",
		.owner = THIS_MODULE,
	},
	.probe =    wm8741_i2c_probe,
	.remove =   wm8741_i2c_remove,
	.id_table = wm8741_i2c_id,
};
#endif

static int __init wm8741_modinit(void)
{
	int ret = 0;

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	ret = i2c_add_driver(&wm8741_i2c_driver);
	if (ret != 0)
		pr_err("Failed to register WM8741 I2C driver: %d\n", ret);
#endif

	return ret;
}
module_init(wm8741_modinit);

static void __exit wm8741_exit(void)
{
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	i2c_del_driver(&wm8741_i2c_driver);
#endif
}
module_exit(wm8741_exit);

MODULE_DESCRIPTION("ASoC WM8741 driver");
MODULE_AUTHOR("Ian Lartey <ian@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");

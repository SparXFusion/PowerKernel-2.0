/*
 * wm8903.c  --  WM8903 ALSA SoC Audio driver
 *
 * Copyright 2008 Wolfson Microelectronics
 * Copyright 2011 NVIDIA, Inc.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * TODO:
 *  - TDM mode configuration.
 *  - Digital microphone support.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/wm8903.h>
#include <trace/events/asoc.h>

#include "wm8903.h"

/* Register defaults at reset */
static u16 wm8903_reg_defaults[] = {
	0x8903,     /* R0   - SW Reset and ID */
	0x0000,     /* R1   - Revision Number */
	0x0000,     /* R2 */
	0x0000,     /* R3 */
	0x0018,     /* R4   - Bias Control 0 */
	0x0000,     /* R5   - VMID Control 0 */
	0x0000,     /* R6   - Mic Bias Control 0 */
	0x0000,     /* R7 */
	0x0001,     /* R8   - Analogue DAC 0 */
	0x0000,     /* R9 */
	0x0001,     /* R10  - Analogue ADC 0 */
	0x0000,     /* R11 */
	0x0000,     /* R12  - Power Management 0 */
	0x0000,     /* R13  - Power Management 1 */
	0x0000,     /* R14  - Power Management 2 */
	0x0000,     /* R15  - Power Management 3 */
	0x0000,     /* R16  - Power Management 4 */
	0x0000,     /* R17  - Power Management 5 */
	0x0000,     /* R18  - Power Management 6 */
	0x0000,     /* R19 */
	0x0400,     /* R20  - Clock Rates 0 */
	0x0D07,     /* R21  - Clock Rates 1 */
	0x0000,     /* R22  - Clock Rates 2 */
	0x0000,     /* R23 */
	0x0050,     /* R24  - Audio Interface 0 */
	0x0242,     /* R25  - Audio Interface 1 */
	0x0008,     /* R26  - Audio Interface 2 */
	0x0022,     /* R27  - Audio Interface 3 */
	0x0000,     /* R28 */
	0x0000,     /* R29 */
	0x00C0,     /* R30  - DAC Digital Volume Left */
	0x00C0,     /* R31  - DAC Digital Volume Right */
	0x0000,     /* R32  - DAC Digital 0 */
	0x0000,     /* R33  - DAC Digital 1 */
	0x0000,     /* R34 */
	0x0000,     /* R35 */
	0x00C0,     /* R36  - ADC Digital Volume Left */
	0x00C0,     /* R37  - ADC Digital Volume Right */
	0x0000,     /* R38  - ADC Digital 0 */
	0x0073,     /* R39  - Digital Microphone 0 */
	0x09BF,     /* R40  - DRC 0 */
	0x3241,     /* R41  - DRC 1 */
	0x0020,     /* R42  - DRC 2 */
	0x0000,     /* R43  - DRC 3 */
	0x0085,     /* R44  - Analogue Left Input 0 */
	0x0085,     /* R45  - Analogue Right Input 0 */
	0x0044,     /* R46  - Analogue Left Input 1 */
	0x0044,     /* R47  - Analogue Right Input 1 */
	0x0000,     /* R48 */
	0x0000,     /* R49 */
	0x0008,     /* R50  - Analogue Left Mix 0 */
	0x0004,     /* R51  - Analogue Right Mix 0 */
	0x0000,     /* R52  - Analogue Spk Mix Left 0 */
	0x0000,     /* R53  - Analogue Spk Mix Left 1 */
	0x0000,     /* R54  - Analogue Spk Mix Right 0 */
	0x0000,     /* R55  - Analogue Spk Mix Right 1 */
	0x0000,     /* R56 */
	0x002D,     /* R57  - Analogue OUT1 Left */
	0x002D,     /* R58  - Analogue OUT1 Right */
	0x0039,     /* R59  - Analogue OUT2 Left */
	0x0039,     /* R60  - Analogue OUT2 Right */
	0x0100,     /* R61 */
	0x0139,     /* R62  - Analogue OUT3 Left */
	0x0139,     /* R63  - Analogue OUT3 Right */
	0x0000,     /* R64 */
	0x0000,     /* R65  - Analogue SPK Output Control 0 */
	0x0000,     /* R66 */
	0x0010,     /* R67  - DC Servo 0 */
	0x0100,     /* R68 */
	0x00A4,     /* R69  - DC Servo 2 */
	0x0807,     /* R70 */
	0x0000,     /* R71 */
	0x0000,     /* R72 */
	0x0000,     /* R73 */
	0x0000,     /* R74 */
	0x0000,     /* R75 */
	0x0000,     /* R76 */
	0x0000,     /* R77 */
	0x0000,     /* R78 */
	0x000E,     /* R79 */
	0x0000,     /* R80 */
	0x0000,     /* R81 */
	0x0000,     /* R82 */
	0x0000,     /* R83 */
	0x0000,     /* R84 */
	0x0000,     /* R85 */
	0x0000,     /* R86 */
	0x0006,     /* R87 */
	0x0000,     /* R88 */
	0x0000,     /* R89 */
	0x0000,     /* R90  - Analogue HP 0 */
	0x0060,     /* R91 */
	0x0000,     /* R92 */
	0x0000,     /* R93 */
	0x0000,     /* R94  - Analogue Lineout 0 */
	0x0060,     /* R95 */
	0x0000,     /* R96 */
	0x0000,     /* R97 */
	0x0000,     /* R98  - Charge Pump 0 */
	0x1F25,     /* R99 */
	0x2B19,     /* R100 */
	0x01C0,     /* R101 */
	0x01EF,     /* R102 */
	0x2B00,     /* R103 */
	0x0000,     /* R104 - Class W 0 */
	0x01C0,     /* R105 */
	0x1C10,     /* R106 */
	0x0000,     /* R107 */
	0x0000,     /* R108 - Write Sequencer 0 */
	0x0000,     /* R109 - Write Sequencer 1 */
	0x0000,     /* R110 - Write Sequencer 2 */
	0x0000,     /* R111 - Write Sequencer 3 */
	0x0000,     /* R112 - Write Sequencer 4 */
	0x0000,     /* R113 */
	0x0000,     /* R114 - Control Interface */
	0x0000,     /* R115 */
	0x00A8,     /* R116 - GPIO Control 1 */
	0x00A8,     /* R117 - GPIO Control 2 */
	0x00A8,     /* R118 - GPIO Control 3 */
	0x0220,     /* R119 - GPIO Control 4 */
	0x01A0,     /* R120 - GPIO Control 5 */
	0x0000,     /* R121 - Interrupt Status 1 */
	0xFFFF,     /* R122 - Interrupt Status 1 Mask */
	0x0000,     /* R123 - Interrupt Polarity 1 */
	0x0000,     /* R124 */
	0x0003,     /* R125 */
	0x0000,     /* R126 - Interrupt Control */
	0x0000,     /* R127 */
	0x0005,     /* R128 */
	0x0000,     /* R129 - Control Interface Test 1 */
	0x0000,     /* R130 */
	0x0000,     /* R131 */
	0x0000,     /* R132 */
	0x0000,     /* R133 */
	0x0000,     /* R134 */
	0x03FF,     /* R135 */
	0x0007,     /* R136 */
	0x0040,     /* R137 */
	0x0000,     /* R138 */
	0x0000,     /* R139 */
	0x0000,     /* R140 */
	0x0000,     /* R141 */
	0x0000,     /* R142 */
	0x0000,     /* R143 */
	0x0000,     /* R144 */
	0x0000,     /* R145 */
	0x0000,     /* R146 */
	0x0000,     /* R147 */
	0x4000,     /* R148 */
	0x6810,     /* R149 - Charge Pump Test 1 */
	0x0004,     /* R150 */
	0x0000,     /* R151 */
	0x0000,     /* R152 */
	0x0000,     /* R153 */
	0x0000,     /* R154 */
	0x0000,     /* R155 */
	0x0000,     /* R156 */
	0x0000,     /* R157 */
	0x0000,     /* R158 */
	0x0000,     /* R159 */
	0x0000,     /* R160 */
	0x0000,     /* R161 */
	0x0000,     /* R162 */
	0x0000,     /* R163 */
	0x0028,     /* R164 - Clock Rate Test 4 */
	0x0004,     /* R165 */
	0x0000,     /* R166 */
	0x0060,     /* R167 */
	0x0000,     /* R168 */
	0x0000,     /* R169 */
	0x0000,     /* R170 */
	0x0000,     /* R171 */
	0x0000,     /* R172 - Analogue Output Bias 0 */
};

struct wm8903_priv {
	struct snd_soc_codec *codec;

	int sysclk;
	int irq;

	int fs;
	int deemph;

	int dcs_pending;
	int dcs_cache[4];

	/* Reference count */
	int class_w_users;

	struct snd_soc_jack *mic_jack;
	int mic_det;
	int mic_short;
	int mic_last_report;
	int mic_delay;

#ifdef CONFIG_GPIOLIB
	struct gpio_chip gpio_chip;
#endif
};

static int wm8903_volatile_register(struct snd_soc_codec *codec, unsigned int reg)
{
	switch (reg) {
	case WM8903_SW_RESET_AND_ID:
	case WM8903_REVISION_NUMBER:
	case WM8903_INTERRUPT_STATUS_1:
	case WM8903_WRITE_SEQUENCER_4:
	case WM8903_DC_SERVO_READBACK_1:
	case WM8903_DC_SERVO_READBACK_2:
	case WM8903_DC_SERVO_READBACK_3:
	case WM8903_DC_SERVO_READBACK_4:
		return 1;

	default:
		return 0;
	}
}

static void wm8903_reset(struct snd_soc_codec *codec)
{
	snd_soc_write(codec, WM8903_SW_RESET_AND_ID, 0);
	memcpy(codec->reg_cache, wm8903_reg_defaults,
	       sizeof(wm8903_reg_defaults));
}

static int wm8903_cp_event(struct snd_soc_dapm_widget *w,
			   struct snd_kcontrol *kcontrol, int event)
{
	WARN_ON(event != SND_SOC_DAPM_POST_PMU);
	mdelay(4);

	return 0;
}

static int wm8903_dcs_event(struct snd_soc_dapm_widget *w,
			    struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	struct wm8903_priv *wm8903 = snd_soc_codec_get_drvdata(codec);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		wm8903->dcs_pending |= 1 << w->shift;
		break;
	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(codec, WM8903_DC_SERVO_0,
				    1 << w->shift, 0);
		break;
	}

	return 0;
}

#define WM8903_DCS_MODE_WRITE_STOP 0
#define WM8903_DCS_MODE_START_STOP 2

static void wm8903_seq_notifier(struct snd_soc_dapm_context *dapm,
				enum snd_soc_dapm_type event, int subseq)
{
	struct snd_soc_codec *codec = container_of(dapm,
						   struct snd_soc_codec, dapm);
	struct wm8903_priv *wm8903 = snd_soc_codec_get_drvdata(codec);
	int dcs_mode = WM8903_DCS_MODE_WRITE_STOP;
	int i, val;

	/* Complete any pending DC servo starts */
	if (wm8903->dcs_pending) {
		dev_dbg(codec->dev, "Starting DC servo for %x\n",
			wm8903->dcs_pending);

		/* If we've no cached values then we need to do startup */
		for (i = 0; i < ARRAY_SIZE(wm8903->dcs_cache); i++) {
			if (!(wm8903->dcs_pending & (1 << i)))
				continue;

			if (wm8903->dcs_cache[i]) {
				dev_dbg(codec->dev,
					"Restore DC servo %d value %x\n",
					3 - i, wm8903->dcs_cache[i]);

				snd_soc_write(codec, WM8903_DC_SERVO_4 + i,
					      wm8903->dcs_cache[i] & 0xff);
			} else {
				dev_dbg(codec->dev,
					"Calibrate DC servo %d\n", 3 - i);
				dcs_mode = WM8903_DCS_MODE_START_STOP;
			}
		}

		/* Don't trust the cache for analogue */
		if (wm8903->class_w_users)
			dcs_mode = WM8903_DCS_MODE_START_STOP;

		snd_soc_update_bits(codec, WM8903_DC_SERVO_2,
				    WM8903_DCS_MODE_MASK, dcs_mode);

		snd_soc_update_bits(codec, WM8903_DC_SERVO_0,
				    WM8903_DCS_ENA_MASK, wm8903->dcs_pending);

		switch (dcs_mode) {
		case WM8903_DCS_MODE_WRITE_STOP:
			break;

		case WM8903_DCS_MODE_START_STOP:
			msleep(270);

			/* Cache the measured offsets for digital */
			if (wm8903->class_w_users)
				break;

			for (i = 0; i < ARRAY_SIZE(wm8903->dcs_cache); i++) {
				if (!(wm8903->dcs_pending & (1 << i)))
					continue;

				val = snd_soc_read(codec,
						   WM8903_DC_SERVO_READBACK_1 + i);
				dev_dbg(codec->dev, "DC servo %d: %x\n",
					3 - i, val);
				wm8903->dcs_cache[i] = val;
			}
			break;

		default:
			pr_warn("DCS mode %d delay not set\n", dcs_mode);
			break;
		}

		wm8903->dcs_pending = 0;
	}
}

/*
 * When used with DAC outputs only the WM8903 charge pump supports
 * operation in class W mode, providing very low power consumption
 * when used with digital sources.  Enable and disable this mode
 * automatically depending on the mixer configuration.
 *
 * All the relevant controls are simple switches.
 */
static int wm8903_class_w_put(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget_list *wlist = snd_kcontrol_chip(kcontrol);
	struct snd_soc_dapm_widget *widget = wlist->widgets[0];
	struct snd_soc_codec *codec = widget->codec;
	struct wm8903_priv *wm8903 = snd_soc_codec_get_drvdata(codec);
	u16 reg;
	int ret;

	reg = snd_soc_read(codec, WM8903_CLASS_W_0);

	/* Turn it off if we're about to enable bypass */
	if (ucontrol->value.integer.value[0]) {
		if (wm8903->class_w_users == 0) {
			dev_dbg(codec->dev, "Disabling Class W\n");
			snd_soc_write(codec, WM8903_CLASS_W_0, reg &
				     ~(WM8903_CP_DYN_FREQ | WM8903_CP_DYN_V));
		}
		wm8903->class_w_users++;
	}

	/* Implement the change */
	ret = snd_soc_dapm_put_volsw(kcontrol, ucontrol);

	/* If we've just disabled the last bypass path turn Class W on */
	if (!ucontrol->value.integer.value[0]) {
		if (wm8903->class_w_users == 1) {
			dev_dbg(codec->dev, "Enabling Class W\n");
			snd_soc_write(codec, WM8903_CLASS_W_0, reg |
				     WM8903_CP_DYN_FREQ | WM8903_CP_DYN_V);
		}
		wm8903->class_w_users--;
	}

	dev_dbg(codec->dev, "Bypass use count now %d\n",
		wm8903->class_w_users);

	return ret;
}

#define SOC_DAPM_SINGLE_W(xname, reg, shift, max, invert) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = snd_soc_info_volsw, \
	.get = snd_soc_dapm_get_volsw, .put = wm8903_class_w_put, \
	.private_value =  SOC_SINGLE_VALUE(reg, shift, max, invert) }


static int wm8903_deemph[] = { 0, 32000, 44100, 48000 };

static int wm8903_set_deemph(struct snd_soc_codec *codec)
{
	struct wm8903_priv *wm8903 = snd_soc_codec_get_drvdata(codec);
	int val, i, best;

	/* If we're using deemphasis select the nearest available sample
	 * rate.
	 */
	if (wm8903->deemph) {
		best = 1;
		for (i = 2; i < ARRAY_SIZE(wm8903_deemph); i++) {
			if (abs(wm8903_deemph[i] - wm8903->fs) <
			    abs(wm8903_deemph[best] - wm8903->fs))
				best = i;
		}

		val = best << WM8903_DEEMPH_SHIFT;
	} else {
		best = 0;
		val = 0;
	}

	dev_dbg(codec->dev, "Set deemphasis %d (%dHz)\n",
		best, wm8903_deemph[best]);

	return snd_soc_update_bits(codec, WM8903_DAC_DIGITAL_1,
				   WM8903_DEEMPH_MASK, val);
}

static int wm8903_get_deemph(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wm8903_priv *wm8903 = snd_soc_codec_get_drvdata(codec);

	ucontrol->value.integer.value[0] = wm8903->deemph;

	return 0;
}

static int wm8903_put_deemph(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wm8903_priv *wm8903 = snd_soc_codec_get_drvdata(codec);
	int deemph = ucontrol->value.integer.value[0];
	int ret = 0;

	if (deemph > 1)
		return -EINVAL;

	mutex_lock(&codec->mutex);
	if (wm8903->deemph != deemph) {
		wm8903->deemph = deemph;

		wm8903_set_deemph(codec);

		ret = 1;
	}
	mutex_unlock(&codec->mutex);

	return ret;
}

/* ALSA can only do steps of .01dB */
static const DECLARE_TLV_DB_SCALE(digital_tlv, -7200, 75, 1);

static const DECLARE_TLV_DB_SCALE(digital_sidetone_tlv, -3600, 300, 0);
static const DECLARE_TLV_DB_SCALE(out_tlv, -5700, 100, 0);

static const DECLARE_TLV_DB_SCALE(drc_tlv_thresh, 0, 75, 0);
static const DECLARE_TLV_DB_SCALE(drc_tlv_amp, -2250, 75, 0);
static const DECLARE_TLV_DB_SCALE(drc_tlv_min, 0, 600, 0);
static const DECLARE_TLV_DB_SCALE(drc_tlv_max, 1200, 600, 0);
static const DECLARE_TLV_DB_SCALE(drc_tlv_startup, -300, 50, 0);

static const char *hpf_mode_text[] = {
	"Hi-fi", "Voice 1", "Voice 2", "Voice 3"
};

static const struct soc_enum hpf_mode =
	SOC_ENUM_SINGLE(WM8903_ADC_DIGITAL_0, 5, 4, hpf_mode_text);

static const char *osr_text[] = {
	"Low power", "High performance"
};

static const struct soc_enum adc_osr =
	SOC_ENUM_SINGLE(WM8903_ANALOGUE_ADC_0, 0, 2, osr_text);

static const struct soc_enum dac_osr =
	SOC_ENUM_SINGLE(WM8903_DAC_DIGITAL_1, 0, 2, osr_text);

static const char *drc_slope_text[] = {
	"1", "1/2", "1/4", "1/8", "1/16", "0"
};

static const struct soc_enum drc_slope_r0 =
	SOC_ENUM_SINGLE(WM8903_DRC_2, 3, 6, drc_slope_text);

static const struct soc_enum drc_slope_r1 =
	SOC_ENUM_SINGLE(WM8903_DRC_2, 0, 6, drc_slope_text);

static const char *drc_attack_text[] = {
	"instantaneous",
	"363us", "762us", "1.45ms", "2.9ms", "5.8ms", "11.6ms", "23.2ms",
	"46.4ms", "92.8ms", "185.6ms"
};

static const struct soc_enum drc_attack =
	SOC_ENUM_SINGLE(WM8903_DRC_1, 12, 11, drc_attack_text);

static const char *drc_decay_text[] = {
	"186ms", "372ms", "743ms", "1.49s", "2.97s", "5.94s", "11.89s",
	"23.87s", "47.56s"
};

static const struct soc_enum drc_decay =
	SOC_ENUM_SINGLE(WM8903_DRC_1, 8, 9, drc_decay_text);

static const char *drc_ff_delay_text[] = {
	"5 samples", "9 samples"
};

static const struct soc_enum drc_ff_delay =
	SOC_ENUM_SINGLE(WM8903_DRC_0, 5, 2, drc_ff_delay_text);

static const char *drc_qr_decay_text[] = {
	"0.725ms", "1.45ms", "5.8ms"
};

static const struct soc_enum drc_qr_decay =
	SOC_ENUM_SINGLE(WM8903_DRC_1, 4, 3, drc_qr_decay_text);

static const char *drc_smoothing_text[] = {
	"Low", "Medium", "High"
};

static const struct soc_enum drc_smoothing =
	SOC_ENUM_SINGLE(WM8903_DRC_0, 11, 3, drc_smoothing_text);

static const char *soft_mute_text[] = {
	"Fast (fs/2)", "Slow (fs/32)"
};

static const struct soc_enum soft_mute =
	SOC_ENUM_SINGLE(WM8903_DAC_DIGITAL_1, 10, 2, soft_mute_text);

static const char *mute_mode_text[] = {
	"Hard", "Soft"
};

static const struct soc_enum mute_mode =
	SOC_ENUM_SINGLE(WM8903_DAC_DIGITAL_1, 9, 2, mute_mode_text);

static const char *companding_text[] = {
	"ulaw", "alaw"
};

static const struct soc_enum dac_companding =
	SOC_ENUM_SINGLE(WM8903_AUDIO_INTERFACE_0, 0, 2, companding_text);

static const struct soc_enum adc_companding =
	SOC_ENUM_SINGLE(WM8903_AUDIO_INTERFACE_0, 2, 2, companding_text);

static const char *input_mode_text[] = {
	"Single-Ended", "Differential Line", "Differential Mic"
};

static const struct soc_enum linput_mode_enum =
	SOC_ENUM_SINGLE(WM8903_ANALOGUE_LEFT_INPUT_1, 0, 3, input_mode_text);

static const struct soc_enum rinput_mode_enum =
	SOC_ENUM_SINGLE(WM8903_ANALOGUE_RIGHT_INPUT_1, 0, 3, input_mode_text);

static const char *linput_mux_text[] = {
	"IN1L", "IN2L", "IN3L"
};

static const struct soc_enum linput_enum =
	SOC_ENUM_SINGLE(WM8903_ANALOGUE_LEFT_INPUT_1, 2, 3, linput_mux_text);

static const struct soc_enum linput_inv_enum =
	SOC_ENUM_SINGLE(WM8903_ANALOGUE_LEFT_INPUT_1, 4, 3, linput_mux_text);

static const char *rinput_mux_text[] = {
	"IN1R", "IN2R", "IN3R"
};

static const struct soc_enum rinput_enum =
	SOC_ENUM_SINGLE(WM8903_ANALOGUE_RIGHT_INPUT_1, 2, 3, rinput_mux_text);

static const struct soc_enum rinput_inv_enum =
	SOC_ENUM_SINGLE(WM8903_ANALOGUE_RIGHT_INPUT_1, 4, 3, rinput_mux_text);


static const char *sidetone_text[] = {
	"None", "Left", "Right"
};

static const struct soc_enum lsidetone_enum =
	SOC_ENUM_SINGLE(WM8903_DAC_DIGITAL_0, 2, 3, sidetone_text);

static const struct soc_enum rsidetone_enum =
	SOC_ENUM_SINGLE(WM8903_DAC_DIGITAL_0, 0, 3, sidetone_text);

static const char *adcinput_text[] = {
	"ADC", "DMIC"
};

static const struct soc_enum adcinput_enum =
	SOC_ENUM_SINGLE(WM8903_CLOCK_RATE_TEST_4, 9, 2, adcinput_text);

static const char *aif_text[] = {
	"Left", "Right"
};

static const struct soc_enum lcapture_enum =
	SOC_ENUM_SINGLE(WM8903_AUDIO_INTERFACE_0, 7, 2, aif_text);

static const struct soc_enum rcapture_enum =
	SOC_ENUM_SINGLE(WM8903_AUDIO_INTERFACE_0, 6, 2, aif_text);

static const struct soc_enum lplay_enum =
	SOC_ENUM_SINGLE(WM8903_AUDIO_INTERFACE_0, 5, 2, aif_text);

static const struct soc_enum rplay_enum =
	SOC_ENUM_SINGLE(WM8903_AUDIO_INTERFACE_0, 4, 2, aif_text);

static const struct snd_kcontrol_new wm8903_snd_controls[] = {

/* Input PGAs - No TLV since the scale depends on PGA mode */
SOC_SINGLE("Left Input PGA Switch", WM8903_ANALOGUE_LEFT_INPUT_0,
	   7, 1, 1),
SOC_SINGLE("Left Input PGA Volume", WM8903_ANALOGUE_LEFT_INPUT_0,
	   0, 31, 0),
SOC_SINGLE("Left Input PGA Common Mode Switch", WM8903_ANALOGUE_LEFT_INPUT_1,
	   6, 1, 0),

SOC_SINGLE("Right Input PGA Switch", WM8903_ANALOGUE_RIGHT_INPUT_0,
	   7, 1, 1),
SOC_SINGLE("Right Input PGA Volume", WM8903_ANALOGUE_RIGHT_INPUT_0,
	   0, 31, 0),
SOC_SINGLE("Right Input PGA Common Mode Switch", WM8903_ANALOGUE_RIGHT_INPUT_1,
	   6, 1, 0),

/* ADCs */
SOC_ENUM("ADC OSR", adc_osr),
SOC_SINGLE("HPF Switch", WM8903_ADC_DIGITAL_0, 4, 1, 0),
SOC_ENUM("HPF Mode", hpf_mode),
SOC_SINGLE("DRC Switch", WM8903_DRC_0, 15, 1, 0),
SOC_ENUM("DRC Compressor Slope R0", drc_slope_r0),
SOC_ENUM("DRC Compressor Slope R1", drc_slope_r1),
SOC_SINGLE_TLV("DRC Compressor Threshold Volume", WM8903_DRC_3, 5, 124, 1,
	       drc_tlv_thresh),
SOC_SINGLE_TLV("DRC Volume", WM8903_DRC_3, 0, 30, 1, drc_tlv_amp),
SOC_SINGLE_TLV("DRC Minimum Gain Volume", WM8903_DRC_1, 2, 3, 1, drc_tlv_min),
SOC_SINGLE_TLV("DRC Maximum Gain Volume", WM8903_DRC_1, 0, 3, 0, drc_tlv_max),
SOC_ENUM("DRC Attack Rate", drc_attack),
SOC_ENUM("DRC Decay Rate", drc_decay),
SOC_ENUM("DRC FF Delay", drc_ff_delay),
SOC_SINGLE("DRC Anticlip Switch", WM8903_DRC_0, 1, 1, 0),
SOC_SINGLE("DRC QR Switch", WM8903_DRC_0, 2, 1, 0),
SOC_SINGLE_TLV("DRC QR Threshold Volume", WM8903_DRC_0, 6, 3, 0, drc_tlv_max),
SOC_ENUM("DRC QR Decay Rate", drc_qr_decay),
SOC_SINGLE("DRC Smoothing Switch", WM8903_DRC_0, 3, 1, 0),
SOC_SINGLE("DRC Smoothing Hysteresis Switch", WM8903_DRC_0, 0, 1, 0),
SOC_ENUM("DRC Smoothing Threshold", drc_smoothing),
SOC_SINGLE_TLV("DRC Startup Volume", WM8903_DRC_0, 6, 18, 0, drc_tlv_startup),

SOC_DOUBLE_R_TLV("Digital Capture Volume", WM8903_ADC_DIGITAL_VOLUME_LEFT,
		 WM8903_ADC_DIGITAL_VOLUME_RIGHT, 1, 120, 0, digital_tlv),
SOC_ENUM("ADC Companding Mode", adc_companding),
SOC_SINGLE("ADC Companding Switch", WM8903_AUDIO_INTERFACE_0, 3, 1, 0),

SOC_DOUBLE_TLV("Digital Sidetone Volume", WM8903_DAC_DIGITAL_0, 4, 8,
	       12, 0, digital_sidetone_tlv),

/* DAC */
SOC_ENUM("DAC OSR", dac_osr),
SOC_DOUBLE_R_TLV("Digital Playback Volume", WM8903_DAC_DIGITAL_VOLUME_LEFT,
		 WM8903_DAC_DIGITAL_VOLUME_RIGHT, 1, 120, 0, digital_tlv),
SOC_ENUM("DAC Soft Mute Rate", soft_mute),
SOC_ENUM("DAC Mute Mode", mute_mode),
SOC_SINGLE("DAC Mono Switch", WM8903_DAC_DIGITAL_1, 12, 1, 0),
SOC_ENUM("DAC Companding Mode", dac_companding),
SOC_SINGLE("DAC Companding Switch", WM8903_AUDIO_INTERFACE_0, 1, 1, 0),
SOC_SINGLE_BOOL_EXT("Playback Deemphasis Switch", 0,
		    wm8903_get_deemph, wm8903_put_deemph),

/* Headphones */
SOC_DOUBLE_R("Headphone Switch",
	     WM8903_ANALOGUE_OUT1_LEFT, WM8903_ANALOGUE_OUT1_RIGHT,
	     8, 1, 1),
SOC_DOUBLE_R("Headphone ZC Switch",
	     WM8903_ANALOGUE_OUT1_LEFT, WM8903_ANALOGUE_OUT1_RIGHT,
	     6, 1, 0),
SOC_DOUBLE_R_TLV("Headphone Volume",
		 WM8903_ANALOGUE_OUT1_LEFT, WM8903_ANALOGUE_OUT1_RIGHT,
		 0, 63, 0, out_tlv),

/* Line out */
SOC_DOUBLE_R("Line Out Switch",
	     WM8903_ANALOGUE_OUT2_LEFT, WM8903_ANALOGUE_OUT2_RIGHT,
	     8, 1, 1),
SOC_DOUBLE_R("Line Out ZC Switch",
	     WM8903_ANALOGUE_OUT2_LEFT, WM8903_ANALOGUE_OUT2_RIGHT,
	     6, 1, 0),
SOC_DOUBLE_R_TLV("Line Out Volume",
		 WM8903_ANALOGUE_OUT2_LEFT, WM8903_ANALOGUE_OUT2_RIGHT,
		 0, 63, 0, out_tlv),

/* Speaker */
SOC_DOUBLE_R("Speaker Switch",
	     WM8903_ANALOGUE_OUT3_LEFT, WM8903_ANALOGUE_OUT3_RIGHT, 8, 1, 1),
SOC_DOUBLE_R("Speaker ZC Switch",
	     WM8903_ANALOGUE_OUT3_LEFT, WM8903_ANALOGUE_OUT3_RIGHT, 6, 1, 0),
SOC_DOUBLE_R_TLV("Speaker Volume",
		 WM8903_ANALOGUE_OUT3_LEFT, WM8903_ANALOGUE_OUT3_RIGHT,
		 0, 63, 0, out_tlv),
};

static const struct snd_kcontrol_new linput_mode_mux =
	SOC_DAPM_ENUM("Left Input Mode Mux", linput_mode_enum);

static const struct snd_kcontrol_new rinput_mode_mux =
	SOC_DAPM_ENUM("Right Input Mode Mux", rinput_mode_enum);

static const struct snd_kcontrol_new linput_mux =
	SOC_DAPM_ENUM("Left Input Mux", linput_enum);

static const struct snd_kcontrol_new linput_inv_mux =
	SOC_DAPM_ENUM("Left Inverting Input Mux", linput_inv_enum);

static const struct snd_kcontrol_new rinput_mux =
	SOC_DAPM_ENUM("Right Input Mux", rinput_enum);

static const struct snd_kcontrol_new rinput_inv_mux =
	SOC_DAPM_ENUM("Right Inverting Input Mux", rinput_inv_enum);

static const struct snd_kcontrol_new lsidetone_mux =
	SOC_DAPM_ENUM("DACL Sidetone Mux", lsidetone_enum);

static const struct snd_kcontrol_new rsidetone_mux =
	SOC_DAPM_ENUM("DACR Sidetone Mux", rsidetone_enum);

static const struct snd_kcontrol_new adcinput_mux =
	SOC_DAPM_ENUM("ADC Input", adcinput_enum);

static const struct snd_kcontrol_new lcapture_mux =
	SOC_DAPM_ENUM("Left Capture Mux", lcapture_enum);

static const struct snd_kcontrol_new rcapture_mux =
	SOC_DAPM_ENUM("Right Capture Mux", rcapture_enum);

static const struct snd_kcontrol_new lplay_mux =
	SOC_DAPM_ENUM("Left Playback Mux", lplay_enum);

static const struct snd_kcontrol_new rplay_mux =
	SOC_DAPM_ENUM("Right Playback Mux", rplay_enum);

static const struct snd_kcontrol_new left_output_mixer[] = {
SOC_DAPM_SINGLE("DACL Switch", WM8903_ANALOGUE_LEFT_MIX_0, 3, 1, 0),
SOC_DAPM_SINGLE("DACR Switch", WM8903_ANALOGUE_LEFT_MIX_0, 2, 1, 0),
SOC_DAPM_SINGLE_W("Left Bypass Switch", WM8903_ANALOGUE_LEFT_MIX_0, 1, 1, 0),
SOC_DAPM_SINGLE_W("Right Bypass Switch", WM8903_ANALOGUE_LEFT_MIX_0, 0, 1, 0),
};

static const struct snd_kcontrol_new right_output_mixer[] = {
SOC_DAPM_SINGLE("DACL Switch", WM8903_ANALOGUE_RIGHT_MIX_0, 3, 1, 0),
SOC_DAPM_SINGLE("DACR Switch", WM8903_ANALOGUE_RIGHT_MIX_0, 2, 1, 0),
SOC_DAPM_SINGLE_W("Left Bypass Switch", WM8903_ANALOGUE_RIGHT_MIX_0, 1, 1, 0),
SOC_DAPM_SINGLE_W("Right Bypass Switch", WM8903_ANALOGUE_RIGHT_MIX_0, 0, 1, 0),
};

static const struct snd_kcontrol_new left_speaker_mixer[] = {
SOC_DAPM_SINGLE("DACL Switch", WM8903_ANALOGUE_SPK_MIX_LEFT_0, 3, 1, 0),
SOC_DAPM_SINGLE("DACR Switch", WM8903_ANALOGUE_SPK_MIX_LEFT_0, 2, 1, 0),
SOC_DAPM_SINGLE("Left Bypass Switch", WM8903_ANALOGUE_SPK_MIX_LEFT_0, 1, 1, 0),
SOC_DAPM_SINGLE("Right Bypass Switch", WM8903_ANALOGUE_SPK_MIX_LEFT_0,
		0, 1, 0),
};

static const struct snd_kcontrol_new right_speaker_mixer[] = {
SOC_DAPM_SINGLE("DACL Switch", WM8903_ANALOGUE_SPK_MIX_RIGHT_0, 3, 1, 0),
SOC_DAPM_SINGLE("DACR Switch", WM8903_ANALOGUE_SPK_MIX_RIGHT_0, 2, 1, 0),
SOC_DAPM_SINGLE("Left Bypass Switch", WM8903_ANALOGUE_SPK_MIX_RIGHT_0,
		1, 1, 0),
SOC_DAPM_SINGLE("Right Bypass Switch", WM8903_ANALOGUE_SPK_MIX_RIGHT_0,
		0, 1, 0),
};

static const struct snd_soc_dapm_widget wm8903_dapm_widgets[] = {
SND_SOC_DAPM_INPUT("IN1L"),
SND_SOC_DAPM_INPUT("IN1R"),
SND_SOC_DAPM_INPUT("IN2L"),
SND_SOC_DAPM_INPUT("IN2R"),
SND_SOC_DAPM_INPUT("IN3L"),
SND_SOC_DAPM_INPUT("IN3R"),
SND_SOC_DAPM_INPUT("DMICDAT"),

SND_SOC_DAPM_OUTPUT("HPOUTL"),
SND_SOC_DAPM_OUTPUT("HPOUTR"),
SND_SOC_DAPM_OUTPUT("LINEOUTL"),
SND_SOC_DAPM_OUTPUT("LINEOUTR"),
SND_SOC_DAPM_OUTPUT("LOP"),
SND_SOC_DAPM_OUTPUT("LON"),
SND_SOC_DAPM_OUTPUT("ROP"),
SND_SOC_DAPM_OUTPUT("RON"),

SND_SOC_DAPM_MICBIAS("Mic Bias", WM8903_MIC_BIAS_CONTROL_0, 0, 0),

SND_SOC_DAPM_MUX("Left Input Mux", SND_SOC_NOPM, 0, 0, &linput_mux),
SND_SOC_DAPM_MUX("Left Input Inverting Mux", SND_SOC_NOPM, 0, 0,
		 &linput_inv_mux),
SND_SOC_DAPM_MUX("Left Input Mode Mux", SND_SOC_NOPM, 0, 0, &linput_mode_mux),

SND_SOC_DAPM_MUX("Right Input Mux", SND_SOC_NOPM, 0, 0, &rinput_mux),
SND_SOC_DAPM_MUX("Right Input Inverting Mux", SND_SOC_NOPM, 0, 0,
		 &rinput_inv_mux),
SND_SOC_DAPM_MUX("Right Input Mode Mux", SND_SOC_NOPM, 0, 0, &rinput_mode_mux),

SND_SOC_DAPM_PGA("Left Input PGA", WM8903_POWER_MANAGEMENT_0, 1, 0, NULL, 0),
SND_SOC_DAPM_PGA("Right Input PGA", WM8903_POWER_MANAGEMENT_0, 0, 0, NULL, 0),

SND_SOC_DAPM_MUX("Left ADC Input", SND_SOC_NOPM, 0, 0, &adcinput_mux),
SND_SOC_DAPM_MUX("Right ADC Input", SND_SOC_NOPM, 0, 0, &adcinput_mux),

SND_SOC_DAPM_ADC("ADCL", NULL, WM8903_POWER_MANAGEMENT_6, 1, 0),
SND_SOC_DAPM_ADC("ADCR", NULL, WM8903_POWER_MANAGEMENT_6, 0, 0),

SND_SOC_DAPM_MUX("Left Capture Mux", SND_SOC_NOPM, 0, 0, &lcapture_mux),
SND_SOC_DAPM_MUX("Right Capture Mux", SND_SOC_NOPM, 0, 0, &rcapture_mux),

SND_SOC_DAPM_AIF_OUT("AIFTXL", "Left HiFi Capture", 0, SND_SOC_NOPM, 0, 0),
SND_SOC_DAPM_AIF_OUT("AIFTXR", "Right HiFi Capture", 0, SND_SOC_NOPM, 0, 0),

SND_SOC_DAPM_MUX("DACL Sidetone", SND_SOC_NOPM, 0, 0, &lsidetone_mux),
SND_SOC_DAPM_MUX("DACR Sidetone", SND_SOC_NOPM, 0, 0, &rsidetone_mux),

SND_SOC_DAPM_AIF_IN("AIFRXL", "Left Playback", 0, SND_SOC_NOPM, 0, 0),
SND_SOC_DAPM_AIF_IN("AIFRXR", "Right Playback", 0, SND_SOC_NOPM, 0, 0),

SND_SOC_DAPM_MUX("Left Playback Mux", SND_SOC_NOPM, 0, 0, &lplay_mux),
SND_SOC_DAPM_MUX("Right Playback Mux", SND_SOC_NOPM, 0, 0, &rplay_mux),

SND_SOC_DAPM_DAC("DACL", NULL, WM8903_POWER_MANAGEMENT_6, 3, 0),
SND_SOC_DAPM_DAC("DACR", NULL, WM8903_POWER_MANAGEMENT_6, 2, 0),

SND_SOC_DAPM_MIXER("Left Output Mixer", WM8903_POWER_MANAGEMENT_1, 1, 0,
		   left_output_mixer, ARRAY_SIZE(left_output_mixer)),
SND_SOC_DAPM_MIXER("Right Output Mixer", WM8903_POWER_MANAGEMENT_1, 0, 0,
		   right_output_mixer, ARRAY_SIZE(right_output_mixer)),

SND_SOC_DAPM_MIXER("Left Speaker Mixer", WM8903_POWER_MANAGEMENT_4, 1, 0,
		   left_speaker_mixer, ARRAY_SIZE(left_speaker_mixer)),
SND_SOC_DAPM_MIXER("Right Speaker Mixer", WM8903_POWER_MANAGEMENT_4, 0, 0,
		   right_speaker_mixer, ARRAY_SIZE(right_speaker_mixer)),

SND_SOC_DAPM_PGA_S("Left Headphone Output PGA", 0, WM8903_POWER_MANAGEMENT_2,
		   1, 0, NULL, 0),
SND_SOC_DAPM_PGA_S("Right Headphone Output PGA", 0, WM8903_POWER_MANAGEMENT_2,
		   0, 0, NULL, 0),

SND_SOC_DAPM_PGA_S("Left Line Output PGA", 0, WM8903_POWER_MANAGEMENT_3, 1, 0,
		   NULL, 0),
SND_SOC_DAPM_PGA_S("Right Line Output PGA", 0, WM8903_POWER_MANAGEMENT_3, 0, 0,
		   NULL, 0),

SND_SOC_DAPM_PGA_S("HPL_RMV_SHORT", 4, WM8903_ANALOGUE_HP_0, 7, 0, NULL, 0),
SND_SOC_DAPM_PGA_S("HPL_ENA_OUTP", 3, WM8903_ANALOGUE_HP_0, 6, 0, NULL, 0),
SND_SOC_DAPM_PGA_S("HPL_ENA_DLY", 2, WM8903_ANALOGUE_HP_0, 5, 0, NULL, 0),
SND_SOC_DAPM_PGA_S("HPL_ENA", 1, WM8903_ANALOGUE_HP_0, 4, 0, NULL, 0),
SND_SOC_DAPM_PGA_S("HPR_RMV_SHORT", 4, WM8903_ANALOGUE_HP_0, 3, 0, NULL, 0),
SND_SOC_DAPM_PGA_S("HPR_ENA_OUTP", 3, WM8903_ANALOGUE_HP_0, 2, 0, NULL, 0),
SND_SOC_DAPM_PGA_S("HPR_ENA_DLY", 2, WM8903_ANALOGUE_HP_0, 1, 0, NULL, 0),
SND_SOC_DAPM_PGA_S("HPR_ENA", 1, WM8903_ANALOGUE_HP_0, 0, 0, NULL, 0),

SND_SOC_DAPM_PGA_S("LINEOUTL_RMV_SHORT", 4, WM8903_ANALOGUE_LINEOUT_0, 7, 0,
		   NULL, 0),
SND_SOC_DAPM_PGA_S("LINEOUTL_ENA_OUTP", 3, WM8903_ANALOGUE_LINEOUT_0, 6, 0,
		   NULL, 0),
SND_SOC_DAPM_PGA_S("LINEOUTL_ENA_DLY", 2, WM8903_ANALOGUE_LINEOUT_0, 5, 0,
		   NULL, 0),
SND_SOC_DAPM_PGA_S("LINEOUTL_ENA", 1, WM8903_ANALOGUE_LINEOUT_0, 4, 0,
		   NULL, 0),
SND_SOC_DAPM_PGA_S("LINEOUTR_RMV_SHORT", 4, WM8903_ANALOGUE_LINEOUT_0, 3, 0,
		   NULL, 0),
SND_SOC_DAPM_PGA_S("LINEOUTR_ENA_OUTP", 3, WM8903_ANALOGUE_LINEOUT_0, 2, 0,
		   NULL, 0),
SND_SOC_DAPM_PGA_S("LINEOUTR_ENA_DLY", 2, WM8903_ANALOGUE_LINEOUT_0, 1, 0,
		   NULL, 0),
SND_SOC_DAPM_PGA_S("LINEOUTR_ENA", 1, WM8903_ANALOGUE_LINEOUT_0, 0, 0,
		   NULL, 0),

SND_SOC_DAPM_SUPPLY("DCS Master", WM8903_DC_SERVO_0, 4, 0, NULL, 0),
SND_SOC_DAPM_PGA_S("HPL_DCS", 3, SND_SOC_NOPM, 3, 0, wm8903_dcs_event,
		   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
SND_SOC_DAPM_PGA_S("HPR_DCS", 3, SND_SOC_NOPM, 2, 0, wm8903_dcs_event,
		   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
SND_SOC_DAPM_PGA_S("LINEOUTL_DCS", 3, SND_SOC_NOPM, 1, 0, wm8903_dcs_event,
		   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
SND_SOC_DAPM_PGA_S("LINEOUTR_DCS", 3, SND_SOC_NOPM, 0, 0, wm8903_dcs_event,
		   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

SND_SOC_DAPM_PGA("Left Speaker PGA", WM8903_POWER_MANAGEMENT_5, 1, 0,
		 NULL, 0),
SND_SOC_DAPM_PGA("Right Speaker PGA", WM8903_POWER_MANAGEMENT_5, 0, 0,
		 NULL, 0),

SND_SOC_DAPM_SUPPLY("Charge Pump", WM8903_CHARGE_PUMP_0, 0, 0,
		    wm8903_cp_event, SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_SUPPLY("CLK_DSP", WM8903_CLOCK_RATES_2, 1, 0, NULL, 0),
SND_SOC_DAPM_SUPPLY("CLK_SYS", WM8903_CLOCK_RATES_2, 2, 0, NULL, 0),
};

static const struct snd_soc_dapm_route wm8903_intercon[] = {

	{ "CLK_DSP", NULL, "CLK_SYS" },
	{ "Mic Bias", NULL, "CLK_SYS" },
	{ "HPL_DCS", NULL, "CLK_SYS" },
	{ "HPR_DCS", NULL, "CLK_SYS" },
	{ "LINEOUTL_DCS", NULL, "CLK_SYS" },
	{ "LINEOUTR_DCS", NULL, "CLK_SYS" },

	{ "Left Input Mux", "IN1L", "IN1L" },
	{ "Left Input Mux", "IN2L", "IN2L" },
	{ "Left Input Mux", "IN3L", "IN3L" },

	{ "Left Input Inverting Mux", "IN1L", "IN1L" },
	{ "Left Input Inverting Mux", "IN2L", "IN2L" },
	{ "Left Input Inverting Mux", "IN3L", "IN3L" },

	{ "Right Input Mux", "IN1R", "IN1R" },
	{ "Right Input Mux", "IN2R", "IN2R" },
	{ "Right Input Mux", "IN3R", "IN3R" },

	{ "Right Input Inverting Mux", "IN1R", "IN1R" },
	{ "Right Input Inverting Mux", "IN2R", "IN2R" },
	{ "Right Input Inverting Mux", "IN3R", "IN3R" },

	{ "Left Input Mode Mux", "Single-Ended", "Left Input Inverting Mux" },
	{ "Left Input Mode Mux", "Differential Line",
	  "Left Input Mux" },
	{ "Left Input Mode Mux", "Differential Line",
	  "Left Input Inverting Mux" },
	{ "Left Input Mode Mux", "Differential Mic",
	  "Left Input Mux" },
	{ "Left Input Mode Mux", "Differential Mic",
	  "Left Input Inverting Mux" },

	{ "Right Input Mode Mux", "Single-Ended",
	  "Right Input Inverting Mux" },
	{ "Right Input Mode Mux", "Differential Line",
	  "Right Input Mux" },
	{ "Right Input Mode Mux", "Differential Line",
	  "Right Input Inverting Mux" },
	{ "Right Input Mode Mux", "Differential Mic",
	  "Right Input Mux" },
	{ "Right Input Mode Mux", "Differential Mic",
	  "Right Input Inverting Mux" },

	{ "Left Input PGA", NULL, "Left Input Mode Mux" },
	{ "Right Input PGA", NULL, "Right Input Mode Mux" },

	{ "Left ADC Input", "ADC", "Left Input PGA" },
	{ "Left ADC Input", "DMIC", "DMICDAT" },
	{ "Right ADC Input", "ADC", "Right Input PGA" },
	{ "Right ADC Input", "DMIC", "DMICDAT" },

	{ "Left Capture Mux", "Left", "ADCL" },
	{ "Left Capture Mux", "Right", "ADCR" },

	{ "Right Capture Mux", "Left", "ADCL" },
	{ "Right Capture Mux", "Right", "ADCR" },

	{ "AIFTXL", NULL, "Left Capture Mux" },
	{ "AIFTXR", NULL, "Right Capture Mux" },

	{ "ADCL", NULL, "Left ADC Input" },
	{ "ADCL", NULL, "CLK_DSP" },
	{ "ADCR", NULL, "Right ADC Input" },
	{ "ADCR", NULL, "CLK_DSP" },

	{ "Left Playback Mux", "Left", "AIFRXL" },
	{ "Left Playback Mux", "Right", "AIFRXR" },

	{ "Right Playback Mux", "Left", "AIFRXL" },
	{ "Right Playback Mux", "Right", "AIFRXR" },

	{ "DACL Sidetone", "Left", "ADCL" },
	{ "DACL Sidetone", "Right", "ADCR" },
	{ "DACR Sidetone", "Left", "ADCL" },
	{ "DACR Sidetone", "Right", "ADCR" },

	{ "DACL", NULL, "Left Playback Mux" },
	{ "DACL", NULL, "DACL Sidetone" },
	{ "DACL", NULL, "CLK_DSP" },

	{ "DACR", NULL, "Right Playback Mux" },
	{ "DACR", NULL, "DACR Sidetone" },
	{ "DACR", NULL, "CLK_DSP" },

	{ "Left Output Mixer", "Left Bypass Switch", "Left Input PGA" },
	{ "Left Output Mixer", "Right Bypass Switch", "Right Input PGA" },
	{ "Left Output Mixer", "DACL Switch", "DACL" },
	{ "Left Output Mixer", "DACR Switch", "DACR" },

	{ "Right Output Mixer", "Left Bypass Switch", "Left Input PGA" },
	{ "Right Output Mixer", "Right Bypass Switch", "Right Input PGA" },
	{ "Right Output Mixer", "DACL Switch", "DACL" },
	{ "Right Output Mixer", "DACR Switch", "DACR" },

	{ "Left Speaker Mixer", "Left Bypass Switch", "Left Input PGA" },
	{ "Left Speaker Mixer", "Right Bypass Switch", "Right Input PGA" },
	{ "Left Speaker Mixer", "DACL Switch", "DACL" },
	{ "Left Speaker Mixer", "DACR Switch", "DACR" },

	{ "Right Speaker Mixer", "Left Bypass Switch", "Left Input PGA" },
	{ "Right Speaker Mixer", "Right Bypass Switch", "Right Input PGA" },
	{ "Right Speaker Mixer", "DACL Switch", "DACL" },
	{ "Right Speaker Mixer", "DACR Switch", "DACR" },

	{ "Left Line Output PGA", NULL, "Left Output Mixer" },
	{ "Right Line Output PGA", NULL, "Right Output Mixer" },

	{ "Left Headphone Output PGA", NULL, "Left Output Mixer" },
	{ "Right Headphone Output PGA", NULL, "Right Output Mixer" },

	{ "Left Speaker PGA", NULL, "Left Speaker Mixer" },
	{ "Right Speaker PGA", NULL, "Right Speaker Mixer" },

	{ "HPL_ENA", NULL, "Left Headphone Output PGA" },
	{ "HPR_ENA", NULL, "Right Headphone Output PGA" },
	{ "HPL_ENA_DLY", NULL, "HPL_ENA" },
	{ "HPR_ENA_DLY", NULL, "HPR_ENA" },
	{ "LINEOUTL_ENA", NULL, "Left Line Output PGA" },
	{ "LINEOUTR_ENA", NULL, "Right Line Output PGA" },
	{ "LINEOUTL_ENA_DLY", NULL, "LINEOUTL_ENA" },
	{ "LINEOUTR_ENA_DLY", NULL, "LINEOUTR_ENA" },

	{ "HPL_DCS", NULL, "DCS Master" },
	{ "HPR_DCS", NULL, "DCS Master" },
	{ "LINEOUTL_DCS", NULL, "DCS Master" },
	{ "LINEOUTR_DCS", NULL, "DCS Master" },

	{ "HPL_DCS", NULL, "HPL_ENA_DLY" },
	{ "HPR_DCS", NULL, "HPR_ENA_DLY" },
	{ "LINEOUTL_DCS", NULL, "LINEOUTL_ENA_DLY" },
	{ "LINEOUTR_DCS", NULL, "LINEOUTR_ENA_DLY" },

	{ "HPL_ENA_OUTP", NULL, "HPL_DCS" },
	{ "HPR_ENA_OUTP", NULL, "HPR_DCS" },
	{ "LINEOUTL_ENA_OUTP", NULL, "LINEOUTL_DCS" },
	{ "LINEOUTR_ENA_OUTP", NULL, "LINEOUTR_DCS" },

	{ "HPL_RMV_SHORT", NULL, "HPL_ENA_OUTP" },
	{ "HPR_RMV_SHORT", NULL, "HPR_ENA_OUTP" },
	{ "LINEOUTL_RMV_SHORT", NULL, "LINEOUTL_ENA_OUTP" },
	{ "LINEOUTR_RMV_SHORT", NULL, "LINEOUTR_ENA_OUTP" },

	{ "HPOUTL", NULL, "HPL_RMV_SHORT" },
	{ "HPOUTR", NULL, "HPR_RMV_SHORT" },
	{ "LINEOUTL", NULL, "LINEOUTL_RMV_SHORT" },
	{ "LINEOUTR", NULL, "LINEOUTR_RMV_SHORT" },

	{ "LOP", NULL, "Left Speaker PGA" },
	{ "LON", NULL, "Left Speaker PGA" },

	{ "ROP", NULL, "Right Speaker PGA" },
	{ "RON", NULL, "Right Speaker PGA" },

	{ "Charge Pump", NULL, "CLK_DSP" },

	{ "Left Headphone Output PGA", NULL, "Charge Pump" },
	{ "Right Headphone Output PGA", NULL, "Charge Pump" },
	{ "Left Line Output PGA", NULL, "Charge Pump" },
	{ "Right Line Output PGA", NULL, "Charge Pump" },
};

static int wm8903_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		snd_soc_update_bits(codec, WM8903_VMID_CONTROL_0,
				    WM8903_VMID_RES_MASK,
				    WM8903_VMID_RES_50K);
		break;

	case SND_SOC_BIAS_STANDBY:
		if (codec->dapm.bias_level == SND_SOC_BIAS_OFF) {
			snd_soc_update_bits(codec, WM8903_BIAS_CONTROL_0,
					    WM8903_POBCTRL | WM8903_ISEL_MASK |
					    WM8903_STARTUP_BIAS_ENA |
					    WM8903_BIAS_ENA,
					    WM8903_POBCTRL |
					    (2 << WM8903_ISEL_SHIFT) |
					    WM8903_STARTUP_BIAS_ENA);

			snd_soc_update_bits(codec,
					    WM8903_ANALOGUE_SPK_OUTPUT_CONTROL_0,
					    WM8903_SPK_DISCHARGE,
					    WM8903_SPK_DISCHARGE);

			msleep(33);

			snd_soc_update_bits(codec, WM8903_POWER_MANAGEMENT_5,
					    WM8903_SPKL_ENA | WM8903_SPKR_ENA,
					    WM8903_SPKL_ENA | WM8903_SPKR_ENA);

			snd_soc_update_bits(codec,
					    WM8903_ANALOGUE_SPK_OUTPUT_CONTROL_0,
					    WM8903_SPK_DISCHARGE, 0);

			snd_soc_update_bits(codec, WM8903_VMID_CONTROL_0,
					    WM8903_VMID_TIE_ENA |
					    WM8903_BUFIO_ENA |
					    WM8903_VMID_IO_ENA |
					    WM8903_VMID_SOFT_MASK |
					    WM8903_VMID_RES_MASK |
					    WM8903_VMID_BUF_ENA,
					    WM8903_VMID_TIE_ENA |
					    WM8903_BUFIO_ENA |
					    WM8903_VMID_IO_ENA |
					    (2 << WM8903_VMID_SOFT_SHIFT) |
					    WM8903_VMID_RES_250K |
					    WM8903_VMID_BUF_ENA);

			msleep(129);

			snd_soc_update_bits(codec, WM8903_POWER_MANAGEMENT_5,
					    WM8903_SPKL_ENA | WM8903_SPKR_ENA,
					    0);

			snd_soc_update_bits(codec, WM8903_VMID_CONTROL_0,
					    WM8903_VMID_SOFT_MASK, 0);

			snd_soc_update_bits(codec, WM8903_VMID_CONTROL_0,
					    WM8903_VMID_RES_MASK,
					    WM8903_VMID_RES_50K);

			snd_soc_update_bits(codec, WM8903_BIAS_CONTROL_0,
					    WM8903_BIAS_ENA | WM8903_POBCTRL,
					    WM8903_BIAS_ENA);

			/* By default no bypass paths are enabled so
			 * enable Class W support.
			 */
			dev_dbg(codec->dev, "Enabling Class W\n");
			snd_soc_update_bits(codec, WM8903_CLASS_W_0,
					    WM8903_CP_DYN_FREQ |
					    WM8903_CP_DYN_V,
					    WM8903_CP_DYN_FREQ |
					    WM8903_CP_DYN_V);
		}

		snd_soc_update_bits(codec, WM8903_VMID_CONTROL_0,
				    WM8903_VMID_RES_MASK,
				    WM8903_VMID_RES_250K);
		break;

	case SND_SOC_BIAS_OFF:
		snd_soc_update_bits(codec, WM8903_BIAS_CONTROL_0,
				    WM8903_BIAS_ENA, 0);

		snd_soc_update_bits(codec, WM8903_VMID_CONTROL_0,
				    WM8903_VMID_SOFT_MASK,
				    2 << WM8903_VMID_SOFT_SHIFT);

		snd_soc_update_bits(codec, WM8903_VMID_CONTROL_0,
				    WM8903_VMID_BUF_ENA, 0);

		msleep(290);

		snd_soc_update_bits(codec, WM8903_VMID_CONTROL_0,
				    WM8903_VMID_TIE_ENA | WM8903_BUFIO_ENA |
				    WM8903_VMID_IO_ENA | WM8903_VMID_RES_MASK |
				    WM8903_VMID_SOFT_MASK |
				    WM8903_VMID_BUF_ENA, 0);

		snd_soc_update_bits(codec, WM8903_BIAS_CONTROL_0,
				    WM8903_STARTUP_BIAS_ENA, 0);
		break;
	}

	codec->dapm.bias_level = level;

	return 0;
}

static int wm8903_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				 int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct wm8903_priv *wm8903 = snd_soc_codec_get_drvdata(codec);

	wm8903->sysclk = freq;

	return 0;
}

static int wm8903_set_dai_fmt(struct snd_soc_dai *codec_dai,
			      unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 aif1 = snd_soc_read(codec, WM8903_AUDIO_INTERFACE_1);

	aif1 &= ~(WM8903_LRCLK_DIR | WM8903_BCLK_DIR | WM8903_AIF_FMT_MASK |
		  WM8903_AIF_LRCLK_INV | WM8903_AIF_BCLK_INV);

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	case SND_SOC_DAIFMT_CBS_CFM:
		aif1 |= WM8903_LRCLK_DIR;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		aif1 |= WM8903_LRCLK_DIR | WM8903_BCLK_DIR;
		break;
	case SND_SOC_DAIFMT_CBM_CFS:
		aif1 |= WM8903_BCLK_DIR;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		aif1 |= 0x3;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		aif1 |= 0x3 | WM8903_AIF_LRCLK_INV;
		break;
	case SND_SOC_DAIFMT_I2S:
		aif1 |= 0x2;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		aif1 |= 0x1;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		break;
	default:
		return -EINVAL;
	}

	/* Clock inversion */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
	case SND_SOC_DAIFMT_DSP_B:
		/* frame inversion not valid for DSP modes */
		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
			break;
		case SND_SOC_DAIFMT_IB_NF:
			aif1 |= WM8903_AIF_BCLK_INV;
			break;
		default:
			return -EINVAL;
		}
		break;
	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_RIGHT_J:
	case SND_SOC_DAIFMT_LEFT_J:
		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
			break;
		case SND_SOC_DAIFMT_IB_IF:
			aif1 |= WM8903_AIF_BCLK_INV | WM8903_AIF_LRCLK_INV;
			break;
		case SND_SOC_DAIFMT_IB_NF:
			aif1 |= WM8903_AIF_BCLK_INV;
			break;
		case SND_SOC_DAIFMT_NB_IF:
			aif1 |= WM8903_AIF_LRCLK_INV;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	snd_soc_write(codec, WM8903_AUDIO_INTERFACE_1, aif1);

	return 0;
}

static int wm8903_digital_mute(struct snd_soc_dai *codec_dai, int mute)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 reg;

	reg = snd_soc_read(codec, WM8903_DAC_DIGITAL_1);

	if (mute)
		reg |= WM8903_DAC_MUTE;
	else
		reg &= ~WM8903_DAC_MUTE;

	snd_soc_write(codec, WM8903_DAC_DIGITAL_1, reg);

	return 0;
}

/* Lookup table for CLK_SYS/fs ratio.  256fs or more is recommended
 * for optimal performance so we list the lower rates first and match
 * on the last match we find. */
static struct {
	int div;
	int rate;
	int mode;
	int mclk_div;
} clk_sys_ratios[] = {
	{   64, 0x0, 0x0, 1 },
	{   68, 0x0, 0x1, 1 },
	{  125, 0x0, 0x2, 1 },
	{  128, 0x1, 0x0, 1 },
	{  136, 0x1, 0x1, 1 },
	{  192, 0x2, 0x0, 1 },
	{  204, 0x2, 0x1, 1 },

	{   64, 0x0, 0x0, 2 },
	{   68, 0x0, 0x1, 2 },
	{  125, 0x0, 0x2, 2 },
	{  128, 0x1, 0x0, 2 },
	{  136, 0x1, 0x1, 2 },
	{  192, 0x2, 0x0, 2 },
	{  204, 0x2, 0x1, 2 },

	{  250, 0x2, 0x2, 1 },
	{  256, 0x3, 0x0, 1 },
	{  272, 0x3, 0x1, 1 },
	{  384, 0x4, 0x0, 1 },
	{  408, 0x4, 0x1, 1 },
	{  375, 0x4, 0x2, 1 },
	{  512, 0x5, 0x0, 1 },
	{  544, 0x5, 0x1, 1 },
	{  500, 0x5, 0x2, 1 },
	{  768, 0x6, 0x0, 1 },
	{  816, 0x6, 0x1, 1 },
	{  750, 0x6, 0x2, 1 },
	{ 1024, 0x7, 0x0, 1 },
	{ 1088, 0x7, 0x1, 1 },
	{ 1000, 0x7, 0x2, 1 },
	{ 1408, 0x8, 0x0, 1 },
	{ 1496, 0x8, 0x1, 1 },
	{ 1536, 0x9, 0x0, 1 },
	{ 1632, 0x9, 0x1, 1 },
	{ 1500, 0x9, 0x2, 1 },

	{  250, 0x2, 0x2, 2 },
	{  256, 0x3, 0x0, 2 },
	{  272, 0x3, 0x1, 2 },
	{  384, 0x4, 0x0, 2 },
	{  408, 0x4, 0x1, 2 },
	{  375, 0x4, 0x2, 2 },
	{  512, 0x5, 0x0, 2 },
	{  544, 0x5, 0x1, 2 },
	{  500, 0x5, 0x2, 2 },
	{  768, 0x6, 0x0, 2 },
	{  816, 0x6, 0x1, 2 },
	{  750, 0x6, 0x2, 2 },
	{ 1024, 0x7, 0x0, 2 },
	{ 1088, 0x7, 0x1, 2 },
	{ 1000, 0x7, 0x2, 2 },
	{ 1408, 0x8, 0x0, 2 },
	{ 1496, 0x8, 0x1, 2 },
	{ 1536, 0x9, 0x0, 2 },
	{ 1632, 0x9, 0x1, 2 },
	{ 1500, 0x9, 0x2, 2 },
};

/* CLK_SYS/BCLK ratios - multiplied by 10 due to .5s */
static struct {
	int ratio;
	int div;
} bclk_divs[] = {
	{  10,  0 },
	{  20,  2 },
	{  30,  3 },
	{  40,  4 },
	{  50,  5 },
	{  60,  7 },
	{  80,  8 },
	{ 100,  9 },
	{ 120, 11 },
	{ 160, 12 },
	{ 200, 13 },
	{ 220, 14 },
	{ 240, 15 },
	{ 300, 17 },
	{ 320, 18 },
	{ 440, 19 },
	{ 480, 20 },
};

/* Sample rates for DSP */
static struct {
	int rate;
	int value;
} sample_rates[] = {
	{  8000,  0 },
	{ 11025,  1 },
	{ 12000,  2 },
	{ 16000,  3 },
	{ 22050,  4 },
	{ 24000,  5 },
	{ 32000,  6 },
	{ 44100,  7 },
	{ 48000,  8 },
	{ 88200,  9 },
	{ 96000, 10 },
	{ 0,      0 },
};

static int wm8903_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec =rtd->codec;
	struct wm8903_priv *wm8903 = snd_soc_codec_get_drvdata(codec);
	int fs = params_rate(params);
	int bclk;
	int bclk_div;
	int i;
	int dsp_config;
	int clk_config;
	int best_val;
	int cur_val;
	int clk_sys;

	u16 aif1 = snd_soc_read(codec, WM8903_AUDIO_INTERFACE_1);
	u16 aif2 = snd_soc_read(codec, WM8903_AUDIO_INTERFACE_2);
	u16 aif3 = snd_soc_read(codec, WM8903_AUDIO_INTERFACE_3);
	u16 clock0 = snd_soc_read(codec, WM8903_CLOCK_RATES_0);
	u16 clock1 = snd_soc_read(codec, WM8903_CLOCK_RATES_1);
	u16 dac_digital1 = snd_soc_read(codec, WM8903_DAC_DIGITAL_1);

	/* Enable sloping stopband filter for low sample rates */
	if (fs <= 24000)
		dac_digital1 |= WM8903_DAC_SB_FILT;
	else
		dac_digital1 &= ~WM8903_DAC_SB_FILT;

	/* Configure sample rate logic for DSP - choose nearest rate */
	dsp_config = 0;
	best_val = abs(sample_rates[dsp_config].rate - fs);
	for (i = 1; i < ARRAY_SIZE(sample_rates); i++) {
		cur_val = abs(sample_rates[i].rate - fs);
		if (cur_val <= best_val) {
			dsp_config = i;
			best_val = cur_val;
		}
	}

	dev_dbg(codec->dev, "DSP fs = %dHz\n", sample_rates[dsp_config].rate);
	clock1 &= ~WM8903_SAMPLE_RATE_MASK;
	clock1 |= sample_rates[dsp_config].value;

	aif1 &= ~WM8903_AIF_WL_MASK;
	bclk = 2 * fs;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		bclk *= 16;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		bclk *= 20;
		aif1 |= 0x4;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		bclk *= 24;
		aif1 |= 0x8;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		bclk *= 32;
		aif1 |= 0xc;
		break;
	default:
		return -EINVAL;
	}

	dev_dbg(codec->dev, "MCLK = %dHz, target sample rate = %dHz\n",
		wm8903->sysclk, fs);

	/* We may not have an MCLK which allows us to generate exactly
	 * the clock we want, particularly with USB derived inputs, so
	 * approximate.
	 */
	clk_config = 0;
	best_val = abs((wm8903->sysclk /
			(clk_sys_ratios[0].mclk_div *
			 clk_sys_ratios[0].div)) - fs);
	for (i = 1; i < ARRAY_SIZE(clk_sys_ratios); i++) {
		cur_val = abs((wm8903->sysclk /
			       (clk_sys_ratios[i].mclk_div *
				clk_sys_ratios[i].div)) - fs);

		if (cur_val <= best_val) {
			clk_config = i;
			best_val = cur_val;
		}
	}

	if (clk_sys_ratios[clk_config].mclk_div == 2) {
		clock0 |= WM8903_MCLKDIV2;
		clk_sys = wm8903->sysclk / 2;
	} else {
		clock0 &= ~WM8903_MCLKDIV2;
		clk_sys = wm8903->sysclk;
	}

	clock1 &= ~(WM8903_CLK_SYS_RATE_MASK |
		    WM8903_CLK_SYS_MODE_MASK);
	clock1 |= clk_sys_ratios[clk_config].rate << WM8903_CLK_SYS_RATE_SHIFT;
	clock1 |= clk_sys_ratios[clk_config].mode << WM8903_CLK_SYS_MODE_SHIFT;

	dev_dbg(codec->dev, "CLK_SYS_RATE=%x, CLK_SYS_MODE=%x div=%d\n",
		clk_sys_ratios[clk_config].rate,
		clk_sys_ratios[clk_config].mode,
		clk_sys_ratios[clk_config].div);

	dev_dbg(codec->dev, "Actual CLK_SYS = %dHz\n", clk_sys);

	/* We may not get quite the right frequency if using
	 * approximate clocks so look for the closest match that is
	 * higher than the target (we need to ensure that there enough
	 * BCLKs to clock out the samples).
	 */
	bclk_div = 0;
	best_val = ((clk_sys * 10) / bclk_divs[0].ratio) - bclk;
	i = 1;
	while (i < ARRAY_SIZE(bclk_divs)) {
		cur_val = ((clk_sys * 10) / bclk_divs[i].ratio) - bclk;
		if (cur_val < 0) /* BCLK table is sorted */
			break;
		bclk_div = i;
		best_val = cur_val;
		i++;
	}

	aif2 &= ~WM8903_BCLK_DIV_MASK;
	aif3 &= ~WM8903_LRCLK_RATE_MASK;

	dev_dbg(codec->dev, "BCLK ratio %d for %dHz - actual BCLK = %dHz\n",
		bclk_divs[bclk_div].ratio / 10, bclk,
		(clk_sys * 10) / bclk_divs[bclk_div].ratio);

	aif2 |= bclk_divs[bclk_div].div;
	aif3 |= bclk / fs;

	wm8903->fs = params_rate(params);
	wm8903_set_deemph(codec);

	snd_soc_write(codec, WM8903_CLOCK_RATES_0, clock0);
	snd_soc_write(codec, WM8903_CLOCK_RATES_1, clock1);
	snd_soc_write(codec, WM8903_AUDIO_INTERFACE_1, aif1);
	snd_soc_write(codec, WM8903_AUDIO_INTERFACE_2, aif2);
	snd_soc_write(codec, WM8903_AUDIO_INTERFACE_3, aif3);
	snd_soc_write(codec, WM8903_DAC_DIGITAL_1, dac_digital1);

	return 0;
}

/**
 * wm8903_mic_detect - Enable microphone detection via the WM8903 IRQ
 *
 * @codec:  WM8903 codec
 * @jack:   jack to report detection events on
 * @det:    value to report for presence detection
 * @shrt:   value to report for short detection
 *
 * Enable microphone detection via IRQ on the WM8903.  If GPIOs are
 * being used to bring out signals to the processor then only platform
 * data configuration is needed for WM8903 and processor GPIOs should
 * be configured using snd_soc_jack_add_gpios() instead.
 *
 * The current threasholds for detection should be configured using
 * micdet_cfg in the platform data.  Using this function will force on
 * the microphone bias for the device.
 */
int wm8903_mic_detect(struct snd_soc_codec *codec, struct snd_soc_jack *jack,
		      int det, int shrt)
{
	struct wm8903_priv *wm8903 = snd_soc_codec_get_drvdata(codec);
	int irq_mask = WM8903_MICDET_EINT | WM8903_MICSHRT_EINT;

	dev_dbg(codec->dev, "Enabling microphone detection: %x %x\n",
		det, shrt);

	/* Store the configuration */
	wm8903->mic_jack = jack;
	wm8903->mic_det = det;
	wm8903->mic_short = shrt;

	/* Enable interrupts we've got a report configured for */
	if (det)
		irq_mask &= ~WM8903_MICDET_EINT;
	if (shrt)
		irq_mask &= ~WM8903_MICSHRT_EINT;

	snd_soc_update_bits(codec, WM8903_INTERRUPT_STATUS_1_MASK,
			    WM8903_MICDET_EINT | WM8903_MICSHRT_EINT,
			    irq_mask);

	if (det || shrt) {
		/* Enable mic detection, this may not have been set through
		 * platform data (eg, if the defaults are OK). */
		snd_soc_update_bits(codec, WM8903_WRITE_SEQUENCER_0,
				    WM8903_WSEQ_ENA, WM8903_WSEQ_ENA);
		snd_soc_update_bits(codec, WM8903_MIC_BIAS_CONTROL_0,
				    WM8903_MICDET_ENA, WM8903_MICDET_ENA);
	} else {
		snd_soc_update_bits(codec, WM8903_MIC_BIAS_CONTROL_0,
				    WM8903_MICDET_ENA, 0);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(wm8903_mic_detect);

static irqreturn_t wm8903_irq(int irq, void *data)
{
	struct snd_soc_codec *codec = data;
	struct wm8903_priv *wm8903 = snd_soc_codec_get_drvdata(codec);
	int mic_report;
	int int_pol;
	int int_val = 0;
	int mask = ~snd_soc_read(codec, WM8903_INTERRUPT_STATUS_1_MASK);

	int_val = snd_soc_read(codec, WM8903_INTERRUPT_STATUS_1) & mask;

	if (int_val & WM8903_WSEQ_BUSY_EINT) {
		dev_warn(codec->dev, "Write sequencer done\n");
	}

	/*
	 * The rest is microphone jack detection.  We need to manually
	 * invert the polarity of the interrupt after each event - to
	 * simplify the code keep track of the last state we reported
	 * and just invert the relevant bits in both the report and
	 * the polarity register.
	 */
	mic_report = wm8903->mic_last_report;
	int_pol = snd_soc_read(codec, WM8903_INTERRUPT_POLARITY_1);

#ifndef CONFIG_SND_SOC_WM8903_MODULE
	if (int_val & (WM8903_MICSHRT_EINT | WM8903_MICDET_EINT))
		trace_snd_soc_jack_irq(dev_name(codec->dev));
#endif

	if (int_val & WM8903_MICSHRT_EINT) {
		dev_dbg(codec->dev, "Microphone short (pol=%x)\n", int_pol);

		mic_report ^= wm8903->mic_short;
		int_pol ^= WM8903_MICSHRT_INV;
	}

	if (int_val & WM8903_MICDET_EINT) {
		dev_dbg(codec->dev, "Microphone detect (pol=%x)\n", int_pol);

		mic_report ^= wm8903->mic_det;
		int_pol ^= WM8903_MICDET_INV;

		msleep(wm8903->mic_delay);
	}

	snd_soc_update_bits(codec, WM8903_INTERRUPT_POLARITY_1,
			    WM8903_MICSHRT_INV | WM8903_MICDET_INV, int_pol);

	snd_soc_jack_report(wm8903->mic_jack, mic_report,
			    wm8903->mic_short | wm8903->mic_det);

	wm8903->mic_last_report = mic_report;

	return IRQ_HANDLED;
}

#define WM8903_PLAYBACK_RATES (SNDRV_PCM_RATE_8000 |\
			       SNDRV_PCM_RATE_11025 |	\
			       SNDRV_PCM_RATE_16000 |	\
			       SNDRV_PCM_RATE_22050 |	\
			       SNDRV_PCM_RATE_32000 |	\
			       SNDRV_PCM_RATE_44100 |	\
			       SNDRV_PCM_RATE_48000 |	\
			       SNDRV_PCM_RATE_88200 |	\
			       SNDRV_PCM_RATE_96000)

#define WM8903_CAPTURE_RATES (SNDRV_PCM_RATE_8000 |\
			      SNDRV_PCM_RATE_11025 |	\
			      SNDRV_PCM_RATE_16000 |	\
			      SNDRV_PCM_RATE_22050 |	\
			      SNDRV_PCM_RATE_32000 |	\
			      SNDRV_PCM_RATE_44100 |	\
			      SNDRV_PCM_RATE_48000)

#define WM8903_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
			SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE)

static struct snd_soc_dai_ops wm8903_dai_ops = {
	.hw_params	= wm8903_hw_params,
	.digital_mute	= wm8903_digital_mute,
	.set_fmt	= wm8903_set_dai_fmt,
	.set_sysclk	= wm8903_set_dai_sysclk,
};

static struct snd_soc_dai_driver wm8903_dai = {
	.name = "wm8903-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = WM8903_PLAYBACK_RATES,
		.formats = WM8903_FORMATS,
	},
	.capture = {
		 .stream_name = "Capture",
		 .channels_min = 2,
		 .channels_max = 2,
		 .rates = WM8903_CAPTURE_RATES,
		 .formats = WM8903_FORMATS,
	 },
	.ops = &wm8903_dai_ops,
	.symmetric_rates = 1,
};

static int wm8903_suspend(struct snd_soc_codec *codec, pm_message_t state)
{
	wm8903_set_bias_level(codec, SND_SOC_BIAS_OFF);

	return 0;
}

static int wm8903_resume(struct snd_soc_codec *codec)
{
	int i;
	u16 *reg_cache = codec->reg_cache;
	u16 *tmp_cache = kmemdup(reg_cache, sizeof(wm8903_reg_defaults),
				 GFP_KERNEL);

	/* Bring the codec back up to standby first to minimise pop/clicks */
	wm8903_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	/* Sync back everything else */
	if (tmp_cache) {
		for (i = 2; i < ARRAY_SIZE(wm8903_reg_defaults); i++)
			if (tmp_cache[i] != reg_cache[i])
				snd_soc_write(codec, i, tmp_cache[i]);
		kfree(tmp_cache);
	} else {
		dev_err(codec->dev, "Failed to allocate temporary cache\n");
	}

	return 0;
}

#ifdef CONFIG_GPIOLIB
static inline struct wm8903_priv *gpio_to_wm8903(struct gpio_chip *chip)
{
	return container_of(chip, struct wm8903_priv, gpio_chip);
}

static int wm8903_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	if (offset >= WM8903_NUM_GPIO)
		return -EINVAL;

	return 0;
}

static int wm8903_gpio_direction_in(struct gpio_chip *chip, unsigned offset)
{
	struct wm8903_priv *wm8903 = gpio_to_wm8903(chip);
	struct snd_soc_codec *codec = wm8903->codec;
	unsigned int mask, val;

	mask = WM8903_GP1_FN_MASK | WM8903_GP1_DIR_MASK;
	val = (WM8903_GPn_FN_GPIO_INPUT << WM8903_GP1_FN_SHIFT) |
		WM8903_GP1_DIR;

	return snd_soc_update_bits(codec, WM8903_GPIO_CONTROL_1 + offset,
				   mask, val);
}

static int wm8903_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct wm8903_priv *wm8903 = gpio_to_wm8903(chip);
	struct snd_soc_codec *codec = wm8903->codec;
	int reg;

	reg = snd_soc_read(codec, WM8903_GPIO_CONTROL_1 + offset);

	return (reg & WM8903_GP1_LVL_MASK) >> WM8903_GP1_LVL_SHIFT;
}

static int wm8903_gpio_direction_out(struct gpio_chip *chip,
				     unsigned offset, int value)
{
	struct wm8903_priv *wm8903 = gpio_to_wm8903(chip);
	struct snd_soc_codec *codec = wm8903->codec;
	unsigned int mask, val;

	mask = WM8903_GP1_FN_MASK | WM8903_GP1_DIR_MASK | WM8903_GP1_LVL_MASK;
	val = (WM8903_GPn_FN_GPIO_OUTPUT << WM8903_GP1_FN_SHIFT) |
		(value << WM8903_GP2_LVL_SHIFT);

	return snd_soc_update_bits(codec, WM8903_GPIO_CONTROL_1 + offset,
				   mask, val);
}

static void wm8903_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct wm8903_priv *wm8903 = gpio_to_wm8903(chip);
	struct snd_soc_codec *codec = wm8903->codec;

	snd_soc_update_bits(codec, WM8903_GPIO_CONTROL_1 + offset,
			    WM8903_GP1_LVL_MASK,
			    !!value << WM8903_GP1_LVL_SHIFT);
}

static struct gpio_chip wm8903_template_chip = {
	.label			= "wm8903",
	.owner			= THIS_MODULE,
	.request		= wm8903_gpio_request,
	.direction_input	= wm8903_gpio_direction_in,
	.get			= wm8903_gpio_get,
	.direction_output	= wm8903_gpio_direction_out,
	.set			= wm8903_gpio_set,
	.can_sleep		= 1,
};

static void wm8903_init_gpio(struct snd_soc_codec *codec)
{
	struct wm8903_priv *wm8903 = snd_soc_codec_get_drvdata(codec);
	struct wm8903_platform_data *pdata = dev_get_platdata(codec->dev);
	int ret;

	wm8903->gpio_chip = wm8903_template_chip;
	wm8903->gpio_chip.ngpio = WM8903_NUM_GPIO;
	wm8903->gpio_chip.dev = codec->dev;

	if (pdata && pdata->gpio_base)
		wm8903->gpio_chip.base = pdata->gpio_base;
	else
		wm8903->gpio_chip.base = -1;

	ret = gpiochip_add(&wm8903->gpio_chip);
	if (ret != 0)
		dev_err(codec->dev, "Failed to add GPIOs: %d\n", ret);
}

static void wm8903_free_gpio(struct snd_soc_codec *codec)
{
	struct wm8903_priv *wm8903 = snd_soc_codec_get_drvdata(codec);
	int ret;

	ret = gpiochip_remove(&wm8903->gpio_chip);
	if (ret != 0)
		dev_err(codec->dev, "Failed to remove GPIOs: %d\n", ret);
}
#else
static void wm8903_init_gpio(struct snd_soc_codec *codec)
{
}

static void wm8903_free_gpio(struct snd_soc_codec *codec)
{
}
#endif

static int wm8903_probe(struct snd_soc_codec *codec)
{
	struct wm8903_platform_data *pdata = dev_get_platdata(codec->dev);
	struct wm8903_priv *wm8903 = snd_soc_codec_get_drvdata(codec);
	int ret, i;
	int trigger, irq_pol;
	u16 val;

	wm8903->codec = codec;

	ret = snd_soc_codec_set_cache_io(codec, 8, 16, SND_SOC_I2C);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to set cache I/O: %d\n", ret);
		return ret;
	}

	val = snd_soc_read(codec, WM8903_SW_RESET_AND_ID);
	if (val != wm8903_reg_defaults[WM8903_SW_RESET_AND_ID]) {
		dev_err(codec->dev,
			"Device with ID register %x is not a WM8903\n", val);
		return -ENODEV;
	}

	val = snd_soc_read(codec, WM8903_REVISION_NUMBER);
	dev_info(codec->dev, "WM8903 revision %c\n",
		 (val & WM8903_CHIP_REV_MASK) + 'A');

	wm8903_reset(codec);

	/* Set up GPIOs and microphone detection */
	if (pdata) {
		bool mic_gpio = false;

		for (i = 0; i < ARRAY_SIZE(pdata->gpio_cfg); i++) {
			if (pdata->gpio_cfg[i] == WM8903_GPIO_NO_CONFIG)
				continue;

			snd_soc_write(codec, WM8903_GPIO_CONTROL_1 + i,
				      pdata->gpio_cfg[i] & 0xffff);

			val = (pdata->gpio_cfg[i] & WM8903_GP1_FN_MASK)
				>> WM8903_GP1_FN_SHIFT;

			switch (val) {
			case WM8903_GPn_FN_MICBIAS_CURRENT_DETECT:
			case WM8903_GPn_FN_MICBIAS_SHORT_DETECT:
				mic_gpio = true;
				break;
			default:
				break;
			}
		}

		snd_soc_write(codec, WM8903_MIC_BIAS_CONTROL_0,
			      pdata->micdet_cfg);

		/* Microphone detection needs the WSEQ clock */
		if (pdata->micdet_cfg)
			snd_soc_update_bits(codec, WM8903_WRITE_SEQUENCER_0,
					    WM8903_WSEQ_ENA, WM8903_WSEQ_ENA);

		/* If microphone detection is enabled by pdata but
		 * detected via IRQ then interrupts can be lost before
		 * the machine driver has set up microphone detection
		 * IRQs as the IRQs are clear on read.  The detection
		 * will be enabled when the machine driver configures.
		 */
		WARN_ON(!mic_gpio && (pdata->micdet_cfg & WM8903_MICDET_ENA));

		wm8903->mic_delay = pdata->micdet_delay;
	}
	
	if (wm8903->irq) {
		if (pdata && pdata->irq_active_low) {
			trigger = IRQF_TRIGGER_LOW;
			irq_pol = WM8903_IRQ_POL;
		} else {
			trigger = IRQF_TRIGGER_HIGH;
			irq_pol = 0;
		}

		snd_soc_update_bits(codec, WM8903_INTERRUPT_CONTROL,
				    WM8903_IRQ_POL, irq_pol);
		
		ret = request_threaded_irq(wm8903->irq, NULL, wm8903_irq,
					   trigger | IRQF_ONESHOT,
					   "wm8903", codec);
		if (ret != 0) {
			dev_err(codec->dev, "Failed to request IRQ: %d\n",
				ret);
			return ret;
		}

		/* Enable write sequencer interrupts */
		snd_soc_update_bits(codec, WM8903_INTERRUPT_STATUS_1_MASK,
				    WM8903_IM_WSEQ_BUSY_EINT, 0);
	}

	/* power on device */
	wm8903_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	/* Latch volume update bits */
	val = snd_soc_read(codec, WM8903_ADC_DIGITAL_VOLUME_LEFT);
	val |= WM8903_ADCVU;
	snd_soc_write(codec, WM8903_ADC_DIGITAL_VOLUME_LEFT, val);
	snd_soc_write(codec, WM8903_ADC_DIGITAL_VOLUME_RIGHT, val);

	val = snd_soc_read(codec, WM8903_DAC_DIGITAL_VOLUME_LEFT);
	val |= WM8903_DACVU;
	snd_soc_write(codec, WM8903_DAC_DIGITAL_VOLUME_LEFT, val);
	snd_soc_write(codec, WM8903_DAC_DIGITAL_VOLUME_RIGHT, val);

	val = snd_soc_read(codec, WM8903_ANALOGUE_OUT1_LEFT);
	val |= WM8903_HPOUTVU;
	snd_soc_write(codec, WM8903_ANALOGUE_OUT1_LEFT, val);
	snd_soc_write(codec, WM8903_ANALOGUE_OUT1_RIGHT, val);

	val = snd_soc_read(codec, WM8903_ANALOGUE_OUT2_LEFT);
	val |= WM8903_LINEOUTVU;
	snd_soc_write(codec, WM8903_ANALOGUE_OUT2_LEFT, val);
	snd_soc_write(codec, WM8903_ANALOGUE_OUT2_RIGHT, val);

	val = snd_soc_read(codec, WM8903_ANALOGUE_OUT3_LEFT);
	val |= WM8903_SPKVU;
	snd_soc_write(codec, WM8903_ANALOGUE_OUT3_LEFT, val);
	snd_soc_write(codec, WM8903_ANALOGUE_OUT3_RIGHT, val);

	/* Enable DAC soft mute by default */
	snd_soc_update_bits(codec, WM8903_DAC_DIGITAL_1,
			    WM8903_DAC_MUTEMODE | WM8903_DAC_MUTE,
			    WM8903_DAC_MUTEMODE | WM8903_DAC_MUTE);

	snd_soc_add_controls(codec, wm8903_snd_controls,
				ARRAY_SIZE(wm8903_snd_controls));

	wm8903_init_gpio(codec);

	return ret;
}

/* power down chip */
static int wm8903_remove(struct snd_soc_codec *codec)
{
	wm8903_free_gpio(codec);
	wm8903_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_wm8903 = {
	.probe =	wm8903_probe,
	.remove =	wm8903_remove,
	.suspend =	wm8903_suspend,
	.resume =	wm8903_resume,
	.set_bias_level = wm8903_set_bias_level,
	.reg_cache_size = ARRAY_SIZE(wm8903_reg_defaults),
	.reg_word_size = sizeof(u16),
	.reg_cache_default = wm8903_reg_defaults,
	.volatile_register = wm8903_volatile_register,
	.seq_notifier = wm8903_seq_notifier,
	.dapm_widgets = wm8903_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(wm8903_dapm_widgets),
	.dapm_routes = wm8903_intercon,
	.num_dapm_routes = ARRAY_SIZE(wm8903_intercon),
};

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
static __devinit int wm8903_i2c_probe(struct i2c_client *i2c,
				      const struct i2c_device_id *id)
{
	struct wm8903_priv *wm8903;
	int ret;

	wm8903 = kzalloc(sizeof(struct wm8903_priv), GFP_KERNEL);
	if (wm8903 == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, wm8903);
	wm8903->irq = i2c->irq;

	ret = snd_soc_register_codec(&i2c->dev,
			&soc_codec_dev_wm8903, &wm8903_dai, 1);
	if (ret < 0)
		kfree(wm8903);
	return ret;
}

static __devexit int wm8903_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	kfree(i2c_get_clientdata(client));
	return 0;
}

static const struct i2c_device_id wm8903_i2c_id[] = {
	{ "wm8903", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8903_i2c_id);

static struct i2c_driver wm8903_i2c_driver = {
	.driver = {
		.name = "wm8903",
		.owner = THIS_MODULE,
	},
	.probe =    wm8903_i2c_probe,
	.remove =   __devexit_p(wm8903_i2c_remove),
	.id_table = wm8903_i2c_id,
};
#endif

static int __init wm8903_modinit(void)
{
	int ret = 0;
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	ret = i2c_add_driver(&wm8903_i2c_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register wm8903 I2C driver: %d\n",
		       ret);
	}
#endif
	return ret;
}
module_init(wm8903_modinit);

static void __exit wm8903_exit(void)
{
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	i2c_del_driver(&wm8903_i2c_driver);
#endif
}
module_exit(wm8903_exit);

MODULE_DESCRIPTION("ASoC WM8903 driver");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.cm>");
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * cs35l45.c - CS35L45 ALSA SoC audio driver
 *
 * Copyright 2019 Cirrus Logic, Inc.
 *
 * Author: James Schulman <james.schulman@cirrus.com>
 *
 */

#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio/consumer.h>
#include <linux/of_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "wm_adsp.h"
#include "cs35l45.h"
#include <sound/cs35l45.h>

#ifdef CONFIG_SND_SOC_CIRRUS_AMP
#include <sound/cirrus/core.h>
#include <sound/cirrus/big_data.h>
#include <sound/cirrus/calibration.h>
#include <sound/cirrus/power.h>
#endif

static struct wm_adsp_ops cs35l45_halo_ops;
static int (*cs35l45_halo_start_core)(struct wm_adsp *dsp);

struct cs35l45_mixer_cache {
	unsigned int reg;
	unsigned int mask;
	unsigned int val;
};

struct cs35l45_fw_entry {
	const char *name;
	int type;
	unsigned int alg;
	void *buf;
	size_t len;
};

static int __cs35l45_initialize(struct cs35l45_private *cs35l45);
static int cs35l45_hibernate(struct cs35l45_private *cs35l45, bool hiber_en);
static int cs35l45_set_sysclk(struct cs35l45_private *cs35l45, int clk_id,
			      unsigned int freq);
static int cs35l45_gpio_configuration(struct cs35l45_private *cs35l45);

static void cs35l45_dsp_pmd_work(struct work_struct *work)
{
	struct cs35l45_private *cs35l45 = container_of(work,
						       struct cs35l45_private,
						       dsp_pmd_work);
	unsigned int pll_sts, pwr_sts, timeout;

	mutex_lock(&cs35l45->dsp_pmd_lock);

	timeout = 50;
	do {
		regmap_read(cs35l45->regmap, CS35L45_IRQ1_STS_1, &pwr_sts);
		regmap_read(cs35l45->regmap, CS35L45_IRQ1_STS_3, &pll_sts);

		pwr_sts &= CS35L45_MSM_GLOBAL_EN_ASSERT_MASK;
		pll_sts &= CS35L45_PLL_LOCK_FLAG_MASK;

		usleep_range(1000, 1100);
		timeout--;
	} while (pwr_sts && pll_sts && timeout);

	if (timeout == 0)
		dev_err(cs35l45->dev, "Timeout for PLL disable conditions\n");
	else
		regmap_update_bits(cs35l45->regmap, CS35L45_REFCLK_INPUT,
				   CS35L45_PLL_FORCE_EN_MASK, 0);

	mutex_unlock(&cs35l45->dsp_pmd_lock);
}

static bool cs35l45_is_csplmboxsts_correct(enum cspl_mboxcmd cmd,
					   enum cspl_mboxstate sts)
{
	switch (cmd) {
	case CSPL_MBOX_CMD_NONE:
	case CSPL_MBOX_CMD_UNKNOWN_CMD:
		return true;
	case CSPL_MBOX_CMD_PAUSE:
		return (sts == CSPL_MBOX_STS_PAUSED);
	case CSPL_MBOX_CMD_RESUME:
		return (sts == CSPL_MBOX_STS_RUNNING);
	case CSPL_MBOX_CMD_REINIT:
		return (sts == CSPL_MBOX_STS_RUNNING);
	case CSPL_MBOX_CMD_STOP_PRE_REINIT:
		return (sts == CSPL_MBOX_STS_RDY_FOR_REINIT);
	case CSPL_MBOX_CMD_HIBERNATE:
		return (sts == CSPL_MBOX_STS_HIBERNATE);
	case CSPL_MBOX_CMD_OUT_OF_HIBERNATE:
		return (sts == CSPL_MBOX_STS_PAUSED);
	case CSPL_MBOX_CMD_PREPARE_RECONFIGURATION:
		return (sts == CSPL_MBOX_STS_RECONFIGURING);
	case CSPL_MBOX_CMD_APPLY_RECONFIGURATION:
		return (sts == CSPL_MBOX_STS_PAUSED);
	default:
		return false;
	}
}

int cs35l45_set_csplmboxcmd(struct cs35l45_private *cs35l45,
			    enum cspl_mboxcmd cmd)
{
	unsigned int sts, i;

	/* Reset DSP sticky bit */
	regmap_write(cs35l45->regmap, CS35L45_IRQ2_EINT_2,
		     CS35L45_DSP_VIRT1_MBOX_MASK);

	/* Reset AP sticky bit */
	regmap_write(cs35l45->regmap, CS35L45_IRQ1_EINT_2,
		     CS35L45_DSP_VIRT2_MBOX_MASK);

	/* Unmask DSP INT */
	regmap_update_bits(cs35l45->regmap, CS35L45_IRQ2_MASK_2,
			   CS35L45_DSP_VIRT1_MBOX_MASK, 0);

	regmap_write(cs35l45->regmap, CS35L45_DSP_VIRT1_MBOX_1, cmd);

	/* Poll for DSP ACK */
	for (i = 0; i < 5; i++) {
		usleep_range(1000, 1100);

		regmap_read(cs35l45->regmap, CS35L45_IRQ1_EINT_2, &sts);
		if (!(sts & CS35L45_DSP_VIRT2_MBOX_MASK))
			continue;

		regmap_write(cs35l45->regmap, CS35L45_IRQ1_EINT_2,
			     CS35L45_DSP_VIRT2_MBOX_MASK);

		break;
	}

	/* Mask DSP INT */
	regmap_update_bits(cs35l45->regmap, CS35L45_IRQ2_MASK_2,
			   CS35L45_DSP_VIRT1_MBOX_MASK,
			   CS35L45_DSP_VIRT1_MBOX_MASK);

	if (i == 5) {
		dev_err(cs35l45->dev, "Timeout waiting for MBOX ACK\n");
		return -ETIMEDOUT;
	}

	regmap_read(cs35l45->regmap, CS35L45_DSP_MBOX_2, &sts);
	if (!cs35l45_is_csplmboxsts_correct(cmd, (enum cspl_mboxstate)sts)) {
		dev_err(cs35l45->dev, "Failed to set MBOX (cmd: %u, sts: %u)\n",
			cmd, sts);
		return -ENOMSG;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(cs35l45_set_csplmboxcmd);

static int cs35l45_dsp_loader_ev(struct snd_soc_dapm_widget *w,
				 struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct cs35l45_private *cs35l45 =
		snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		if (cs35l45->dsp.booted) {
			dev_err(cs35l45->dev, "DSP already booted\n");
			return -EPERM;
		}

		wm_adsp_early_event(w, kcontrol, event);
		break;
	case SND_SOC_DAPM_POST_PMU:
		wm_adsp_event(w, kcontrol, event);
		break;
	default:
		dev_err(cs35l45->dev, "Invalid event = 0x%x\n", event);
		return -EINVAL;
	}

	return 0;
}

static __be32 alg_disable_val[] = {cpu_to_be32(0)};

static const struct cs35l45_fw_entry cs35l45_preload_entries[] = {
	{
		.name = "EFFICIENCY_ENABLE",
		.type = WMFW_ADSP2_XM,
		.alg = CS35L45_ALGID_EFFICIENCY,
		.buf = (void *)alg_disable_val,
		.len = ARRAY_SIZE(alg_disable_val) * sizeof(__be32),
	},
	{
		.name = "CS_ENABLE",
		.type = WMFW_ADSP2_XM,
		.alg = CS35L45_ALGID_CURRENT_SHARING,
		.buf = (void *)alg_disable_val,
		.len = ARRAY_SIZE(alg_disable_val) * sizeof(__be32),
	},
};

static int cs35l45_dsp_boot_ev(struct snd_soc_dapm_widget *w,
			       struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct cs35l45_private *cs35l45 =
		snd_soc_component_get_drvdata(component);
	const struct cs35l45_fw_entry *entry;
	enum cspl_mboxcmd mboxcmd = CSPL_MBOX_CMD_NONE;
	unsigned int sts, i;
	int ret;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		if (!cs35l45->dsp.booted) {
			dev_err(cs35l45->dev, "Preload DSP before boot\n");
			return -EPERM;
		}

		regmap_update_bits(cs35l45->regmap, CS35L45_PWRMGT_CTL,
				   CS35L45_MEM_RDY_MASK,
				   CS35L45_MEM_RDY_MASK);

		regmap_write(cs35l45->regmap, CS35L45_DSP1_CCM_CORE_CONTROL,
			     CS35L45_CCM_PM_REMAP_MASK |
			     CS35L45_CCM_CORE_RESET_MASK);

#ifdef CONFIG_SND_SOC_CIRRUS_AMP
		cirrus_cal_apply(cs35l45->pdata.mfd_suffix);
#endif

		for (i = 0; i < ARRAY_SIZE(cs35l45_preload_entries); i++) {
			entry = &cs35l45_preload_entries[i];
			ret = wm_adsp_write_ctl(&cs35l45->dsp, entry->name,
						entry->type, entry->alg,
						entry->buf, entry->len);
			if (ret < 0)
				dev_err(cs35l45->dev, "Failed to set %s (%d)\n",
					entry->name, ret);
		}

		(*cs35l45_halo_start_core)(&cs35l45->dsp);

		/* Poll for DSP ACK */
		for (i = 0; i < 10; i++) {
			usleep_range(1000, 1100);

			regmap_read(cs35l45->regmap, CS35L45_IRQ1_EINT_2, &sts);
			if (!(sts & CS35L45_DSP_VIRT2_MBOX_MASK))
				continue;

			regmap_write(cs35l45->regmap, CS35L45_IRQ1_EINT_2,
				     CS35L45_DSP_VIRT2_MBOX_MASK);

			break;
		}

		if (i == 10)
			dev_err(cs35l45->dev, "Timeout waiting for MBOX ACK\n");

		mboxcmd = CSPL_MBOX_CMD_PAUSE;
		ret = cs35l45_set_csplmboxcmd(cs35l45, mboxcmd);
		if (ret < 0)
			dev_err(cs35l45->dev, "MBOX failure (%d)\n", ret);

		ret = cs35l45_gpio_configuration(cs35l45);
		if (ret < 0) {
			dev_err(cs35l45->dev,
				"Failed to apply GPIO config (%d)\n", ret);
			return ret;
		}
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_update_bits(cs35l45->regmap,
				   CS35L45_DSP1_STREAM_ARB_TX1_CONFIG_0,
				   CS35L45_DSP1_STREAM_ARB_TX1_EN_MASK, 0);

		regmap_update_bits(cs35l45->regmap,
				   CS35L45_DSP1_STREAM_ARB_MSTR1_CONFIG_0,
				   CS35L45_DSP1_STREAM_ARB_MSTR0_EN_MASK, 0);

		wm_adsp_early_event(w, kcontrol, event);
		wm_adsp_event(w, kcontrol, event);

		regmap_update_bits(cs35l45->regmap, CS35L45_PWRMGT_CTL,
				   CS35L45_MEM_RDY_MASK, 0);
		break;
	default:
		dev_err(cs35l45->dev, "Invalid event = 0x%x\n", event);
		return -EINVAL;
	}

	return 0;
}

static int cs35l45_dsp_power_ev(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct cs35l45_private *cs35l45 =
		snd_soc_component_get_drvdata(component);
	enum cspl_mboxcmd mboxcmd = CSPL_MBOX_CMD_NONE;
	int ret = 0;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		if (!cs35l45->dsp.running) {
			dev_err(cs35l45->dev, "DSP not running\n");
			return -EPERM;
		}

		if (cs35l45->bus_type == CONTROL_BUS_I2C) {
			flush_work(&cs35l45->dsp_pmd_work);

			if (cs35l45->pdata.pll_auto_en)
				regmap_update_bits(cs35l45->regmap,
						   CS35L45_REFCLK_INPUT,
						   CS35L45_PLL_FORCE_EN_MASK,
						   CS35L45_PLL_FORCE_EN_MASK);

			usleep_range(5000, 5100);
		}

		mboxcmd = CSPL_MBOX_CMD_RESUME;
		ret = cs35l45_set_csplmboxcmd(cs35l45, mboxcmd);
		if (ret < 0)
			dev_err(cs35l45->dev, "MBOX failure (%d)\n", ret);
#ifdef CONFIG_SND_SOC_CIRRUS_AMP
		cirrus_pwr_start(cs35l45->pdata.mfd_suffix);
#endif
		break;
	case SND_SOC_DAPM_PRE_PMD:
		if (!cs35l45->dsp.running) {
			dev_err(cs35l45->dev, "DSP not running\n");
			return -EPERM;
		}

		mboxcmd = CSPL_MBOX_CMD_PAUSE;
		ret = cs35l45_set_csplmboxcmd(cs35l45, mboxcmd);
		if (ret < 0)
			dev_err(cs35l45->dev, "MBOX failure (%d)\n", ret);

#ifdef CONFIG_SND_SOC_CIRRUS_AMP
		cirrus_pwr_stop(cs35l45->pdata.mfd_suffix);
#endif

		if (cs35l45->pdata.pll_auto_en &&
		    (cs35l45->bus_type == CONTROL_BUS_I2C))
			queue_work(system_unbound_wq, &cs35l45->dsp_pmd_work);

		break;
	default:
		dev_err(cs35l45->dev, "Invalid event = 0x%x\n", event);
		ret = -EINVAL;
	}

	return ret;
}

static int cs35l45_global_en_ev(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct cs35l45_private *cs35l45 =
		snd_soc_component_get_drvdata(component);
	unsigned int val;
	int ret = 0;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_write(cs35l45->regmap, CS35L45_GLOBAL_ENABLES,
			     CS35L45_GLOBAL_EN_MASK);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_read(cs35l45->regmap, CS35L45_BLOCK_ENABLES, &val);

		val = (val & CS35L45_BST_EN_MASK) >> CS35L45_BST_EN_SHIFT;
		if (val == CS35L45_BST_DISABLE_FET_ON)
			regmap_update_bits(cs35l45->regmap,
					   CS35L45_BLOCK_ENABLES,
					   CS35L45_BST_EN_MASK,
					   CS35L45_BST_DISABLE_FET_OFF <<
					   CS35L45_BST_EN_SHIFT);

		usleep_range(3000, 3100);

		regmap_write(cs35l45->regmap, CS35L45_GLOBAL_ENABLES, 0);
		break;
	default:
		dev_err(cs35l45->dev, "Invalid event = 0x%x\n", event);
		ret = -EINVAL;
	}

	return ret;
}

static const char * const pcm_tx_txt[] = {"Zero", "ASP_RX1", "ASP_RX2", "VMON",
			"IMON", "ERR_VOL", "VDD_BATTMON", "VDD_BSTMON",
			"DSP_TX1", "DSP_TX2"};

static const unsigned int pcm_tx_val[] = {CS35L45_PCM_SRC_ZERO,
			CS35L45_PCM_SRC_ASP_RX1, CS35L45_PCM_SRC_ASP_RX2,
			CS35L45_PCM_SRC_VMON, CS35L45_PCM_SRC_IMON,
			CS35L45_PCM_SRC_ERR_VOL, CS35L45_PCM_SRC_VDD_BATTMON,
			CS35L45_PCM_SRC_VDD_BSTMON, CS35L45_PCM_SRC_DSP_TX1,
			CS35L45_PCM_SRC_DSP_TX2};

static const char * const pcm_rx_txt[] = {"Zero", "ASP_RX1", "ASP_RX2", "VMON",
			"IMON", "ERR_VOL", "CLASSH_TGT", "VDD_BATTMON",
			"VDD_BSTMON", "TEMPMON"};

static const unsigned int pcm_rx_val[] = {CS35L45_PCM_SRC_ZERO,
			CS35L45_PCM_SRC_ASP_RX1, CS35L45_PCM_SRC_ASP_RX2,
			CS35L45_PCM_SRC_VMON, CS35L45_PCM_SRC_IMON,
			CS35L45_PCM_SRC_ERR_VOL, CS35L45_PCM_SRC_CLASSH_TGT,
			CS35L45_PCM_SRC_VDD_BATTMON, CS35L45_PCM_SRC_VDD_BSTMON,
			CS35L45_PCM_SRC_TEMPMON};

static const char * const pcm_dac_txt[] = {"Zero", "ASP_RX1", "ASP_RX2",
			"DSP_TX1", "DSP_TX2"};

static const unsigned int pcm_dac_val[] = {CS35L45_PCM_SRC_ZERO,
			CS35L45_PCM_SRC_ASP_RX1, CS35L45_PCM_SRC_ASP_RX2,
			CS35L45_PCM_SRC_DSP_TX1, CS35L45_PCM_SRC_DSP_TX2};

static const char * const pcm_ng_txt[] = {"Zero", "ASP_RX1", "ASP_RX2"};

static const unsigned int pcm_ng_val[] = {CS35L45_PCM_SRC_ZERO,
			CS35L45_PCM_SRC_ASP_RX1, CS35L45_PCM_SRC_ASP_RX2};

static const struct soc_enum mux_enums[] = {
	SOC_VALUE_ENUM_SINGLE(CS35L45_ASPTX1_INPUT, 0, CS35L45_PCM_SRC_MASK,
			ARRAY_SIZE(pcm_tx_txt), pcm_tx_txt, pcm_tx_val),
	SOC_VALUE_ENUM_SINGLE(CS35L45_ASPTX2_INPUT, 0, CS35L45_PCM_SRC_MASK,
			ARRAY_SIZE(pcm_tx_txt), pcm_tx_txt, pcm_tx_val),
	SOC_VALUE_ENUM_SINGLE(CS35L45_ASPTX3_INPUT, 0, CS35L45_PCM_SRC_MASK,
			 ARRAY_SIZE(pcm_tx_txt), pcm_tx_txt, pcm_tx_val),
	SOC_VALUE_ENUM_SINGLE(CS35L45_ASPTX4_INPUT, 0, CS35L45_PCM_SRC_MASK,
			ARRAY_SIZE(pcm_tx_txt), pcm_tx_txt, pcm_tx_val),
	SOC_VALUE_ENUM_SINGLE(CS35L45_DSP1RX1_INPUT, 0, CS35L45_PCM_SRC_MASK,
			ARRAY_SIZE(pcm_rx_txt), pcm_rx_txt, pcm_rx_val),
	SOC_VALUE_ENUM_SINGLE(CS35L45_DSP1RX2_INPUT, 0, CS35L45_PCM_SRC_MASK,
			ARRAY_SIZE(pcm_rx_txt), pcm_rx_txt, pcm_rx_val),
	SOC_VALUE_ENUM_SINGLE(CS35L45_DSP1RX3_INPUT, 0, CS35L45_PCM_SRC_MASK,
			ARRAY_SIZE(pcm_rx_txt), pcm_rx_txt, pcm_rx_val),
	SOC_VALUE_ENUM_SINGLE(CS35L45_DSP1RX4_INPUT, 0, CS35L45_PCM_SRC_MASK,
			ARRAY_SIZE(pcm_rx_txt), pcm_rx_txt, pcm_rx_val),
	SOC_VALUE_ENUM_SINGLE(CS35L45_DSP1RX5_INPUT, 0, CS35L45_PCM_SRC_MASK,
			ARRAY_SIZE(pcm_rx_txt), pcm_rx_txt, pcm_rx_val),
	SOC_VALUE_ENUM_SINGLE(CS35L45_DSP1RX6_INPUT, 0, CS35L45_PCM_SRC_MASK,
			ARRAY_SIZE(pcm_rx_txt), pcm_rx_txt, pcm_rx_val),
	SOC_VALUE_ENUM_SINGLE(CS35L45_DSP1RX7_INPUT, 0, CS35L45_PCM_SRC_MASK,
			ARRAY_SIZE(pcm_rx_txt), pcm_rx_txt, pcm_rx_val),
	SOC_VALUE_ENUM_SINGLE(CS35L45_DSP1RX8_INPUT, 0, CS35L45_PCM_SRC_MASK,
			ARRAY_SIZE(pcm_rx_txt), pcm_rx_txt, pcm_rx_val),
	SOC_VALUE_ENUM_SINGLE(CS35L45_DACPCM1_INPUT, 0, CS35L45_PCM_SRC_MASK,
			ARRAY_SIZE(pcm_dac_txt), pcm_dac_txt, pcm_dac_val),
	SOC_VALUE_ENUM_SINGLE(CS35L45_NGATE1_INPUT, 0, CS35L45_PCM_SRC_MASK,
			ARRAY_SIZE(pcm_ng_txt), pcm_ng_txt, pcm_ng_val),
	SOC_VALUE_ENUM_SINGLE(CS35L45_NGATE2_INPUT, 0, CS35L45_PCM_SRC_MASK,
			ARRAY_SIZE(pcm_ng_txt), pcm_ng_txt, pcm_ng_val),
};

static const struct snd_kcontrol_new muxes[] = {
	SOC_DAPM_ENUM("ASP_TX1 Source", mux_enums[ASP_TX1]),
	SOC_DAPM_ENUM("ASP_TX2 Source", mux_enums[ASP_TX2]),
	SOC_DAPM_ENUM("ASP_TX3 Source", mux_enums[ASP_TX3]),
	SOC_DAPM_ENUM("ASP_TX4 Source", mux_enums[ASP_TX4]),
};

static const struct snd_kcontrol_new amp_en_ctl =
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0);

static const struct snd_kcontrol_new bbpe_en_ctl =
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0);

static const struct snd_kcontrol_new ngate_en_ctl =
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0);

static const struct snd_kcontrol_new nfr_en_ctl =
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0);

static const struct snd_soc_dapm_widget cs35l45_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("DSP1 Preload", NULL),
	SND_SOC_DAPM_SPK("DSP1 Enable", NULL),

	SND_SOC_DAPM_SUPPLY_S("DSP1 Preloader", 100, SND_SOC_NOPM, 0, 0,
			      cs35l45_dsp_loader_ev, SND_SOC_DAPM_PRE_PMU |
			      SND_SOC_DAPM_POST_PMU),

	SND_SOC_DAPM_SUPPLY_S("DSP1 Boot", 200, SND_SOC_NOPM, 0, 0,
			      cs35l45_dsp_boot_ev, SND_SOC_DAPM_POST_PMU |
			      SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_PGA_S("DSP1 Secondary", 100, SND_SOC_NOPM, 0, 0,
			   cs35l45_dsp_power_ev, SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_PGA_S("DSP1 Primary", 200, SND_SOC_NOPM, 0, 0,
			   cs35l45_dsp_power_ev, SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_PGA_E("GLOBAL_EN", SND_SOC_NOPM, 0, 0, NULL, 0,
			   cs35l45_global_en_ev, SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_SUPPLY("VMON", CS35L45_BLOCK_ENABLES, 12, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("IMON", CS35L45_BLOCK_ENABLES, 13, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("BATTMON", CS35L45_BLOCK_ENABLES, 8, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("BSTMON", CS35L45_BLOCK_ENABLES, 9, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("RCV_EN", CS35L45_BLOCK_ENABLES, 2, 0, NULL, 0),

	SND_SOC_DAPM_AIF_IN("ASP", NULL, 0, CS35L45_BLOCK_ENABLES2, 27, 0),
	SND_SOC_DAPM_AIF_IN("ASP_RX1", NULL, 0, CS35L45_ASP_ENABLES1, 16, 0),
	SND_SOC_DAPM_AIF_IN("ASP_RX2", NULL, 0, CS35L45_ASP_ENABLES1, 17, 0),
	SND_SOC_DAPM_AIF_IN("NGATE_CH1", NULL, 0, CS35L45_MIXER_NGATE_CH1_CFG,
			    16, 0),
	SND_SOC_DAPM_AIF_IN("NGATE_CH2", NULL, 0, CS35L45_MIXER_NGATE_CH2_CFG,
			    16, 0),

	SND_SOC_DAPM_AIF_OUT("ASP_TX1", NULL, 0, CS35L45_ASP_ENABLES1, 0, 1),
	SND_SOC_DAPM_AIF_OUT("ASP_TX2", NULL, 0, CS35L45_ASP_ENABLES1, 1, 1),
	SND_SOC_DAPM_AIF_OUT("ASP_TX3", NULL, 0, CS35L45_ASP_ENABLES1, 2, 1),
	SND_SOC_DAPM_AIF_OUT("ASP_TX4", NULL, 0, CS35L45_ASP_ENABLES1, 3, 1),

	SND_SOC_DAPM_MUX("ASP_TX1 Source", SND_SOC_NOPM, 0, 0, &muxes[ASP_TX1]),
	SND_SOC_DAPM_MUX("ASP_TX2 Source", SND_SOC_NOPM, 0, 0, &muxes[ASP_TX2]),
	SND_SOC_DAPM_MUX("ASP_TX3 Source", SND_SOC_NOPM, 0, 0, &muxes[ASP_TX3]),
	SND_SOC_DAPM_MUX("ASP_TX4 Source", SND_SOC_NOPM, 0, 0, &muxes[ASP_TX4]),

	SND_SOC_DAPM_SWITCH("AMP Enable", SND_SOC_NOPM, 0, 0, &amp_en_ctl),
	SND_SOC_DAPM_SWITCH("BBPE Enable", CS35L45_BLOCK_ENABLES2, 13, 0,
			    &bbpe_en_ctl),
	SND_SOC_DAPM_SWITCH("NFR Enable", CS35L45_BLOCK_ENABLES, 1, 0,
			    &nfr_en_ctl),
	SND_SOC_DAPM_SWITCH("NGATE Enable", SND_SOC_NOPM, 0, 0, &ngate_en_ctl),

	SND_SOC_DAPM_MIXER("Exit", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("Entry", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_OUTPUT("SPK"),
	SND_SOC_DAPM_OUTPUT("RCV"),
	SND_SOC_DAPM_INPUT("AP"),
};

static const struct snd_soc_dapm_route cs35l45_dapm_routes[] = {
	/* DSP */
	{"DSP1 Preload", NULL, "DSP1 Preloader"},
	{"DSP1 Enable", NULL, "DSP1 Boot"},

	{"DSP1 Secondary", NULL, "DSP1 Preloader"},
	{"DSP1 Secondary", NULL, "DSP1 Boot"},
	{"DSP1 Secondary", NULL, "VMON"},
	{"DSP1 Secondary", NULL, "IMON"},
	{"DSP1 Secondary", NULL, "BATTMON"},
	{"DSP1 Secondary", NULL, "BSTMON"},

	{"DSP1 Primary", NULL, "DSP1 Preloader"},
	{"DSP1 Primary", NULL, "DSP1 Boot"},
	{"DSP1 Primary", NULL, "VMON"},
	{"DSP1 Primary", NULL, "IMON"},
	{"DSP1 Primary", NULL, "BATTMON"},
	{"DSP1 Primary", NULL, "BSTMON"},

	/* Feedback */
	{"ASP_TX1", NULL, "AP"},
	{"ASP_TX2", NULL, "AP"},
	{"ASP_TX3", NULL, "AP"},
	{"ASP_TX4", NULL, "AP"},

	{"ASP_TX1 Source", "Zero", "ASP_TX1"},
	{"ASP_TX2 Source", "Zero", "ASP_TX2"},
	{"ASP_TX3 Source", "Zero", "ASP_TX3"},
	{"ASP_TX4 Source", "Zero", "ASP_TX4"},

	{"Capture", NULL, "ASP_TX1 Source"},
	{"Capture", NULL, "ASP_TX2 Source"},
	{"Capture", NULL, "ASP_TX3 Source"},
	{"Capture", NULL, "ASP_TX4 Source"},

	{"Capture", NULL, "VMON"},
	{"Capture", NULL, "IMON"},
	{"Capture", NULL, "BATTMON"},
	{"Capture", NULL, "BSTMON"},

	/* Playback */
	{"Entry", NULL, "Playback"},

	{"AMP Enable", "Switch", "Entry"},

	{"ASP_RX1", NULL, "AMP Enable"},
	{"ASP_RX2", NULL, "AMP Enable"},

	{"ASP", NULL, "ASP_RX1"},
	{"ASP", NULL, "ASP_RX2"},

	{"BBPE Enable", "Switch", "AMP Enable"},
	{"NFR Enable", "Switch", "AMP Enable"},

	{"NGATE_CH1", NULL, "AMP Enable"},
	{"NGATE_CH2", NULL, "AMP Enable"},

	{"NGATE Enable", "Switch", "NGATE_CH1"},
	{"NGATE Enable", "Switch", "NGATE_CH2"},

	{"Exit", NULL, "ASP"},
	{"Exit", NULL, "BBPE Enable"},
	{"Exit", NULL, "NFR Enable"},
	{"Exit", NULL, "NGATE Enable"},

	{"RCV", NULL, "RCV_EN"},
	{"RCV", NULL, "Exit"},

	{"SPK", NULL, "Exit"},
};

static const struct snd_soc_dapm_route cs35l45_asp_routes[] = {
	{"GLOBAL_EN", NULL, "Entry"},
	{"Exit", NULL, "GLOBAL_EN"},
};

static const struct snd_soc_dapm_route cs35l45_dsp_secondary_routes[] = {
	{"DSP1 Secondary", NULL, "Entry"},
	{"Exit", NULL, "DSP1 Secondary"},
};

static const struct snd_soc_dapm_route cs35l45_dsp_primary_routes[] = {
	{"DSP1 Primary", NULL, "Entry"},
	{"Exit", NULL, "DSP1 Primary"},
};

static int cs35l45_set_dapm_route_mode(struct cs35l45_private *cs35l45,
				       enum dapm_route_mode dapm_mode)
{
	struct snd_soc_component *component =
			snd_soc_lookup_component(cs35l45->dev, NULL);
	struct snd_soc_dapm_context *dapm =
			snd_soc_component_get_dapm(component);

	if (cs35l45->dapm_mode == dapm_mode)
		return 0;

	switch (cs35l45->dapm_mode) {
	case DAPM_MODE_ASP:
		snd_soc_dapm_del_routes(dapm, cs35l45_asp_routes,
				ARRAY_SIZE(cs35l45_asp_routes));
		break;
	case DAPM_MODE_DSP_SECONDARY:
		snd_soc_dapm_del_routes(dapm, cs35l45_dsp_secondary_routes,
				ARRAY_SIZE(cs35l45_dsp_secondary_routes));
		break;
	case DAPM_MODE_DSP_PRIMARY:
		snd_soc_dapm_del_routes(dapm, cs35l45_dsp_primary_routes,
				ARRAY_SIZE(cs35l45_dsp_primary_routes));
		break;
	}

	switch (dapm_mode) {
	case DAPM_MODE_ASP:
		snd_soc_dapm_add_routes(dapm, cs35l45_asp_routes,
				ARRAY_SIZE(cs35l45_asp_routes));
		break;
	case DAPM_MODE_DSP_SECONDARY:
		snd_soc_dapm_add_routes(dapm, cs35l45_dsp_secondary_routes,
				ARRAY_SIZE(cs35l45_dsp_secondary_routes));
		break;
	case DAPM_MODE_DSP_PRIMARY:
		snd_soc_dapm_add_routes(dapm, cs35l45_dsp_primary_routes,
				ARRAY_SIZE(cs35l45_dsp_primary_routes));
		break;
	default:
		dev_err(cs35l45->dev, "Invalid DAPM route mode (%d)\n",
			dapm_mode);
		return -EINVAL;
	}

	cs35l45->dapm_mode = dapm_mode;

	return 0;
}

static const char * const gain_texts[] = {"10dB", "13dB", "16dB", "19dB"};
static const unsigned int gain_values[] = {0x00, 0x01, 0x02, 0x03};
static SOC_VALUE_ENUM_SINGLE_DECL(gain_enum, CS35L45_AMP_GAIN,
			CS35L45_AMP_GAIN_PCM_SHIFT,
			CS35L45_AMP_GAIN_PCM_MASK >> CS35L45_AMP_GAIN_PCM_SHIFT,
			gain_texts, gain_values);

static const char * const amplifier_mode_texts[] = {"SPK", "RCV"};
static SOC_ENUM_SINGLE_DECL(amplifier_mode_enum, SND_SOC_NOPM, 0,
			    amplifier_mode_texts);

static const char * const hibernate_mode_texts[] = {"Off", "On"};
static SOC_ENUM_SINGLE_DECL(hibernate_mode_enum, SND_SOC_NOPM, 0,
			    hibernate_mode_texts);

static const DECLARE_TLV_DB_RANGE(dig_pcm_vol_tlv, 0, 0,
				  TLV_DB_SCALE_ITEM(TLV_DB_GAIN_MUTE, 0, 1),
				  1, 913, TLV_DB_SCALE_ITEM(-10200, 25, 0));

static int cs35l45_activate_ctl(struct cs35l45_private *cs35l45,
				const char *ctl_name, bool active)
{
	struct snd_soc_component *component =
			snd_soc_lookup_component(cs35l45->dev, NULL);
	struct snd_card *card = component->card->snd_card;
	struct snd_kcontrol *kcontrol;
	struct snd_kcontrol_volatile *vd;
	unsigned int index_offset;
	char name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];

	if (component->name_prefix)
		snprintf(name, SNDRV_CTL_ELEM_ID_NAME_MAXLEN, "%s %s",
			 component->name_prefix, ctl_name);
	else
		snprintf(name, SNDRV_CTL_ELEM_ID_NAME_MAXLEN, "%s", ctl_name);

	kcontrol = snd_soc_card_get_kcontrol(component->card, name);
	if (!kcontrol) {
		dev_err(cs35l45->dev, "Can't find kcontrol %s\n", name);
		return -EINVAL;
	}

	index_offset = snd_ctl_get_ioff(kcontrol, &kcontrol->id);
	vd = &kcontrol->vd[index_offset];
	if (active)
		vd->access |= SNDRV_CTL_ELEM_ACCESS_WRITE;
	else
		vd->access &= ~SNDRV_CTL_ELEM_ACCESS_WRITE;

	snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_INFO, &kcontrol->id);

	return 0;
}

static int cs35l45_amplifier_mode_get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
			snd_soc_kcontrol_component(kcontrol);
	struct cs35l45_private *cs35l45 =
			snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = cs35l45->amplifier_mode;

	return 0;
}

static int cs35l45_amplifier_mode_put(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
			snd_soc_kcontrol_component(kcontrol);
	struct cs35l45_private *cs35l45 =
			snd_soc_component_get_drvdata(component);
	struct snd_soc_dapm_context *dapm =
			snd_soc_component_get_dapm(component);
	unsigned int val;

	if (ucontrol->value.integer.value[0] == cs35l45->amplifier_mode)
		return 0;

	regmap_read(cs35l45->regmap, CS35L45_IRQ1_STS_1, &val);
	if (val & CS35L45_MSM_GLOBAL_EN_ASSERT_MASK) {
		dev_err(cs35l45->dev, "Only switch mode while powered down\n");
		return -EINVAL;
	}

	cs35l45->amplifier_mode = ucontrol->value.integer.value[0];

	if (cs35l45->amplifier_mode == AMP_MODE_SPK) {
		snd_soc_component_enable_pin(component, "SPK");
		snd_soc_component_disable_pin(component, "RCV");

		regmap_update_bits(cs35l45->regmap, CS35L45_BLOCK_ENABLES,
				   CS35L45_BST_EN_MASK,
				   CS35L45_BST_ENABLE << CS35L45_BST_EN_SHIFT);

		regmap_update_bits(cs35l45->regmap, CS35L45_HVLV_CONFIG,
				   CS35L45_HVLV_MODE_MASK,
				   CS35L45_HVLV_OPERATION <<
				   CS35L45_HVLV_MODE_SHIFT);
	} else  /* AMP_MODE_RCV */ {
		snd_soc_component_enable_pin(component, "RCV");
		snd_soc_component_disable_pin(component, "SPK");

		regmap_update_bits(cs35l45->regmap, CS35L45_BLOCK_ENABLES,
				   CS35L45_BST_EN_MASK,
				   CS35L45_BST_DISABLE_FET_OFF <<
				   CS35L45_BST_EN_SHIFT);

		regmap_update_bits(cs35l45->regmap, CS35L45_HVLV_CONFIG,
				   CS35L45_HVLV_MODE_MASK,
				   CS35L45_FORCE_LV_OPERATION <<
				   CS35L45_HVLV_MODE_SHIFT);

		regmap_update_bits(cs35l45->regmap,
				   CS35L45_BLOCK_ENABLES2,
				   CS35L45_AMP_DRE_EN_MASK, 0);

		regmap_update_bits(cs35l45->regmap, CS35L45_AMP_GAIN,
				   CS35L45_AMP_GAIN_PCM_MASK,
				   CS35L45_AMP_GAIN_PCM_13DBV <<
				   CS35L45_AMP_GAIN_PCM_SHIFT);
	}

	snd_soc_dapm_sync(dapm);

	return 0;
}

static int cs35l45_hibernate_mode_get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
			snd_soc_kcontrol_component(kcontrol);
	struct cs35l45_private *cs35l45 =
			snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = cs35l45->hibernate_mode;

	return 0;
}

static int cs35l45_hibernate_mode_put(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
			snd_soc_kcontrol_component(kcontrol);
	struct cs35l45_private *cs35l45 =
			snd_soc_component_get_drvdata(component);
	int ret;

	ret = cs35l45_hibernate(cs35l45, ucontrol->value.integer.value[0]);
	if (ret < 0)
		dev_err(cs35l45->dev, "Set hibernate mode failed (%d)\n", ret);

	return 0;
}

static int cs35l45_dsp_boot_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
			snd_soc_kcontrol_component(kcontrol);
	unsigned int val;

	val = snd_soc_component_get_pin_status(component, "DSP1 Enable");

	ucontrol->value.integer.value[0] = (val > 0) ? 1 : 0;

	return 0;
}

static int cs35l45_dsp_boot_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
			snd_soc_kcontrol_component(kcontrol);
	struct cs35l45_private *cs35l45 =
			snd_soc_component_get_drvdata(component);
	struct snd_soc_dapm_context *dapm =
			snd_soc_component_get_dapm(component);

	if (!cs35l45->dsp.booted) {
		dev_err(cs35l45->dev, "Preload DSP before boot\n");
		return -EPERM;
	}

	if (ucontrol->value.integer.value[0]) {
		snd_soc_component_force_enable_pin(component, "DSP1 Enable");
		cs35l45_set_dapm_route_mode(cs35l45, DAPM_MODE_DSP_PRIMARY);

		snd_soc_dapm_sync(dapm);

		regmap_update_bits(cs35l45->regmap, CS35L45_BLOCK_ENABLES2,
				   CS35L45_SYNC_EN_MASK, CS35L45_SYNC_EN_MASK);

		regmap_update_bits(cs35l45->regmap, CS35L45_SYNC_TX_RX_ENABLES,
				   CS35L45_SYNC_SW_EN_MASK,
				   CS35L45_SYNC_SW_EN_MASK);
	} else {
		snd_soc_component_disable_pin(component, "DSP1 Enable");
		cs35l45_set_dapm_route_mode(cs35l45, DAPM_MODE_ASP);

		snd_soc_dapm_sync(dapm);

		regmap_update_bits(cs35l45->regmap, CS35L45_BLOCK_ENABLES2,
				   CS35L45_SYNC_EN_MASK, 0);

		regmap_update_bits(cs35l45->regmap, CS35L45_SYNC_TX_RX_ENABLES,
				   CS35L45_SYNC_SW_EN_MASK, 0);
	}

	return 0;
}

static int cs35l45_sync_num_devices_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
			snd_soc_kcontrol_component(kcontrol);
	struct cs35l45_private *cs35l45 =
			snd_soc_component_get_drvdata(component);
	__be32 buf;
	int ret;

	if (!cs35l45->dsp.running) {
		ucontrol->value.integer.value[0] = 0;
		return 0;
	}

	ret = wm_adsp_read_ctl(&cs35l45->dsp, "NUM_DEVICES", WMFW_ADSP2_XM,
			       CS35L45_ALGID_MDSYNC, &buf, sizeof(__be32));
	if (ret < 0) {
		dev_err(cs35l45->dev, "Control read error (%d)\n", ret);
		return ret;
	}

	ucontrol->value.integer.value[0] = be32_to_cpu(buf);

	return 0;
}

static int cs35l45_sync_num_devices_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
			snd_soc_kcontrol_component(kcontrol);
	struct cs35l45_private *cs35l45 =
			snd_soc_component_get_drvdata(component);
	__be32 buf;
	int ret;

	if (!cs35l45->dsp.running) {
		dev_err(cs35l45->dev, "DSP not running\n");
		return -EPERM;
	}

	buf = cpu_to_be32(ucontrol->value.integer.value[0]);
	ret = wm_adsp_write_ctl(&cs35l45->dsp, "NUM_DEVICES", WMFW_ADSP2_XM,
				CS35L45_ALGID_MDSYNC, &buf, sizeof(__be32));
	if (ret < 0) {
		dev_err(cs35l45->dev, "Control write error (%d)\n", ret);
		return ret;
	}

	return 0;
}

static int cs35l45_sync_id_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
			snd_soc_kcontrol_component(kcontrol);
	struct cs35l45_private *cs35l45 =
			snd_soc_component_get_drvdata(component);
	__be32 buf;
	int ret;

	if (!cs35l45->dsp.running) {
		ucontrol->value.integer.value[0] = 0;
		return 0;
	}

	ret = wm_adsp_read_ctl(&cs35l45->dsp, "ID", WMFW_ADSP2_XM,
			       CS35L45_ALGID_MDSYNC, &buf, sizeof(__be32));
	if (ret < 0) {
		dev_err(cs35l45->dev, "Control read error (%d)\n", ret);
		return ret;
	}

	ucontrol->value.integer.value[0] = be32_to_cpu(buf);

	return 0;
}

static int cs35l45_sync_id_put(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
			snd_soc_kcontrol_component(kcontrol);
	struct cs35l45_private *cs35l45 =
			snd_soc_component_get_drvdata(component);
	__be32 buf;
	int ret;

	if (!cs35l45->dsp.running) {
		dev_err(cs35l45->dev, "DSP not running\n");
		return -EPERM;
	}

	buf = cpu_to_be32(ucontrol->value.integer.value[0]);
	ret = wm_adsp_write_ctl(&cs35l45->dsp, "ID", WMFW_ADSP2_XM,
				CS35L45_ALGID_MDSYNC, &buf, sizeof(__be32));
	if (ret < 0) {
		dev_err(cs35l45->dev, "Control write error (%d)\n", ret);
		return ret;
	}

	return 0;
}

static int cs35l45_dsp_prepare_reconfig_get(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = 0;

	return 0;
}

static int cs35l45_dsp_prepare_reconfig_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
			snd_soc_kcontrol_component(kcontrol);
	struct cs35l45_private *cs35l45 =
			snd_soc_component_get_drvdata(component);

	if (!cs35l45->dsp.running) {
		dev_err(cs35l45->dev, "DSP not running\n");
		return -EPERM;
	}

	if (!ucontrol->value.integer.value[0])
		return 0;

	regmap_write(cs35l45->regmap, CS35L45_DSP_VIRT1_MBOX_1,
		     CSPL_MBOX_CMD_PREPARE_RECONFIGURATION);

	usleep_range(5000, 5100);

	return 0;
}

static int cs35l45_dsp_apply_reconfig_get(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = 0;

	return 0;
}

static int cs35l45_dsp_apply_reconfig_put(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	static const struct reg_sequence cs35l45_sync_pwr_en_patch[] = {
		{0x00000040, 0x00000055},
		{0x00000040, 0x000000AA},
		{0x00000044, 0x00000055},
		{0x00000044, 0x000000AA},
		{0x00002114, 0x00040000},
		{0x0000225C, 0x0002000A},
		{0x00000040, 0x00000000},
		{0x00000044, 0x00000000},
	};
	static const struct reg_sequence cs35l45_sync_pwr_dis_patch[] = {
		{0x00000040, 0x00000055},
		{0x00000040, 0x000000AA},
		{0x00000044, 0x00000055},
		{0x00000044, 0x000000AA},
		{0x00002114, 0x00000000},
		{0x0000225C, 0x00000000},
		{0x00000040, 0x00000000},
		{0x00000044, 0x00000000},
	};
	struct snd_soc_component *component =
			snd_soc_kcontrol_component(kcontrol);
	struct cs35l45_private *cs35l45 =
			snd_soc_component_get_drvdata(component);
	unsigned int is_primary, num_devices, mask;
	__be32 buf;
	int ret = 0;

	if (!cs35l45->dsp.running) {
		dev_err(cs35l45->dev, "DSP not running\n");
		return -EPERM;
	}

	if (!ucontrol->value.integer.value[0])
		return 0;

	ret = wm_adsp_read_ctl(&cs35l45->dsp, "MASTER", WMFW_ADSP2_XM,
			       CS35L45_ALGID_MDSYNC, &buf, sizeof(__be32));
	if (ret < 0) {
		dev_err(cs35l45->dev, "Control read error (%d)\n", ret);
		return ret;
	}

	is_primary = be32_to_cpu(buf);

	ret = wm_adsp_read_ctl(&cs35l45->dsp, "NUM_DEVICES", WMFW_ADSP2_XM,
			       CS35L45_ALGID_MDSYNC, &buf, sizeof(__be32));
	if (ret < 0) {
		dev_err(cs35l45->dev, "Control read error (%d)\n", ret);
		return ret;
	}

	num_devices = be32_to_cpu(buf);

	if (is_primary && (num_devices > 1))
		mask = CS35L45_SYNC_PWR_TX_EN_MASK |
		       CS35L45_SYNC_PWR_RX_EN_MASK;
	else if ((num_devices > 1))
		mask = CS35L45_SYNC_PWR_RX_EN_MASK;
	else
		mask = 0;

	regmap_update_bits(cs35l45->regmap, CS35L45_SYNC_TX_RX_ENABLES,
			   CS35L45_SYNC_PWR_TX_EN_MASK |
			   CS35L45_SYNC_PWR_RX_EN_MASK, mask);

	if (mask)
		regmap_multi_reg_write(cs35l45->regmap,
				       cs35l45_sync_pwr_en_patch,
				       ARRAY_SIZE(cs35l45_sync_pwr_en_patch));
	else
		regmap_multi_reg_write(cs35l45->regmap,
				       cs35l45_sync_pwr_dis_patch,
				       ARRAY_SIZE(cs35l45_sync_pwr_dis_patch));

	if (is_primary)
		cs35l45_set_dapm_route_mode(cs35l45, DAPM_MODE_DSP_PRIMARY);
	else
		cs35l45_set_dapm_route_mode(cs35l45, DAPM_MODE_DSP_SECONDARY);

	regmap_write(cs35l45->regmap, CS35L45_DSP_VIRT1_MBOX_1,
		     CSPL_MBOX_CMD_APPLY_RECONFIGURATION);

	usleep_range(5000, 5100);

	return 0;
}

static const struct snd_kcontrol_new cs35l45_aud_controls[] = {
#ifndef CONFIG_SND_SOC_CIRRUS_AMP
	WM_ADSP_FW_CONTROL("DSP1", 0),
#endif
	WM_ADSP2_PRELOAD_SWITCH("DSP1", 1),

	SOC_SINGLE("AMP Mute", CS35L45_AMP_OUTPUT_MUTE, 0, 1, 0),
	SOC_SINGLE("SYNC Enable Switch", CS35L45_BLOCK_ENABLES2, 8, 1, 0),
	SOC_SINGLE("PLL Force Enable Switch", CS35L45_REFCLK_INPUT, 16, 1, 0),
	SOC_SINGLE_EXT("DSP1 Boot Switch", SND_SOC_NOPM, 1, 1, 0,
		       cs35l45_dsp_boot_get, cs35l45_dsp_boot_put),
	SOC_SINGLE_EXT("DSP1 SYNC NUM DEVICES", SND_SOC_NOPM, 1, 8, 0,
		       cs35l45_sync_num_devices_get,
		       cs35l45_sync_num_devices_put),
	SOC_SINGLE_EXT("DSP1 SYNC ID", SND_SOC_NOPM, 1, 7, 0,
		       cs35l45_sync_id_get, cs35l45_sync_id_put),
	SOC_SINGLE_EXT("DSP1 Prepare Reconfiguration", SND_SOC_NOPM, 1, 1, 0,
		       cs35l45_dsp_prepare_reconfig_get,
		       cs35l45_dsp_prepare_reconfig_put),
	SOC_SINGLE_EXT("DSP1 Apply Reconfiguration", SND_SOC_NOPM, 1, 1, 0,
		       cs35l45_dsp_apply_reconfig_get,
		       cs35l45_dsp_apply_reconfig_put),
	SOC_SINGLE_RANGE("ASPTX1 Slot Position", CS35L45_ASP_FRAME_CONTROL1, 0,
			 0, 63, 0),
	SOC_SINGLE_RANGE("ASPTX2 Slot Position", CS35L45_ASP_FRAME_CONTROL1, 8,
			 0, 63, 0),
	SOC_SINGLE_RANGE("ASPTX3 Slot Position", CS35L45_ASP_FRAME_CONTROL1, 16,
			 0, 63, 0),
	SOC_SINGLE_RANGE("ASPTX4 Slot Position", CS35L45_ASP_FRAME_CONTROL1, 24,
			 0, 63, 0),
	SOC_SINGLE_RANGE("ASPRX1 Slot Position", CS35L45_ASP_FRAME_CONTROL5, 0,
			 0, 63, 0),
	SOC_SINGLE_RANGE("ASPRX2 Slot Position", CS35L45_ASP_FRAME_CONTROL5, 8,
			 0, 63, 0),

	SOC_ENUM("DSP_RX1 Source", mux_enums[DSP_RX1]),
	SOC_ENUM("DSP_RX2 Source", mux_enums[DSP_RX2]),
	SOC_ENUM("DSP_RX3 Source", mux_enums[DSP_RX3]),
	SOC_ENUM("DSP_RX4 Source", mux_enums[DSP_RX4]),
	SOC_ENUM("DSP_RX5 Source", mux_enums[DSP_RX5]),
	SOC_ENUM("DSP_RX6 Source", mux_enums[DSP_RX6]),
	SOC_ENUM("DSP_RX7 Source", mux_enums[DSP_RX7]),
	SOC_ENUM("DSP_RX8 Source", mux_enums[DSP_RX8]),
	SOC_ENUM("DACPCM Source", mux_enums[DACPCM]),
	SOC_ENUM("NGATE1 Source", mux_enums[NGATE1]),
	SOC_ENUM("NGATE2 Source", mux_enums[NGATE2]),
	SOC_ENUM("AMP PCM Gain", gain_enum),

	SOC_ENUM_EXT("Amplifier Mode", amplifier_mode_enum,
		     cs35l45_amplifier_mode_get, cs35l45_amplifier_mode_put),
	SOC_ENUM_EXT("Hibernate Mode", hibernate_mode_enum,
		     cs35l45_hibernate_mode_get, cs35l45_hibernate_mode_put),

	{
		.name = "Digital PCM Volume",
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ |
			  SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.tlv.p  = dig_pcm_vol_tlv,
		.info = snd_soc_info_volsw_sx,
		.get = snd_soc_get_volsw_sx,
		.put = snd_soc_put_volsw_sx,
		.private_value = (unsigned long)&(struct soc_mixer_control)
			{
				 .reg = CS35L45_AMP_PCM_CONTROL,
				 .rreg = CS35L45_AMP_PCM_CONTROL,
				 .shift = 0, .rshift = 0,
				 .max = 0x391, .min = CS35L45_AMP_VOL_PCM_MUTE
			}
	},
};

static int cs35l45_dai_set_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct cs35l45_private *cs35l45 =
			snd_soc_component_get_drvdata(codec_dai->component);
	unsigned int asp_fmt, fsync_inv, bclk_inv, clock_mode;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		clock_mode = 1;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		clock_mode = 0;
		break;
	default:
		dev_warn(cs35l45->dev, "Mixed clock mode unsupported (%d)\n",
			 fmt & SND_SOC_DAIFMT_MASTER_MASK);
		return -EINVAL;
	}

	regmap_update_bits(cs35l45->regmap, CS35L45_ASP_CONTROL2,
			   CS35L45_ASP_BCLK_MSTR_MASK,
			   clock_mode << CS35L45_ASP_BCLK_MSTR_SHIFT);

	regmap_update_bits(cs35l45->regmap, CS35L45_ASP_CONTROL2,
			   CS35L45_ASP_FSYNC_MSTR_MASK,
			   clock_mode << CS35L45_ASP_FSYNC_MSTR_SHIFT);

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		asp_fmt = 0;
		break;
	case SND_SOC_DAIFMT_I2S:
		asp_fmt = 2;
		break;
	default:
		dev_warn(cs35l45->dev, "Unsupported DAI format (%d)\n",
			 fmt & SND_SOC_DAIFMT_FORMAT_MASK);
		return -EINVAL;
	}

	regmap_update_bits(cs35l45->regmap, CS35L45_ASP_CONTROL2,
			   CS35L45_ASP_FMT_MASK,
			   asp_fmt << CS35L45_ASP_FMT_SHIFT);

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_IF:
		fsync_inv = 1;
		bclk_inv = 0;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		fsync_inv = 0;
		bclk_inv = 1;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		fsync_inv = 1;
		bclk_inv = 1;
		break;
	case SND_SOC_DAIFMT_NB_NF:
		fsync_inv = 0;
		bclk_inv = 0;
		break;
	default:
		dev_warn(cs35l45->dev, "Invalid clock polarity (%d)\n",
			 fmt & SND_SOC_DAIFMT_INV_MASK);
		return -EINVAL;
	}

	regmap_update_bits(cs35l45->regmap, CS35L45_ASP_CONTROL2,
			   CS35L45_ASP_FSYNC_INV_MASK,
			   fsync_inv << CS35L45_ASP_FSYNC_INV_SHIFT);

	regmap_update_bits(cs35l45->regmap, CS35L45_ASP_CONTROL2,
			   CS35L45_ASP_BCLK_INV_MASK,
			   bclk_inv << CS35L45_ASP_BCLK_INV_SHIFT);

	return 0;
}

static int cs35l45_dai_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct cs35l45_private *cs35l45 =
			snd_soc_component_get_drvdata(dai->component);
	unsigned int asp_width, asp_wl, global_fs;
	unsigned int hpf_override = CS35l45_HPF_DEFAULT;
	static const struct reg_sequence cs35l45_unlock[] = {
		{0x00000040, 0x00000055},
		{0x00000040, 0x000000AA},
		{0x00000044, 0x00000055},
		{0x00000044, 0x000000AA},
	};
	static const struct reg_sequence cs35l45_lock[] = {
		{0x00000040, 0x00000000},
		{0x00000044, 0x00000000},
	};

	switch (params_rate(params)) {
	case 8000:
		global_fs = CS35L45_8_KHZ;
		break;
	case 16000:
		global_fs = CS35L45_16_KHZ;
		break;
	case 44100:
		hpf_override = CS35L45_HPF_44P1;
		global_fs = CS35L45_44P100_KHZ;
		break;
	case 48000:
		global_fs = CS35L45_48P0_KHZ;
		break;
	case 88200:
		hpf_override = CS35L45_HPF_88P2;
		global_fs = CS35L45_88P200_KHZ;
		break;
	case 96000:
		global_fs = CS35L45_96P0_KHZ;
		break;
	default:
		dev_warn(cs35l45->dev, "Unsupported params rate (%d)\n",
			 params_rate(params));
		return -EINVAL;
	}

	regmap_update_bits(cs35l45->regmap, CS35L45_GLOBAL_SAMPLE_RATE,
			   CS35L45_GLOBAL_FS_MASK,
			   global_fs << CS35L45_GLOBAL_FS_SHIFT);

	regmap_multi_reg_write(cs35l45->regmap, cs35l45_unlock,
			       ARRAY_SIZE(cs35l45_unlock));

	regmap_write(cs35l45->regmap, CS35L45_AMP_PCM_HPF_TST, hpf_override);

	regmap_multi_reg_write(cs35l45->regmap, cs35l45_lock,
			       ARRAY_SIZE(cs35l45_lock));

	asp_wl = params_width(params);
	if (asp_wl > CS35L45_ASP_WL_MAX)
		asp_wl = CS35L45_ASP_WL_MAX;
	else if (asp_wl < CS35L45_ASP_WL_MIN)
		asp_wl = CS35L45_ASP_WL_MIN;

	asp_width = cs35l45->pdata.use_tdm_slots ?
			    cs35l45->slot_width : params_physical_width(params);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		regmap_update_bits(cs35l45->regmap, CS35L45_ASP_CONTROL2,
				   CS35L45_ASP_WIDTH_RX_MASK,
				   asp_width << CS35L45_ASP_WIDTH_RX_SHIFT);

		regmap_update_bits(cs35l45->regmap, CS35L45_ASP_DATA_CONTROL5,
				   CS35L45_ASP_WL_MASK,
				   asp_wl << CS35L45_ASP_WL_SHIFT);
	} else {
		regmap_update_bits(cs35l45->regmap, CS35L45_ASP_CONTROL2,
				   CS35L45_ASP_WIDTH_TX_MASK,
				   asp_width << CS35L45_ASP_WIDTH_TX_SHIFT);

		regmap_update_bits(cs35l45->regmap, CS35L45_ASP_DATA_CONTROL1,
				   CS35L45_ASP_WL_MASK,
				   asp_wl << CS35L45_ASP_WL_SHIFT);
	}

	return 0;
}

static int cs35l45_dai_set_tdm_slot(struct snd_soc_dai *dai,
				    unsigned int tx_mask, unsigned int rx_mask,
				    int slots, int slot_width)
{
	struct cs35l45_private *cs35l45 =
			snd_soc_component_get_drvdata(dai->component);

	cs35l45->slot_width = slot_width;

	return 0;
}

static int cs35l45_dai_set_sysclk(struct snd_soc_dai *dai,
				  int clk_id, unsigned int freq, int dir)
{
	struct cs35l45_private *cs35l45 =
			snd_soc_component_get_drvdata(dai->component);

	return cs35l45_set_sysclk(cs35l45, clk_id, freq);
}

static int cs35l45_dai_startup(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	struct cs35l45_private *cs35l45 =
			snd_soc_component_get_drvdata(dai->component);

	if (cs35l45->hibernate_mode == HIBER_MODE_EN) {
		dev_err(cs35l45->dev,
			"Amp is hibernating; please wake up first\n");
		return -EPERM;
	}

	return 0;
}

static const struct snd_soc_dai_ops cs35l45_dai_ops = {
	.startup = cs35l45_dai_startup,
	.set_fmt = cs35l45_dai_set_fmt,
	.hw_params = cs35l45_dai_hw_params,
	.set_tdm_slot = cs35l45_dai_set_tdm_slot,
	.set_sysclk = cs35l45_dai_set_sysclk,
};

#define CS35L45_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | \
			 SNDRV_PCM_FMTBIT_S24_3LE| \
			 SNDRV_PCM_FMTBIT_S24_LE | \
			 SNDRV_PCM_FMTBIT_S32_LE)

#define CS35L45_RATES (SNDRV_PCM_RATE_8000  | \
		       SNDRV_PCM_RATE_16000 | \
		       SNDRV_PCM_RATE_44100 | \
		       SNDRV_PCM_RATE_48000 | \
		       SNDRV_PCM_RATE_88200 | \
		       SNDRV_PCM_RATE_96000)

static struct snd_soc_dai_driver cs35l45_dai = {
	.name = "cs35l45",
	.playback = {
		      .stream_name = "Playback",
		      .channels_min = 1,
		      .channels_max = 8,
		      .rates = CS35L45_RATES,
		      .formats = CS35L45_FORMATS,
	},
	.capture = {
		      .stream_name = "Capture",
		      .channels_min = 1,
		      .channels_max = 8,
		      .rates = CS35L45_RATES,
		      .formats = CS35L45_FORMATS,
	},
	.ops = &cs35l45_dai_ops,
};

static int cs35l45_component_set_sysclk(struct snd_soc_component *component,
					int clk_id, int source,
					unsigned int freq, int dir)
{
	struct cs35l45_private *cs35l45 =
			snd_soc_component_get_drvdata(component);

	return cs35l45_set_sysclk(cs35l45, clk_id, freq);
}

#ifdef CONFIG_SND_SOC_CIRRUS_AMP
#define CS35L45_ALG_ID_VIMON	0xf205
#define CS35L45_ALG_ID_HALO	0x4fa00
#endif

static int cs35l45_component_probe(struct snd_soc_component *component)
{
	struct cs35l45_private *cs35l45 =
			snd_soc_component_get_drvdata(component);
	struct snd_soc_dapm_context *dapm =
			snd_soc_component_get_dapm(component);
#ifdef CONFIG_SND_SOC_CIRRUS_AMP
	static struct reg_sequence cs35l45_cal_pre_config[] = {
		{CS35L45_DSP1RX6_INPUT,	CS35L45_PCM_SRC_VDD_BSTMON},
		{CS35L45_MIXER_NGATE_CH1_CFG,	0},
		{CS35L45_MIXER_NGATE_CH2_CFG,	0},
	};

	static struct reg_sequence cs35l45_cal_post_config[] = {
		{CS35L45_DSP1RX6_INPUT,	CS35L45_PCM_SRC_VDD_BATTMON},
	};

	struct cs35l45_platform_data *pdata = &cs35l45->pdata;
	struct cirrus_amp_config amp_cfg;
	unsigned int val;
	int ret;

	if (pdata->ngate_ch1_hold & CS35L45_VALID_PDATA)
		val = pdata->ngate_ch1_hold & (~CS35L45_VALID_PDATA);
	else
		val = CS35L45_AUX_NGATE_CH_HOLD_DEFAULT;

	cs35l45_cal_pre_config[1].def |= val << CS35L45_AUX_NGATE_CH_HOLD_SHIFT;

	if (pdata->ngate_ch1_thr & CS35L45_VALID_PDATA)
		val = pdata->ngate_ch1_thr & (~CS35L45_VALID_PDATA);
	else
		val = CS35L45_AUX_NGATE_CH_THR_DEFAULT;

	cs35l45_cal_pre_config[1].def |= val << CS35L45_AUX_NGATE_CH_THR_SHIFT;

	if (pdata->ngate_ch2_hold & CS35L45_VALID_PDATA)
		val = pdata->ngate_ch2_hold & (~CS35L45_VALID_PDATA);
	else
		val = CS35L45_AUX_NGATE_CH_HOLD_DEFAULT;

	cs35l45_cal_pre_config[2].def |= val << CS35L45_AUX_NGATE_CH_HOLD_SHIFT;

	if (pdata->ngate_ch2_thr & CS35L45_VALID_PDATA)
		val = pdata->ngate_ch2_thr & (~CS35L45_VALID_PDATA);
	else
		val = CS35L45_AUX_NGATE_CH_THR_DEFAULT;

	cs35l45_cal_pre_config[2].def |= val << CS35L45_AUX_NGATE_CH_THR_SHIFT;

	amp_cfg.component = component;
	amp_cfg.regmap = cs35l45->regmap;
	amp_cfg.pre_config = cs35l45_cal_pre_config;
	amp_cfg.post_config = cs35l45_cal_post_config;
	amp_cfg.dsp_part_name = cs35l45->pdata.dsp_part_name;
	amp_cfg.num_pre_configs = ARRAY_SIZE(cs35l45_cal_pre_config);
	amp_cfg.num_post_configs = ARRAY_SIZE(cs35l45_cal_post_config);
	amp_cfg.mbox_cmd = CS35L45_DSP_VIRT1_MBOX_1;
	amp_cfg.mbox_sts = CS35L45_DSP_MBOX_2;
	amp_cfg.global_en = CS35L45_IRQ1_STS_1;
	amp_cfg.global_en_mask = CS35L45_MSM_GLOBAL_EN_ASSERT_MASK;
	amp_cfg.vimon_alg_id = CS35L45_ALG_ID_VIMON;
	amp_cfg.halo_alg_id = CS35L45_ALG_ID_HALO;
	amp_cfg.bd_max_temp = cs35l45->pdata.bd_max_temp &
			      (~CS35L45_VALID_PDATA);
	amp_cfg.target_temp = cs35l45->pdata.pwr_params_cfg.target_temp &
			      (~CS35L45_VALID_PDATA);
	amp_cfg.exit_temp = cs35l45->pdata.pwr_params_cfg.exit_temp &
			    (~CS35L45_VALID_PDATA);
	amp_cfg.perform_vimon_cal = false;
	amp_cfg.calibration_disable = false;
	amp_cfg.pwr_enable = false;

	ret = cirrus_amp_add(cs35l45->pdata.mfd_suffix, amp_cfg);
	if (ret < 0) {
		dev_err(cs35l45->dev, "Failed to register cirrus amp (%d)\n",
			ret);
		return -EPROBE_DEFER;
	}
#endif

	cs35l45->dapm_mode = DAPM_MODE_ASP;

	snd_soc_dapm_add_routes(dapm, cs35l45_asp_routes,
				ARRAY_SIZE(cs35l45_asp_routes));

	snd_soc_component_disable_pin(component, "RCV");
	snd_soc_component_disable_pin(component, "DSP1 Enable");

	snd_soc_dapm_sync(dapm);

	component->regmap = cs35l45->regmap;

	wm_adsp2_component_probe(&cs35l45->dsp, component);

#ifdef CONFIG_SND_SOC_CIRRUS_AMP
	snd_soc_add_component_controls(component, &cs35l45->dsp.fw_ctrl, 1);
#endif

	return 0;
}

static void cs35l45_component_remove(struct snd_soc_component *component)
{
	struct cs35l45_private *cs35l45 =
		snd_soc_component_get_drvdata(component);

	wm_adsp2_component_remove(&cs35l45->dsp, component);
}

static const struct snd_soc_component_driver cs35l45_component = {
	.probe = cs35l45_component_probe,
	.remove = cs35l45_component_remove,
	.set_sysclk = cs35l45_component_set_sysclk,

	.dapm_widgets = cs35l45_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(cs35l45_dapm_widgets),

	.dapm_routes = cs35l45_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(cs35l45_dapm_routes),

	.controls = cs35l45_aud_controls,
	.num_controls = ARRAY_SIZE(cs35l45_aud_controls),
};

static int cs35l45_get_clk_config(int freq)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cs35l45_pll_sysclk); i++) {
		if (cs35l45_pll_sysclk[i].freq == freq)
			return cs35l45_pll_sysclk[i].clk_cfg;
	}

	return -EINVAL;
}

static int cs35l45_set_sysclk(struct cs35l45_private *cs35l45, int clk_id,
			      unsigned int freq)
{
	unsigned int val;
	int extclk_cfg, clksrc;

	switch (clk_id) {
	case 0:
		clksrc = CS35L45_PLL_REFCLK_SEL_BCLK;
		break;
	default:
		dev_err(cs35l45->dev, "Invalid CLK Config\n");
		return -EINVAL;
	}

	extclk_cfg = cs35l45_get_clk_config(freq);
	if (extclk_cfg < 0) {
		dev_err(cs35l45->dev, "Invalid CLK Config: %d, freq: %u\n",
			extclk_cfg, freq);
		return -EINVAL;
	}

	regmap_read(cs35l45->regmap, CS35L45_REFCLK_INPUT, &val);
	val = (val & CS35L45_PLL_REFCLK_FREQ_MASK) >>
		     CS35L45_PLL_REFCLK_FREQ_SHIFT;

	if (val == extclk_cfg)
		return 0;

	regmap_update_bits(cs35l45->regmap, CS35L45_REFCLK_INPUT,
			   CS35L45_PLL_OPEN_LOOP_MASK,
			   CS35L45_PLL_OPEN_LOOP_MASK);

	regmap_update_bits(cs35l45->regmap, CS35L45_REFCLK_INPUT,
			   CS35L45_PLL_REFCLK_FREQ_MASK,
			   extclk_cfg << CS35L45_PLL_REFCLK_FREQ_SHIFT);

	regmap_update_bits(cs35l45->regmap, CS35L45_REFCLK_INPUT,
			   CS35L45_PLL_REFCLK_EN_MASK, 0);

	regmap_update_bits(cs35l45->regmap, CS35L45_REFCLK_INPUT,
			   CS35L45_PLL_REFCLK_SEL_MASK,
			   clksrc << CS35L45_PLL_REFCLK_SEL_SHIFT);

	regmap_update_bits(cs35l45->regmap, CS35L45_REFCLK_INPUT,
			   CS35L45_PLL_OPEN_LOOP_MASK, 0);

	regmap_update_bits(cs35l45->regmap, CS35L45_REFCLK_INPUT,
			   CS35L45_PLL_REFCLK_EN_MASK,
			   CS35L45_PLL_REFCLK_EN_MASK);

	return 0;
}

static int cs35l45_msm_global_en_assert(struct cs35l45_private *cs35l45)
{
	if (cs35l45->amplifier_mode == AMP_MODE_RCV)
		regmap_update_bits(cs35l45->regmap, CS35L45_BLOCK_ENABLES,
				   CS35L45_BST_EN_MASK,
				   CS35L45_BST_DISABLE_FET_ON <<
				   CS35L45_BST_EN_SHIFT);

	return 0;
}

static const struct cs35l45_irq_monitor cs35l45_irq_mons[] = {
	{
		.reg = CS35L45_IRQ1_EINT_1,
		.mask = CS35L45_IRQ1_MASK_1,
		.bitmask = CS35L45_MSM_GLOBAL_EN_ASSERT_MASK,
		.description = "Global enable assertion",
		.info_msg = NULL,
		.dbg_msg = "Global enable assert detected!",
		.warn_msg = NULL,
		.err_msg = NULL,
		.callback = cs35l45_msm_global_en_assert,
	},
	{
		.reg = CS35L45_IRQ1_EINT_3,
		.mask = CS35L45_IRQ1_MASK_3,
		.bitmask = CS35L45_PLL_LOCK_FLAG_MASK,
		.description = "PLL lock",
		.info_msg = NULL,
		.dbg_msg = "PLL lock detected!",
		.warn_msg = NULL,
		.err_msg = NULL,
		.callback = NULL,
	},
	{
		.reg = CS35L45_IRQ1_EINT_3,
		.mask = CS35L45_IRQ1_MASK_3,
		.bitmask = CS35L45_PLL_UNLOCK_FLAG_RISE_MASK,
		.description = "PLL unlock flag rise",
		.info_msg = NULL,
		.dbg_msg = "PLL unlock flag rise detected!",
		.warn_msg = NULL,
		.err_msg = NULL,
		.callback = NULL,
	},
	{
		.reg = CS35L45_IRQ1_EINT_18,
		.mask = CS35L45_IRQ1_MASK_18,
		.bitmask = CS35L45_GLOBAL_ERROR_MASK,
		.description = "Global error",
		.info_msg = NULL,
		.dbg_msg = NULL,
		.warn_msg = NULL,
		.err_msg = "Global error detected!",
		.callback = NULL,
	},
};

static irqreturn_t cs35l45_irq(int irq, void *data)
{
	struct cs35l45_private *cs35l45 = data;
	const struct cs35l45_irq_monitor *irq_mon;
	unsigned int irq_regs[] = {CS35L45_IRQ1_EINT_1, CS35L45_IRQ1_EINT_2,
				   CS35L45_IRQ1_EINT_3, CS35L45_IRQ1_EINT_4,
				   CS35L45_IRQ1_EINT_5, CS35L45_IRQ1_EINT_7,
				   CS35L45_IRQ1_EINT_8, CS35L45_IRQ1_EINT_18};
	unsigned int irq_masks[] = {CS35L45_IRQ1_MASK_1, CS35L45_IRQ1_MASK_2,
				    CS35L45_IRQ1_MASK_3, CS35L45_IRQ1_MASK_4,
				    CS35L45_IRQ1_MASK_5, CS35L45_IRQ1_MASK_7,
				    CS35L45_IRQ1_MASK_8, CS35L45_IRQ1_MASK_18};
	unsigned int status[ARRAY_SIZE(irq_regs)];
	unsigned int masks[ARRAY_SIZE(irq_masks)];
	unsigned int val;
	unsigned int i;
	int ret;
	bool irq_detect = false;

	if (!cs35l45->initialized)
		return IRQ_NONE;

	for (i = 0; i < ARRAY_SIZE(irq_regs); i++) {
		regmap_read(cs35l45->regmap, irq_regs[i], &status[i]);
		regmap_read(cs35l45->regmap, irq_masks[i], &masks[i]);
		irq_detect |= (status[i] & (~masks[i]));
	}

	if (!irq_detect)
		return IRQ_NONE;

	for (i = 0; i < ARRAY_SIZE(cs35l45_irq_mons); i++) {
		irq_mon = &cs35l45_irq_mons[i];
		regmap_read(cs35l45->regmap, irq_mon->reg, &val);
		if (!(val & irq_mon->bitmask))
			continue;

		regmap_write(cs35l45->regmap, irq_mon->reg, irq_mon->bitmask);

		if (irq_mon->info_msg)
			dev_info(cs35l45->dev, "%s\n", irq_mon->info_msg);

		if (irq_mon->dbg_msg)
			dev_dbg(cs35l45->dev, "%s\n", irq_mon->dbg_msg);

		if (irq_mon->warn_msg)
			dev_warn(cs35l45->dev, "%s\n", irq_mon->warn_msg);

		if (irq_mon->err_msg)
			dev_err(cs35l45->dev, "%s\n", irq_mon->err_msg);

		if (irq_mon->callback) {
			ret = irq_mon->callback(cs35l45);
			if (ret < 0)
				dev_err(cs35l45->dev,
					"IRQ (%s) callback failure (%d)\n",
					irq_mon->description, ret);
		}
	}

	return IRQ_HANDLED;
}

static int cs35l45_register_irq_monitors(struct cs35l45_private *cs35l45)
{
	unsigned int val;
	int i;

	if (!cs35l45->irq)
		return 0;

	for (i = 0; i < ARRAY_SIZE(cs35l45_irq_mons); i++) {
		regmap_read(cs35l45->regmap, cs35l45_irq_mons[i].mask, &val);
		if (!(val & cs35l45_irq_mons[i].bitmask)) {
			dev_err(cs35l45->dev, "IRQ (%s) is already unmasked\n",
				cs35l45_irq_mons[i].description);
			continue;
		}

		regmap_write(cs35l45->regmap, cs35l45_irq_mons[i].reg,
			     cs35l45_irq_mons[i].bitmask);

		regmap_update_bits(cs35l45->regmap, cs35l45_irq_mons[i].mask,
				   cs35l45_irq_mons[i].bitmask, 0);
	}

	return 0;
}

static int cs35l45_gpio_configuration(struct cs35l45_private *cs35l45)
{
	struct cs35l45_platform_data *pdata = &cs35l45->pdata;
	struct gpio_ctrl *gpios[] = {&pdata->gpio_ctrl1, &pdata->gpio_ctrl2,
				     &pdata->gpio_ctrl3};
	unsigned int gpio_regs[] = {CS35L45_GPIO1_CTRL1, CS35L45_GPIO2_CTRL1,
				    CS35L45_GPIO3_CTRL1};
	unsigned int pad_regs[] = {CS35L45_SYNC_GPIO1,
				   CS35L45_INTB_GPIO2_MCLK_REF, CS35L45_GPIO3};
	unsigned int val;
	int i;

	for (i = 0; i < ARRAY_SIZE(gpios); i++) {
		if (!gpios[i]->is_present)
			continue;

		if (gpios[i]->dir & CS35L45_VALID_PDATA) {
			val = gpios[i]->dir & (~CS35L45_VALID_PDATA);
			regmap_update_bits(cs35l45->regmap, gpio_regs[i],
					   CS35L45_GPIO_DIR_MASK,
					   val << CS35L45_GPIO_DIR_SHIFT);
		}

		if (gpios[i]->lvl & CS35L45_VALID_PDATA) {
			val = gpios[i]->lvl & (~CS35L45_VALID_PDATA);
			regmap_update_bits(cs35l45->regmap, gpio_regs[i],
					   CS35L45_GPIO_LVL_MASK,
					   val << CS35L45_GPIO_LVL_SHIFT);
		}

		if (gpios[i]->op_cfg & CS35L45_VALID_PDATA) {
			val = gpios[i]->op_cfg & (~CS35L45_VALID_PDATA);
			regmap_update_bits(cs35l45->regmap, gpio_regs[i],
					   CS35L45_GPIO_OP_CFG_MASK,
					   val << CS35L45_GPIO_OP_CFG_SHIFT);
		}

		if (gpios[i]->pol & CS35L45_VALID_PDATA) {
			val = gpios[i]->pol & (~CS35L45_VALID_PDATA);
			regmap_update_bits(cs35l45->regmap, gpio_regs[i],
					   CS35L45_GPIO_POL_MASK,
					   val << CS35L45_GPIO_POL_SHIFT);
		}

		if (gpios[i]->ctrl & CS35L45_VALID_PDATA) {
			val = gpios[i]->ctrl & (~CS35L45_VALID_PDATA);
			regmap_update_bits(cs35l45->regmap, pad_regs[i],
					   CS35L45_GPIO_CTRL_MASK,
					   val << CS35L45_GPIO_CTRL_SHIFT);
		}

		if (gpios[i]->invert & CS35L45_VALID_PDATA) {
			val = gpios[i]->invert & (~CS35L45_VALID_PDATA);
			regmap_update_bits(cs35l45->regmap, pad_regs[i],
					   CS35L45_GPIO_INVERT_MASK,
					   val << CS35L45_GPIO_INVERT_SHIFT);
		}
	}

	return 0;
}

static int cs35l45_apply_of_data(struct cs35l45_private *cs35l45)
{
	struct cs35l45_platform_data *pdata = &cs35l45->pdata;
	const struct of_entry *entry;
	unsigned int val;
	u32 *ptr;
	int i, j, ret;

	if (!pdata)
		return 0;

	if (pdata->asp_sdout_hiz_ctrl & CS35L45_VALID_PDATA) {
		val = pdata->asp_sdout_hiz_ctrl & (~CS35L45_VALID_PDATA);
		regmap_update_bits(cs35l45->regmap, CS35L45_ASP_CONTROL3,
				   CS35L45_ASP_DOUT_HIZ_CTRL_MASK,
				   val << CS35L45_ASP_DOUT_HIZ_CTRL_SHIFT);
	}

	if (pdata->ngate_ch1_hold & CS35L45_VALID_PDATA) {
		val = pdata->ngate_ch1_hold & (~CS35L45_VALID_PDATA);
		regmap_update_bits(cs35l45->regmap, CS35L45_MIXER_NGATE_CH1_CFG,
				   CS35L45_AUX_NGATE_CH_HOLD_MASK,
				   val << CS35L45_AUX_NGATE_CH_HOLD_SHIFT);
	}

	if (pdata->ngate_ch1_thr & CS35L45_VALID_PDATA) {
		val = pdata->ngate_ch1_thr & (~CS35L45_VALID_PDATA);
		regmap_update_bits(cs35l45->regmap, CS35L45_MIXER_NGATE_CH1_CFG,
				   CS35L45_AUX_NGATE_CH_THR_MASK,
				   val << CS35L45_AUX_NGATE_CH_THR_SHIFT);
	}

	if (pdata->ngate_ch2_hold & CS35L45_VALID_PDATA) {
		val = pdata->ngate_ch2_hold & (~CS35L45_VALID_PDATA);
		regmap_update_bits(cs35l45->regmap, CS35L45_MIXER_NGATE_CH2_CFG,
				   CS35L45_AUX_NGATE_CH_HOLD_MASK,
				   val << CS35L45_AUX_NGATE_CH_HOLD_SHIFT);
	}

	if (pdata->ngate_ch2_thr & CS35L45_VALID_PDATA) {
		val = pdata->ngate_ch2_thr & (~CS35L45_VALID_PDATA);
		regmap_update_bits(cs35l45->regmap, CS35L45_MIXER_NGATE_CH2_CFG,
				   CS35L45_AUX_NGATE_CH_THR_MASK,
				   val << CS35L45_AUX_NGATE_CH_THR_SHIFT);
	}

	if (!pdata->bst_bpe_inst_cfg.is_present)
		goto bst_bpe_misc_cfg;

	for (i = BST_BPE_INST_THLD; i < BST_BPE_INST_PARAMS; i++) {
		for (j = L0; j < BST_BPE_INST_LEVELS; j++) {
			entry = cs35l45_get_bst_bpe_inst_entry(j, i);
			ptr = cs35l45_get_bst_bpe_inst_param(cs35l45, j, i);
			val = ((*ptr) & (~CS35L45_VALID_PDATA)) << entry->shift;
			if ((entry->reg) && ((*ptr) & CS35L45_VALID_PDATA))
				regmap_update_bits(cs35l45->regmap, entry->reg,
						   entry->mask, val);
		}
	}

bst_bpe_misc_cfg:
	if (!pdata->bst_bpe_misc_cfg.is_present)
		goto bst_bpe_il_lim_cfg;

	for (i = BST_BPE_INST_INF_HOLD_RLS; i < BST_BPE_MISC_PARAMS; i++) {
		ptr = cs35l45_get_bst_bpe_misc_param(cs35l45, i);
		val = ((*ptr) & (~CS35L45_VALID_PDATA))
			<< bst_bpe_misc_map[i].shift;
		if ((*ptr) & CS35L45_VALID_PDATA)
			regmap_update_bits(cs35l45->regmap,
					   bst_bpe_misc_map[i].reg,
					   bst_bpe_misc_map[i].mask, val);
	}

bst_bpe_il_lim_cfg:
	if (!pdata->bst_bpe_il_lim_cfg.is_present)
		goto hvlv_cfg;

	for (i = BST_BPE_IL_LIM_THLD_DEL1; i < BST_BPE_IL_LIM_PARAMS; i++) {
		ptr = cs35l45_get_bst_bpe_il_lim_param(cs35l45, i);
		val = ((*ptr) & (~CS35L45_VALID_PDATA))
			<< bst_bpe_il_lim_map[i].shift;
		if ((*ptr) & CS35L45_VALID_PDATA)
			regmap_update_bits(cs35l45->regmap,
					   bst_bpe_il_lim_map[i].reg,
					   bst_bpe_il_lim_map[i].mask, val);
	}

hvlv_cfg:
	if (!pdata->hvlv_cfg.is_present)
		goto ldpm_cfg;

	if (pdata->hvlv_cfg.hvlv_thld_hys & CS35L45_VALID_PDATA) {
		val = pdata->hvlv_cfg.hvlv_thld_hys & (~CS35L45_VALID_PDATA);
		regmap_update_bits(cs35l45->regmap, CS35L45_HVLV_CONFIG,
				   CS35L45_HVLV_THLD_HYS_MASK,
				   val << CS35L45_HVLV_THLD_HYS_SHIFT);
	}

	if (pdata->hvlv_cfg.hvlv_thld & CS35L45_VALID_PDATA) {
		val = pdata->hvlv_cfg.hvlv_thld & (~CS35L45_VALID_PDATA);
		regmap_update_bits(cs35l45->regmap, CS35L45_HVLV_CONFIG,
				   CS35L45_HVLV_THLD_MASK,
				   val << CS35L45_HVLV_THLD_SHIFT);
	}

	if (pdata->hvlv_cfg.hvlv_dly & CS35L45_VALID_PDATA) {
		val = pdata->hvlv_cfg.hvlv_dly & (~CS35L45_VALID_PDATA);
		regmap_update_bits(cs35l45->regmap, CS35L45_HVLV_CONFIG,
				   CS35L45_HVLV_DLY_MASK,
				   val << CS35L45_HVLV_DLY_SHIFT);
	}

ldpm_cfg:
	if (!pdata->ldpm_cfg.is_present)
		goto classh_cfg;

	for (i = LDPM_GP1_BOOST_SEL; i < LDPM_PARAMS; i++) {
		ptr = cs35l45_get_ldpm_param(cs35l45, i);
		val = ((*ptr) & (~CS35L45_VALID_PDATA)) << ldpm_map[i].shift;
		if ((*ptr) & CS35L45_VALID_PDATA)
			regmap_update_bits(cs35l45->regmap, ldpm_map[i].reg,
					   ldpm_map[i].mask, val);
	}

classh_cfg:
	if (!pdata->classh_cfg.is_present)
		goto gpio_cfg;

	for (i = CH_HDRM; i < CLASSH_PARAMS; i++) {
		ptr = cs35l45_get_classh_param(cs35l45, i);
		val = ((*ptr) & (~CS35L45_VALID_PDATA)) << classh_map[i].shift;
		if ((*ptr) & CS35L45_VALID_PDATA)
			regmap_update_bits(cs35l45->regmap, classh_map[i].reg,
					   classh_map[i].mask, val);
	}

	regmap_update_bits(cs35l45->regmap, CS35L45_CLASSH_CONFIG3,
			   CS35L45_CH_OVB_LATCH_MASK,
			   CS35L45_CH_OVB_LATCH_MASK);

	regmap_update_bits(cs35l45->regmap, CS35L45_CLASSH_CONFIG3,
			   CS35L45_CH_OVB_LATCH_MASK, 0);

gpio_cfg:
	ret = cs35l45_gpio_configuration(cs35l45);
	if (ret < 0) {
		dev_err(cs35l45->dev, "Failed to apply GPIO config (%d)\n",
			ret);
		return ret;
	}

	return 0;
}

static int cs35l45_parse_of_data(struct cs35l45_private *cs35l45)
{
	struct cs35l45_platform_data *pdata = &cs35l45->pdata;
	struct device_node *node = cs35l45->dev->of_node;
	struct device_node *child;
	const struct of_entry *entry;
	struct gpio_ctrl *gpios[] = {&pdata->gpio_ctrl1, &pdata->gpio_ctrl2,
				     &pdata->gpio_ctrl3};
	unsigned int val, params[BST_BPE_INST_LEVELS];
	char of_name[32];
	u32 *ptr;
	int ret, i, j;

	if ((!node) || (!pdata))
		return 0;

	ret = of_property_read_u32(node, "cirrus,asp-sdout-hiz-ctrl", &val);
	if (!ret)
		pdata->asp_sdout_hiz_ctrl = val | CS35L45_VALID_PDATA;

	pdata->use_tdm_slots = of_property_read_bool(node,
						     "cirrus,use-tdm-slots");

	pdata->pll_auto_en = of_property_read_bool(node, "cirrus,pll-auto-en");

	ret = of_property_read_string(node, "cirrus,dsp-part-name",
				      &pdata->dsp_part_name);
	if (ret < 0)
		pdata->dsp_part_name = "cs35l45";

	ret = of_property_read_u32(node, "cirrus,ngate-ch1-hold", &val);
	if (!ret)
		pdata->ngate_ch1_hold = val | CS35L45_VALID_PDATA;

	ret = of_property_read_u32(node, "cirrus,ngate-ch1-thr", &val);
	if (!ret)
		pdata->ngate_ch1_thr = val | CS35L45_VALID_PDATA;

	ret = of_property_read_u32(node, "cirrus,ngate-ch2-hold", &val);
	if (!ret)
		pdata->ngate_ch2_hold = val | CS35L45_VALID_PDATA;

	ret = of_property_read_u32(node, "cirrus,ngate-ch2-thr", &val);
	if (!ret)
		pdata->ngate_ch2_thr = val | CS35L45_VALID_PDATA;

#ifdef CONFIG_SND_SOC_CIRRUS_AMP
	ret = of_property_read_string(node, "cirrus,mfd-suffix",
				      &pdata->mfd_suffix);
	if (ret < 0)
		pdata->mfd_suffix = "";

	ret = of_property_read_u32(node, "cirrus,bd-max-temp", &val);
	if (!ret)
		pdata->bd_max_temp = val | CS35L45_VALID_PDATA;

	child = of_get_child_by_name(node, "cirrus,pwr-params-config");
	pdata->pwr_params_cfg.is_present = child ? true : false;
	if (!pdata->pwr_params_cfg.is_present)
		goto bst_bpe_inst_cfg;

	pdata->pwr_params_cfg.global_en = of_property_read_bool(child,
						"pwr-global-enable");

	ret = of_property_read_u32(child, "pwr-target-temp", &val);
	if (!ret)
		pdata->pwr_params_cfg.target_temp = val | CS35L45_VALID_PDATA;

	ret = of_property_read_u32(child, "pwr-exit-temp", &val);
	if (!ret)
		pdata->pwr_params_cfg.exit_temp = val | CS35L45_VALID_PDATA;

	of_node_put(child);

bst_bpe_inst_cfg:
#endif
	child = of_get_child_by_name(node, "cirrus,bst-bpe-inst-config");
	pdata->bst_bpe_inst_cfg.is_present = child ? true : false;
	if (!pdata->bst_bpe_inst_cfg.is_present)
		goto bst_bpe_misc_cfg;

	for (i = BST_BPE_INST_THLD; i < BST_BPE_INST_PARAMS; i++) {
		entry = cs35l45_get_bst_bpe_inst_entry(L0, i);
		ret = of_property_read_u32_array(child, entry->name, params,
						 BST_BPE_INST_LEVELS);
		if (ret)
			continue;

		for (j = L0; j < BST_BPE_INST_LEVELS; j++) {
			ptr = cs35l45_get_bst_bpe_inst_param(cs35l45, j, i);
			(*ptr) = params[j] | CS35L45_VALID_PDATA;
		}
	}

	of_node_put(child);

bst_bpe_misc_cfg:
	child = of_get_child_by_name(node, "cirrus,bst-bpe-misc-config");
	pdata->bst_bpe_misc_cfg.is_present = child ? true : false;
	if (!pdata->bst_bpe_misc_cfg.is_present)
		goto bst_bpe_il_lim_cfg;

	for (i = BST_BPE_INST_INF_HOLD_RLS; i < BST_BPE_MISC_PARAMS; i++) {
		ptr = cs35l45_get_bst_bpe_misc_param(cs35l45, i);
		ret = of_property_read_u32(child, bst_bpe_misc_map[i].name,
					   &val);
		if (!ret)
			(*ptr) = val | CS35L45_VALID_PDATA;
	}

	of_node_put(child);

bst_bpe_il_lim_cfg:
	child = of_get_child_by_name(node, "cirrus,bst-bpe-il-lim-config");
	pdata->bst_bpe_il_lim_cfg.is_present = child ? true : false;
	if (!pdata->bst_bpe_il_lim_cfg.is_present)
		goto hvlv_cfg;

	for (i = BST_BPE_IL_LIM_THLD_DEL1; i < BST_BPE_IL_LIM_PARAMS; i++) {
		ptr = cs35l45_get_bst_bpe_il_lim_param(cs35l45, i);
		ret = of_property_read_u32(child, bst_bpe_il_lim_map[i].name,
					   &val);
		if (!ret)
			(*ptr) = val | CS35L45_VALID_PDATA;
	}

	of_node_put(child);

hvlv_cfg:
	child = of_get_child_by_name(node, "cirrus,hvlv-config");
	pdata->hvlv_cfg.is_present = child ? true : false;
	if (!pdata->hvlv_cfg.is_present)
		goto ldpm_cfg;

	ret = of_property_read_u32(child, "hvlv-thld-hys", &val);
	if (!ret)
		pdata->hvlv_cfg.hvlv_thld_hys = val | CS35L45_VALID_PDATA;

	ret = of_property_read_u32(child, "hvlv-thld", &val);
	if (!ret)
		pdata->hvlv_cfg.hvlv_thld = val | CS35L45_VALID_PDATA;

	ret = of_property_read_u32(child, "hvlv-dly", &val);
	if (!ret)
		pdata->hvlv_cfg.hvlv_dly = val | CS35L45_VALID_PDATA;

	of_node_put(child);

ldpm_cfg:
	child = of_get_child_by_name(node, "cirrus,ldpm-config");
	pdata->ldpm_cfg.is_present = child ? true : false;
	if (!pdata->ldpm_cfg.is_present)
		goto classh_cfg;

	for (i = LDPM_GP1_BOOST_SEL; i < LDPM_PARAMS; i++) {
		ptr = cs35l45_get_ldpm_param(cs35l45, i);
		ret = of_property_read_u32(child, ldpm_map[i].name, &val);
		if (!ret)
			(*ptr) = val | CS35L45_VALID_PDATA;
	}

	of_node_put(child);

classh_cfg:
	child = of_get_child_by_name(node, "cirrus,classh-config");
	pdata->classh_cfg.is_present = child ? true : false;
	if (!pdata->classh_cfg.is_present)
		goto gpio_cfg;

	for (i = CH_HDRM; i < CLASSH_PARAMS; i++) {
		ptr = cs35l45_get_classh_param(cs35l45, i);
		ret = of_property_read_u32(child, classh_map[i].name, &val);
		if (!ret)
			(*ptr) = val | CS35L45_VALID_PDATA;
	}

	of_node_put(child);

gpio_cfg:
	for (i = 0; i < ARRAY_SIZE(gpios); i++) {
		sprintf(of_name, "cirrus,gpio-ctrl%d", i + 1);
		child = of_get_child_by_name(node, of_name);
		gpios[i]->is_present = child ? true : false;
		if (!gpios[i]->is_present)
			continue;

		ret = of_property_read_u32(child, "gpio-dir", &val);
		if (!ret)
			gpios[i]->dir = val | CS35L45_VALID_PDATA;

		ret = of_property_read_u32(child, "gpio-lvl", &val);
		if (!ret)
			gpios[i]->lvl = val | CS35L45_VALID_PDATA;

		ret = of_property_read_u32(child, "gpio-op-cfg", &val);
		if (!ret)
			gpios[i]->op_cfg = val | CS35L45_VALID_PDATA;

		ret = of_property_read_u32(child, "gpio-pol", &val);
		if (!ret)
			gpios[i]->pol = val | CS35L45_VALID_PDATA;

		ret = of_property_read_u32(child, "gpio-ctrl", &val);
		if (!ret)
			gpios[i]->ctrl = val | CS35L45_VALID_PDATA;

		ret = of_property_read_u32(child, "gpio-invert", &val);
		if (!ret)
			gpios[i]->invert = val | CS35L45_VALID_PDATA;

		of_node_put(child);
	}

	return 0;
}

static int cs35l45_hibernate(struct cs35l45_private *cs35l45, bool hiber_en)
{
	unsigned int sts, cmd, val;
	int ret, i;
	struct cs35l45_mixer_cache mixer_cache[] = {
		{CS35L45_ASPTX1_INPUT,	CS35L45_PCM_SRC_MASK, 0},
		{CS35L45_ASPTX2_INPUT,	CS35L45_PCM_SRC_MASK, 0},
		{CS35L45_ASPTX3_INPUT,	CS35L45_PCM_SRC_MASK, 0},
		{CS35L45_ASPTX4_INPUT,	CS35L45_PCM_SRC_MASK, 0},
		{CS35L45_DSP1RX1_INPUT,	CS35L45_PCM_SRC_MASK, 0},
		{CS35L45_DSP1RX2_INPUT, CS35L45_PCM_SRC_MASK, 0},
		{CS35L45_DSP1RX3_INPUT, CS35L45_PCM_SRC_MASK, 0},
		{CS35L45_DSP1RX4_INPUT, CS35L45_PCM_SRC_MASK, 0},
		{CS35L45_DSP1RX5_INPUT, CS35L45_PCM_SRC_MASK, 0},
		{CS35L45_DSP1RX6_INPUT, CS35L45_PCM_SRC_MASK, 0},
		{CS35L45_DSP1RX7_INPUT, CS35L45_PCM_SRC_MASK, 0},
		{CS35L45_DSP1RX8_INPUT, CS35L45_PCM_SRC_MASK, 0},
		{CS35L45_DACPCM1_INPUT, CS35L45_PCM_SRC_MASK, 0},
		{CS35L45_NGATE1_INPUT, CS35L45_PCM_SRC_MASK, 0},
		{CS35L45_NGATE2_INPUT, CS35L45_PCM_SRC_MASK, 0},
		{CS35L45_AMP_GAIN, CS35L45_AMP_GAIN_PCM_MASK, 0},
		{CS35L45_AMP_OUTPUT_MUTE, CS35L45_AMP_MUTE_MASK, 0},
		{CS35L45_AMP_PCM_CONTROL, CS35L45_AMP_VOL_PCM_MASK, 0},
		{CS35L45_ASP_FRAME_CONTROL1, CS35L45_ASP_TX_ALL_SLOTS, 0},
		{CS35L45_ASP_FRAME_CONTROL5, CS35L45_ASP_RX_ALL_SLOTS, 0},
	};

	if (hiber_en == cs35l45->hibernate_mode)
		return 0;

	if (!cs35l45->dsp.booted) {
		dev_err(cs35l45->dev, "Firmware not loaded\n");
		return -EPERM;
	}

	if (hiber_en == HIBER_MODE_EN) {
		regmap_read(cs35l45->regmap, CS35L45_DSP_MBOX_2, &sts);
		if (((enum cspl_mboxstate)sts) != CSPL_MBOX_STS_PAUSED) {
			dev_err(cs35l45->dev, "FW not paused (%d)\n", sts);
			return -EINVAL;
		}

		flush_work(&cs35l45->dsp_pmd_work);

		cmd = CSPL_MBOX_CMD_HIBERNATE;
		regmap_write(cs35l45->regmap, CS35L45_DSP_VIRT1_MBOX_1, cmd);

		ret = cs35l45_activate_ctl(cs35l45, "DSP1 Preload Switch",
					   false);
		if (ret < 0)
			dev_err(cs35l45->dev, "Unable to deactivate ctl (%d)\n",
				ret);

		cs35l45->initialized = false;

		regcache_cache_only(cs35l45->regmap, true);
	} else  /* HIBER_MODE_DIS */ {
		for (i = 0; i < ARRAY_SIZE(mixer_cache); i++)
			regmap_read(cs35l45->regmap, mixer_cache[i].reg,
				    &mixer_cache[i].val);

		regcache_cache_only(cs35l45->regmap, false);

		regcache_drop_region(cs35l45->regmap, CS35L45_DEVID,
				     CS35L45_MIXER_NGATE_CH2_CFG);

		for (i = 0; i < 5; i++) {
			usleep_range(200, 300);

			ret = regmap_read(cs35l45->regmap, CS35L45_DEVID, &val);
			if (!ret)
				break;
		}

		if (i == 5) {
			dev_info(cs35l45->dev, "Timeout trying to wake amp");
			return -ETIMEDOUT;
		}

		ret = __cs35l45_initialize(cs35l45);
		if (ret < 0) {
			dev_err(cs35l45->dev, "Failed to reinitialize (%d)\n",
				ret);
			return ret;
		}

		regmap_update_bits(cs35l45->regmap, CS35L45_PWRMGT_CTL,
				   CS35L45_MEM_RDY_MASK, CS35L45_MEM_RDY_MASK);

		usleep_range(100, 200);

		cmd = CSPL_MBOX_CMD_OUT_OF_HIBERNATE;
		ret = cs35l45_set_csplmboxcmd(cs35l45, cmd);
		if (ret < 0)
			dev_err(cs35l45->dev, "MBOX failure (%d)\n", ret);

		for (i = 0; i < ARRAY_SIZE(mixer_cache); i++)
			regmap_update_bits(cs35l45->regmap, mixer_cache[i].reg,
					   mixer_cache[i].mask,
					   mixer_cache[i].val);

		ret = cs35l45_activate_ctl(cs35l45, "DSP1 Preload Switch",
					   true);
		if (ret < 0)
			dev_err(cs35l45->dev, "Unable to activate ctl (%d)\n",
				ret);
	}

	cs35l45->hibernate_mode = hiber_en;

	return 0;
}

static const struct reg_sequence cs35l45_sync_patch[] = {
	{0x00000040,			0x00000055},
	{0x00000040,			0x000000AA},
	{0x00000044,			0x00000055},
	{0x00000044,			0x000000AA},
	{CS35L45_GLOBAL_OVERRIDES,	0x0000000A},
	{0x0000350C,			0x7FF0007C},
	{0x00003510,			0x00007FF0},
	{0x00000040,			0x00000000},
	{0x00000044,			0x00000000},
};

static const struct reg_sequence cs35l45_init_patch[] = {
	{0x00000040,		0x00000055},
	{0x00000040,		0x000000AA},
	{0x00000044,		0x00000055},
	{0x00000044,		0x000000AA},
	{0x00006480,		0x0830500A},
	{0x00007C60,		0x1000850B},
	{CS35L45_BOOST_OV_CFG,	0x007000D0},
	{CS35L45_LDPM_CONFIG,	0x0001B636},
	{0x00002C08,		0x00000009},
	{0x00006850,		0x0A30FFC4},
	{0x00003820,		0x00040100},
	{0x00003824,		0x00000000},
	{0x00007CFC,		0x62870004},
	{0x00000040,		0x00000000},
	{0x00000044,		0x00000000},
	{CS35L45_BOOST_CCM_CFG,	0xF0000003},
	{CS35L45_BOOST_DCM_CFG,	0x08710220},
	{CS35L45_ERROR_RELEASE,	0x00200000},
};

static int __cs35l45_initialize(struct cs35l45_private *cs35l45)
{
	struct device *dev = cs35l45->dev;
	unsigned int sts, wksrc;
	int ret, i;

	if (cs35l45->initialized)
		return -EPERM;

	for (i = 0; i < 5; i++) {
		usleep_range(1000, 1100);

		regmap_read(cs35l45->regmap, CS35L45_IRQ1_EINT_4, &sts);
		if (!(sts & CS35L45_OTP_BOOT_DONE_STS_MASK))
			continue;

		regmap_write(cs35l45->regmap, CS35L45_IRQ1_EINT_4,
			     CS35L45_OTP_BOOT_DONE_STS_MASK |
			     CS35L45_OTP_BUSY_MASK);

		break;
	}

	if (i == 5) {
		dev_err(cs35l45->dev, "Timeout waiting for OTP boot\n");
		return -ETIMEDOUT;
	}

	ret = regmap_multi_reg_write(cs35l45->regmap, cs35l45_init_patch,
				     ARRAY_SIZE(cs35l45_init_patch));
	if (ret < 0) {
		dev_err(dev, "Failed to apply init patch %d\n", ret);
		return ret;
	}

	if (cs35l45->bus_type == CONTROL_BUS_SPI)
		regmap_update_bits(cs35l45->regmap, CS35L45_REFCLK_INPUT,
				   CS35L45_PLL_FORCE_EN_MASK,
				   CS35L45_PLL_FORCE_EN_MASK);

	regmap_write(cs35l45->regmap, CS35L45_MIXER_PILOT0_INPUT,
		     CS35L45_PCM_SRC_DSP_TX2);

	ret = cs35l45_apply_of_data(cs35l45);
	if (ret < 0) {
		dev_err(dev, "applying OF data failed (%d)\n", ret);
		return ret;
	}

	if (cs35l45->irq) {
		ret = cs35l45_register_irq_monitors(cs35l45);
		if (ret < 0) {
			dev_err(dev, "Failed to register IRQ monitors: %d\n",
				ret);
			return ret;
		}
	}

	if (cs35l45->bus_type == CONTROL_BUS_I2C)
		wksrc = CS35L45_WKSRC_I2C;
	else
		wksrc = CS35L45_WKSRC_SPI;

	regmap_update_bits(cs35l45->regmap, CS35L45_WAKESRC_CTL,
			   CS35L45_WKSRC_EN_MASK,
			   wksrc << CS35L45_WKSRC_EN_SHIFT);

	regmap_update_bits(cs35l45->regmap, CS35L45_WAKESRC_CTL,
			   CS35L45_UPDT_WKCTL_MASK, CS35L45_UPDT_WKCTL_MASK);

	regmap_update_bits(cs35l45->regmap, CS35L45_WKI2C_CTL,
			   CS35L45_WKI2C_ADDR_MASK, cs35l45->i2c_addr);

	regmap_update_bits(cs35l45->regmap, CS35L45_WKI2C_CTL,
			   CS35L45_UPDT_WKI2C_MASK, CS35L45_UPDT_WKI2C_MASK);

	ret = regmap_multi_reg_write(cs35l45->regmap, cs35l45_sync_patch,
				     ARRAY_SIZE(cs35l45_sync_patch));
	if (ret < 0) {
		dev_err(dev, "Failed to apply sync patch %d\n", ret);
		return ret;
	}

	cs35l45->initialized = true;

	return 0;
}

int cs35l45_initialize(struct cs35l45_private *cs35l45)
{
	struct device *dev = cs35l45->dev;
	unsigned int dev_id, rev_id;
	int ret;

	ret = regmap_read(cs35l45->regmap, CS35L45_DEVID, &dev_id);
	if (ret < 0) {
		dev_err(dev, "Get Device ID failed\n");
		return ret;
	}

	ret = regmap_read(cs35l45->regmap, CS35L45_REVID, &rev_id);
	if (ret < 0) {
		dev_err(dev, "Get Revision ID failed\n");
		return ret;
	}

	ret = __cs35l45_initialize(cs35l45);
	if (ret < 0) {
		dev_err(dev, "CS35L45 failed to initialize (%d)\n", ret);
		return ret;
	}

	regmap_update_bits(cs35l45->regmap,
			   CS35L45_DSP1_STREAM_ARB_TX1_CONFIG_0,
			   CS35L45_DSP1_STREAM_ARB_TX1_EN_MASK, 0);

	regmap_update_bits(cs35l45->regmap,
			   CS35L45_DSP1_STREAM_ARB_MSTR1_CONFIG_0,
			   CS35L45_DSP1_STREAM_ARB_MSTR0_EN_MASK, 0);

	regmap_update_bits(cs35l45->regmap, CS35L45_DSP1_CCM_CORE_CONTROL,
			   CS35L45_CCM_CORE_EN_MASK, 0);

	dev_info(dev, "Cirrus Logic CS35L45 (%x), Revision: %02X\n", dev_id,
		 rev_id);

	return 0;
}
EXPORT_SYMBOL_GPL(cs35l45_initialize);

static const struct wm_adsp_region cs35l45_dsp1_regions[] = {
	{ .type = WMFW_HALO_PM_PACKED,	.base = CS35L45_DSP1_PMEM_0 },
	{ .type = WMFW_HALO_XM_PACKED,	.base = CS35L45_DSP1_XMEM_PACK_0 },
	{ .type = WMFW_HALO_YM_PACKED,	.base = CS35L45_DSP1_YMEM_PACK_0 },
	{. type = WMFW_ADSP2_XM,	.base = CS35L45_DSP1_XMEM_UNPACK24_0},
	{. type = WMFW_ADSP2_YM,	.base = CS35L45_DSP1_YMEM_UNPACK24_0},
};

static int cs35l45_dsp_init(struct cs35l45_private *cs35l45)
{
	struct wm_adsp *dsp = &cs35l45->dsp;
	int ret, i;

	dsp->part = cs35l45->pdata.dsp_part_name;
	dsp->num = 1;
	dsp->type = WMFW_HALO;
	dsp->rev = 0;
	dsp->dev = cs35l45->dev;
	dsp->regmap = cs35l45->regmap;
	dsp->base = CS35L45_DSP1_CLOCK_FREQ;
	dsp->base_sysinfo = CS35L45_DSP1_SYS_ID;
	dsp->mem = cs35l45_dsp1_regions;
	dsp->num_mems = ARRAY_SIZE(cs35l45_dsp1_regions);
	dsp->lock_regions = 0xFFFFFFFF;
	dsp->n_rx_channels = CS35L45_DSP_N_RX_RATES;
	dsp->n_tx_channels = CS35L45_DSP_N_TX_RATES;

	mutex_init(&cs35l45->rate_lock);
	ret = wm_halo_init(dsp, &cs35l45->rate_lock);

	for (i = 0; i < CS35L45_DSP_N_RX_RATES; i++)
		dsp->rx_rate_cache[i] = 0x1;

	for (i = 0; i < CS35L45_DSP_N_TX_RATES; i++)
		dsp->tx_rate_cache[i] = 0x1;

	if (!cs35l45_halo_start_core) {
		cs35l45_halo_start_core = dsp->ops->start_core;
		cs35l45_halo_ops = (*dsp->ops);
		cs35l45_halo_ops.start_core = NULL;
	}

	dsp->ops = &cs35l45_halo_ops;

	return ret;
}

static const char * const cs35l45_supplies[] = {"VA", "VP"};

int cs35l45_probe(struct cs35l45_private *cs35l45)
{
	struct device *dev = cs35l45->dev;
	unsigned long irq_pol = IRQF_ONESHOT | IRQF_SHARED;
	int ret;
	u32 i;

	INIT_WORK(&cs35l45->dsp_pmd_work, cs35l45_dsp_pmd_work);

	mutex_init(&cs35l45->dsp_pmd_lock);

	for (i = 0; i < ARRAY_SIZE(cs35l45_supplies); i++)
		cs35l45->supplies[i].supply = cs35l45_supplies[i];

	ret = devm_regulator_bulk_get(dev, CS35L45_NUM_SUPPLIES,
				      cs35l45->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to request core supplies: %d\n", ret);
		return ret;
	}

	ret = regulator_bulk_enable(CS35L45_NUM_SUPPLIES, cs35l45->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable core supplies: %d\n", ret);
		return ret;
	}

	/* returning NULL can be an option if in stereo mode */
	cs35l45->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						      GPIOD_OUT_LOW);
	if (IS_ERR(cs35l45->reset_gpio)) {
		ret = PTR_ERR(cs35l45->reset_gpio);
		cs35l45->reset_gpio = NULL;
		if (ret == -EBUSY) {
			dev_info(dev,
				 "Reset line busy, assuming shared reset\n");
		} else {
			dev_err(dev, "Failed to get reset GPIO: %d\n", ret);
			goto err;
		}
	}

	if (cs35l45->reset_gpio) {
		gpiod_set_value_cansleep(cs35l45->reset_gpio, 0);
		usleep_range(2000, 2100);
		gpiod_set_value_cansleep(cs35l45->reset_gpio, 1);
	}

	cs35l45->slot_width = CS35L45_DEFAULT_SLOT_WIDTH;

	ret = cs35l45_parse_of_data(cs35l45);
	if (ret < 0) {
		dev_err(dev, "parsing OF data failed: %d\n", ret);
		goto err;
	}

	ret = cs35l45_dsp_init(cs35l45);
	if (ret < 0) {
		dev_err(dev, "dsp_init failed: %d\n", ret);
		goto err;
	}

	if (cs35l45->irq) {
		if (cs35l45->pdata.gpio_ctrl2.invert & (~CS35L45_VALID_PDATA))
			irq_pol |= IRQF_TRIGGER_HIGH;
		else
			irq_pol |= IRQF_TRIGGER_LOW;

		ret = devm_request_threaded_irq(dev, cs35l45->irq, NULL,
						cs35l45_irq, irq_pol,
						"cs35l45", cs35l45);
		if (ret < 0)
			dev_warn(cs35l45->dev, "Failed to request IRQ: %d\n",
				 ret);
	}

	return devm_snd_soc_register_component(dev, &cs35l45_component,
					       &cs35l45_dai, 1);

err:
	regulator_bulk_disable(CS35L45_NUM_SUPPLIES, cs35l45->supplies);
	return ret;
}
EXPORT_SYMBOL_GPL(cs35l45_probe);

int cs35l45_remove(struct cs35l45_private *cs35l45)
{
	if (cs35l45->reset_gpio)
		gpiod_set_value_cansleep(cs35l45->reset_gpio, 0);

	wm_adsp2_remove(&cs35l45->dsp);
	regulator_bulk_disable(CS35L45_NUM_SUPPLIES, cs35l45->supplies);

	return 0;
}
EXPORT_SYMBOL_GPL(cs35l45_remove);

MODULE_DESCRIPTION("ASoC CS35L45 driver");
MODULE_AUTHOR("James Schulman, Cirrus Logic Inc, <james.schulman@cirrus.com>");
MODULE_LICENSE("GPL");

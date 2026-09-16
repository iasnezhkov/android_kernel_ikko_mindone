// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#include <linux/delay.h>
#include <linux/string.h>

#include "kd_camera_typedef.h"
#include "kd_camera_feature.h"

#include <linux/moduleparam.h>

#include "imgsensor_sensor.h"
#include "imgsensor_hw.h"
int curr_sensor_id;// prize add by zhuzhengjiang for camera 20220110 start
int last_sensor_idx; // prize add by zhuzhengjiang for  switch camera slow

/* MINDONE-CAM-BISECT: manual, insmod-time power-sequence bisection, ON branch
 * only (OFF/cleanup untouched). mindone_pwr_stop=N: stop after 0-based step N
 * of IMX766 sensor_power_sequence[] (default 99 = run all; step numbering in
 * CAMERA-IMX766-2908 §8.4.1). mindone_pwr_skip_mclk=1: skip only the
 * MCLK step, isolating whether the GPIO126 MCLK function change kills the
 * board (§8.4 hypothesis c).
 */
static int mindone_pwr_stop = 99;
module_param(mindone_pwr_stop, int, 0644);
MODULE_PARM_DESC(mindone_pwr_stop,
	"MINDONE-CAM-BISECT: 0-based IMX766 power-on step to stop after (inclusive), default 99=run all");

static bool mindone_pwr_skip_mclk;
module_param(mindone_pwr_skip_mclk, bool, 0644);
MODULE_PARM_DESC(mindone_pwr_skip_mclk,
	"MINDONE-CAM-BISECT: 1 = skip the MCLK (pin=10) power-on step, still run every other step");

/* MINDONE-CAM-BISECT 29.08 (round 2): isolate GPIO15(RST)=HIGH itself, with
 * NO rails and NO MCLK ever driven first -- tests whether the RST=HIGH
 * transition alone is what the board reacts to (systemic/shared-net wiring),
 * independent of the "rails+RST combination" hypothesis. When set, every ON
 * step is skipped EXCEPT the one matching (pin==RST && state==HIGH) -- i.e.
 * PDN/AVDD/AVDD1/AFVDD/DVDD/DOVDD/MCLK and even RST=LOW are all skipped, only
 * the single RST=HIGH pdev->set() call actually executes.
 */
static bool mindone_rst_only;
module_param(mindone_rst_only, bool, 0644);
MODULE_PARM_DESC(mindone_rst_only,
	"MINDONE-CAM-BISECT: 1 = skip every ON step except RST=HIGH (no rails, no MCLK, no PDN, no RST=LOW)");
/*the index is consistent with enum IMGSENSOR_HW_PIN*/
char * const imgsensor_hw_pin_names[] = {
	"none",
	"pnd", // prize add by zhuzhengjiang
	"rst",
	"vcama",
	"vcama1",
	"vcamaf",
	"vcamd",
	"vcamio",
	"mipi_switch_en",
	"mipi_switch_sel",
	"mclk"
};

/*the index is consistent with enum IMGSENSOR_HW_ID*/
char * const imgsensor_hw_id_names[] = {
	"mclk",
	"regulator",
	"gpio"
};
char * const imgsensor_prj_names[] = {
	"tb8781p2_64"
};
enum IMGSENSOR_RETURN imgsensor_hw_init(struct IMGSENSOR_HW *phw)
{
	struct IMGSENSOR_HW_SENSOR_POWER      *psensor_pwr;
	struct IMGSENSOR_HW_CFG               *pcust_pwr_cfg;
	struct IMGSENSOR_HW_CUSTOM_POWER_INFO *ppwr_info;
	unsigned int i, j, len;
	char str_prop_name[LENGTH_FOR_SNPRINTF];
	const char *pin_hw_id_name;
	const char *prj_name;
	unsigned int custlen = 0;
	struct device_node *of_node
		= of_find_compatible_node(NULL, NULL, "mediatek,imgsensor");
	int ret_snprintf = 0;

	mutex_init(&phw->common.pinctrl_mutex);

	memset(str_prop_name, 0, sizeof(str_prop_name));
	snprintf(str_prop_name, sizeof(str_prop_name),
			"mtk_custom_project");
	if (of_property_read_string(
			of_node, str_prop_name,
			&prj_name) == 0) {
		custlen = strlen(prj_name);
	}
	/* update the imgsensor_custom_cfg by dts */
	for (i = 0; i < IMGSENSOR_SENSOR_IDX_MAX_NUM; i++) {
		PK_DBG("IMGSENSOR_SENSOR_IDX: %d\n", i);

		if (custlen != 0 && strncmp(prj_name, imgsensor_prj_names[0], custlen)
			== 0) {
			pcust_pwr_cfg = imgsensor_mt8781_config;
		} else {
			pcust_pwr_cfg = imgsensor_custom_config;
		}


		while (pcust_pwr_cfg->sensor_idx != i &&
		       pcust_pwr_cfg->sensor_idx != IMGSENSOR_SENSOR_IDX_NONE)
			pcust_pwr_cfg++;

		if (pcust_pwr_cfg->sensor_idx == IMGSENSOR_SENSOR_IDX_NONE)
			continue;

		/* i2c_dev */
		switch (i) {
		case IMGSENSOR_SENSOR_IDX_MAIN2:
			{
				if (IS_MT6877(phw->g_platform_id) ||
					IS_MT6833(phw->g_platform_id) ||
					IS_MT6781(phw->g_platform_id) ||
					IS_MT6779(phw->g_platform_id))
					pcust_pwr_cfg->i2c_dev = IMGSENSOR_I2C_DEV_1;
				else
					pcust_pwr_cfg->i2c_dev = IMGSENSOR_I2C_DEV_1;//prize modify by linchong 20220613
			}
			break;
		case IMGSENSOR_SENSOR_IDX_SUB2:
			{
				if (IS_MT6785(phw->g_platform_id) ||
					IS_MT6779(phw->g_platform_id) ||
					IS_MT6768(phw->g_platform_id))
					pcust_pwr_cfg->i2c_dev = IMGSENSOR_I2C_DEV_1;
				else
					pcust_pwr_cfg->i2c_dev = IMGSENSOR_I2C_DEV_3;
			}
			break;
		case IMGSENSOR_SENSOR_IDX_MAIN3:
			{
				if (IS_MT6853(phw->g_platform_id) ||
					IS_MT6785(phw->g_platform_id) ||
					IS_MT6779(phw->g_platform_id))
					pcust_pwr_cfg->i2c_dev = IMGSENSOR_I2C_DEV_1;
				else if (IS_MT6893(phw->g_platform_id) ||
					IS_MT6885(phw->g_platform_id) ||
					IS_MT6873(phw->g_platform_id) ||
					IS_MT6855(phw->g_platform_id))
					pcust_pwr_cfg->i2c_dev = IMGSENSOR_I2C_DEV_4;
				else
					pcust_pwr_cfg->i2c_dev = IMGSENSOR_I2C_DEV_2;
			}
			break;
		default:
			break;
		}

		/* pwr_info */
		ppwr_info = pcust_pwr_cfg->pwr_info;
		while (ppwr_info->pin != IMGSENSOR_HW_PIN_NONE) {
			memset(str_prop_name, 0, sizeof(str_prop_name));
			ret_snprintf = snprintf(str_prop_name,
					sizeof(str_prop_name),
					"cam%d_pin_%s",
					i,
					imgsensor_hw_pin_names[ppwr_info->pin]);
			if (ret_snprintf < 0)
				PK_DBG("NOTICE: %s, snprintf err, %d\n",
					__func__, ret_snprintf);
			if (of_property_read_string(
				of_node, str_prop_name,
				&pin_hw_id_name) == 0) {
				for (j = 0; j < IMGSENSOR_HW_ID_MAX_NUM; j++) {
					len = strlen(imgsensor_hw_id_names[j]);
					if (strncmp(pin_hw_id_name, imgsensor_hw_id_names[j], len)
						== 0) {
						PK_DBG("imgsensor_hw_cfg hw_pin:%s,name:%s,id:%d\n",
							str_prop_name, pin_hw_id_name, j);
						/* MINDONE-CAM-PWR: per-pin dt-override trace (F3056/F3063 test done; pr_debug = quiet). */
						pr_debug("MINDONE-CAM-PWR: %s dt-override id=%d(%s)\n",
							str_prop_name, j, imgsensor_hw_id_names[j]);
						ppwr_info->id = j;
						break;
					}
				}
			} else {
				/* MINDONE-CAM-IMX766-B: do NOT force IMGSENSOR_HW_ID_NONE.
				 * This board's kd_camera_hw1 DT node has no cam%d_pin_%s
				 * properties, so this branch used to run for every pin,
				 * silently overwriting the sane compile-time defaults
				 * with NONE and making power_sequence() skip every
				 * pdev->set() -- a silent power no-op. Keep the default
				 * when the DT override is absent. */
				PK_DBG("NOTICE: imgsensor_hw_cfg hw_pin:%s, dts override absent, keep default id:%d\n",
					str_prop_name, ppwr_info->id);
				/* MINDONE-CAM-PWR: same pr_info reasoning as above. */
				pr_debug("MINDONE-CAM-PWR: %s dt-absent, kept-default id=%d\n",
					str_prop_name, ppwr_info->id);
			}
			ppwr_info++;
		}
	}
	/* update the imgsensor_custom_cfg by dts END */

	for (i = 0; i < IMGSENSOR_HW_ID_MAX_NUM; i++) {
		if (hw_open[i] != NULL)
			(hw_open[i]) (&phw->pdev[i]);

		if (phw->pdev[i]->init != NULL)
			(phw->pdev[i]->init)(
				phw->pdev[i]->pinstance, &phw->common);
	}

	for (i = 0; i < IMGSENSOR_SENSOR_IDX_MAX_NUM; i++) {
		psensor_pwr = &phw->sensor_pwr[i];

		if (custlen != 0 && strncmp(prj_name, imgsensor_prj_names[0], custlen)
			== 0) {
			pcust_pwr_cfg = imgsensor_mt8781_config;
		} else {
			pcust_pwr_cfg = imgsensor_custom_config;
		}

		while (pcust_pwr_cfg->sensor_idx != i &&
		       pcust_pwr_cfg->sensor_idx != IMGSENSOR_SENSOR_IDX_NONE)
			pcust_pwr_cfg++;

		if (pcust_pwr_cfg->sensor_idx == IMGSENSOR_SENSOR_IDX_NONE)
			continue;

		ppwr_info = pcust_pwr_cfg->pwr_info;
		while (ppwr_info->pin != IMGSENSOR_HW_PIN_NONE) {
			if (ppwr_info->pin != IMGSENSOR_HW_PIN_UNDEF) {
				for (j = 0;
					j < IMGSENSOR_HW_ID_MAX_NUM &&
					ppwr_info->id != phw->pdev[j]->id;
					j++) {
				}
				psensor_pwr->id[ppwr_info->pin] = j;
				/* MINDONE-CAM-PWR 29.08: this is the value
				 * imgsensor_hw_power_sequence() will actually read
				 * for this (sensor_idx, pin) at power-on/off time --
				 * log it once here, at commit time, so it doesn't
				 * need per-call ratelimiting.
				 */
				pr_debug("MINDONE-CAM-PWR: sensor_idx=%d pin=%d(%s) final id=%d(%s)\n",
					i, ppwr_info->pin,
					imgsensor_hw_pin_names[ppwr_info->pin],
					j,
					(j < IMGSENSOR_HW_ID_MAX_NUM)
						? imgsensor_hw_id_names[j] : "NONE");
			}
			ppwr_info++;
		}
	}

	for (i = 0; i < IMGSENSOR_SENSOR_IDX_MAX_NUM; i++) {
		memset(str_prop_name, 0, sizeof(str_prop_name));
		snprintf(str_prop_name,
			sizeof(str_prop_name),
			"cam%d_%s",
			i,
			"enable_sensor");
		if (of_property_read_string(
			of_node, str_prop_name,
			&phw->enable_sensor_by_index[i]) < 0) {
			PK_DBG("Property cust-sensor not defined\n");
			phw->enable_sensor_by_index[i] = NULL;
		}
	}

	return IMGSENSOR_RETURN_SUCCESS;
}

enum IMGSENSOR_RETURN imgsensor_hw_release_all(struct IMGSENSOR_HW *phw)
{
	int i;

	for (i = 0; i < IMGSENSOR_HW_ID_MAX_NUM; i++) {
		if (phw->pdev[i]->release != NULL)
			(phw->pdev[i]->release)(phw->pdev[i]->pinstance);
	}
	return IMGSENSOR_RETURN_SUCCESS;
}

static enum IMGSENSOR_RETURN imgsensor_hw_power_sequence(
		struct IMGSENSOR_HW             *phw,
		enum   IMGSENSOR_SENSOR_IDX      sensor_idx,
		enum   IMGSENSOR_HW_POWER_STATUS pwr_status,
		struct IMGSENSOR_HW_POWER_SEQ   *ppower_sequence,
		char *pcurr_idx)
{
	struct IMGSENSOR_HW_SENSOR_POWER *psensor_pwr =
					&phw->sensor_pwr[sensor_idx];
	struct IMGSENSOR_HW_POWER_SEQ    *ppwr_seq = ppower_sequence;
	struct IMGSENSOR_HW_POWER_INFO   *ppwr_info;
	struct IMGSENSOR_HW_DEVICE       *pdev;
	int                               pin_cnt = 0;

	static DEFINE_RATELIMIT_STATE(ratelimit, 1 * HZ, 30);

#ifdef CONFIG_FPGA_EARLY_PORTING  /*for FPGA*/
	if (1) {
		PK_DBG("FPGA return true for power control\n");
		return IMGSENSOR_RETURN_SUCCESS;
	}
#endif
    curr_sensor_id = sensor_idx; // prize add by zhuzhengjiang for camera 20220110 start
	while (ppwr_seq < ppower_sequence + IMGSENSOR_HW_SENSOR_MAX_NUM &&
		ppwr_seq->name != NULL) {
		if (!strcmp(ppwr_seq->name, PLATFORM_POWER_SEQ_NAME)) {
			if (sensor_idx == ppwr_seq->_idx)
				break;
		} else {
			if (!strcmp(ppwr_seq->name, pcurr_idx))
				break;
		}
		ppwr_seq++;
	}

	if (ppwr_seq->name == NULL)
		return IMGSENSOR_RETURN_ERROR;

	ppwr_info = ppwr_seq->pwr_info;

	while (ppwr_info->pin != IMGSENSOR_HW_PIN_NONE &&
	       ppwr_info->pin < IMGSENSOR_HW_PIN_MAX_NUM &&
	       ppwr_info < ppwr_seq->pwr_info + IMGSENSOR_HW_POWER_INFO_MAX) {

		if (pwr_status == IMGSENSOR_HW_POWER_STATUS_ON) {
			if (ppwr_info->pin != IMGSENSOR_HW_PIN_UNDEF) {
				bool mindone_skip_step = false;
				const char *mindone_skip_reason = NULL;

				if (mindone_pwr_skip_mclk &&
					ppwr_info->pin == IMGSENSOR_HW_PIN_MCLK) {
					mindone_skip_step = true;
					mindone_skip_reason = "mindone_pwr_skip_mclk=1";
				} else if (mindone_rst_only &&
					!(ppwr_info->pin == IMGSENSOR_HW_PIN_RST &&
					  ppwr_info->pin_state_on ==
					  IMGSENSOR_HW_PIN_STATE_LEVEL_HIGH)) {
					mindone_skip_step = true;
					mindone_skip_reason = "mindone_rst_only=1, not the RST=HIGH step";
				}

				if (mindone_skip_step) {
					pr_debug("MINDONE-CAM-PWR: ON  step=%d sensor_idx=%d pin=%d state=%d SKIP (%s)\n",
						pin_cnt, sensor_idx, ppwr_info->pin,
						ppwr_info->pin_state_on, mindone_skip_reason);
				} else if (psensor_pwr->id[ppwr_info->pin] != IMGSENSOR_HW_ID_MAX_NUM) {
					pdev = phw->pdev[psensor_pwr->id[ppwr_info->pin]];

					if (__ratelimit(&ratelimit))
						PK_DBG(
						"sensor_idx %d, ppwr_info->pin %d, ppwr_info->pin_state_on %d, delay %u",
						sensor_idx,
						ppwr_info->pin,
						ppwr_info->pin_state_on,
						ppwr_info->pin_on_delay);

					/* MINDONE-CAM-PWR 29.08: pr_debug above is a
					 * silent no-op on this build -- pr_info always
					 * prints, no dynamic_debug needed.
					 */
					pr_debug("MINDONE-CAM-PWR: ON  step=%d sensor_idx=%d pin=%d id=%d state=%d delay=%u set=%s\n",
						pin_cnt, sensor_idx, ppwr_info->pin,
						psensor_pwr->id[ppwr_info->pin],
						ppwr_info->pin_state_on,
						ppwr_info->pin_on_delay,
						(pdev->set != NULL) ? "yes" : "NULL");

					if (pdev->set != NULL)
						pdev->set(
							pdev->pinstance,
							sensor_idx,
							ppwr_info->pin,
							ppwr_info->pin_state_on);
				} else {
					pr_debug("MINDONE-CAM-PWR: ON  step=%d sensor_idx=%d pin=%d id=NONE -- SKIPPED (no device)\n",
						pin_cnt, sensor_idx, ppwr_info->pin);
				}
			}

			mdelay(ppwr_info->pin_on_delay);

			/* MINDONE-CAM-BISECT: stop the ON sequence right here, after
			 * this step's mdelay, so timing up to the stop point matches
			 * a real run. The OFF branch is a separate call, never reached
			 * from here, and always runs its full reverse sequence later. */
			if (pin_cnt >= mindone_pwr_stop) {
				pr_debug("MINDONE-CAM-PWR: STOP after step=%d pin=%d sensor_idx=%d (mindone_pwr_stop=%d)\n",
					pin_cnt, ppwr_info->pin, sensor_idx,
					mindone_pwr_stop);
				return IMGSENSOR_RETURN_SUCCESS;
			}
		}

		ppwr_info++;
		pin_cnt++;
	}

	if (pwr_status == IMGSENSOR_HW_POWER_STATUS_OFF) {
		while (pin_cnt) {
			ppwr_info--;
			pin_cnt--;

			if (ppwr_info->pin != IMGSENSOR_HW_PIN_UNDEF) {
				if (psensor_pwr->id[ppwr_info->pin] != IMGSENSOR_HW_ID_MAX_NUM) {
					pdev = phw->pdev[psensor_pwr->id[ppwr_info->pin]];

					if (__ratelimit(&ratelimit))
						PK_DBG(
						"sensor_idx %d, ppwr_info->pin %d, ppwr_info->pin_state_off %d, delay %u",
						sensor_idx,
						ppwr_info->pin,
						ppwr_info->pin_state_off,
						ppwr_info->pin_on_delay);

					pr_debug("MINDONE-CAM-PWR: OFF sensor_idx=%d pin=%d id=%d state=%d delay=%u set=%s\n",
						sensor_idx, ppwr_info->pin,
						psensor_pwr->id[ppwr_info->pin],
						ppwr_info->pin_state_off,
						ppwr_info->pin_on_delay,
						(pdev->set != NULL) ? "yes" : "NULL");

					if (pdev->set != NULL)
						pdev->set(
							pdev->pinstance,
							sensor_idx,
							ppwr_info->pin,
							ppwr_info->pin_state_off);
				} else {
					pr_debug("MINDONE-CAM-PWR: OFF sensor_idx=%d pin=%d id=NONE -- SKIPPED (no device)\n",
						sensor_idx, ppwr_info->pin);
				}
			}

			mdelay(ppwr_info->pin_on_delay);
		}
	}

	return IMGSENSOR_RETURN_SUCCESS;
}

enum IMGSENSOR_RETURN imgsensor_hw_power(
		struct IMGSENSOR_HW *phw,
		struct IMGSENSOR_SENSOR *psensor,
		enum IMGSENSOR_HW_POWER_STATUS pwr_status)
{
	int ret = 0;
	enum IMGSENSOR_SENSOR_IDX sensor_idx = psensor->inst.sensor_idx;
	char *curr_sensor_name = psensor->inst.psensor_list->name;
	char str_index[LENGTH_FOR_SNPRINTF];

	PK_DBG("sensor_idx %d, power %d curr_sensor_name %s, enable list %s\n",
		sensor_idx,
		pwr_status,
		curr_sensor_name,
		phw->enable_sensor_by_index[(uint32_t)sensor_idx] == NULL
		? "NULL"
		: phw->enable_sensor_by_index[(uint32_t)sensor_idx]);

	if (phw->enable_sensor_by_index[(uint32_t)sensor_idx] &&
	!strstr(phw->enable_sensor_by_index[(uint32_t)sensor_idx], curr_sensor_name))
		return IMGSENSOR_RETURN_ERROR;

	ret = snprintf(str_index, sizeof(str_index), "%d", sensor_idx);
	if (ret < 0) {
		PK_DBG("Error! snprintf allocate 0");
		ret = IMGSENSOR_RETURN_ERROR;
		return ret;
	}
	if (IS_MT6873(phw->g_platform_id) || IS_MT6853(phw->g_platform_id))
		imgsensor_hw_power_sequence(
				phw,
				sensor_idx,
				pwr_status,
				platform_power_sequence_for_mipi_switch,
				str_index);
	else if (IS_MT6833(phw->g_platform_id))
		imgsensor_hw_power_sequence(
				phw,
				sensor_idx,
				pwr_status,
				platform_power_sequence_for_mt6833,
				str_index);
	else
		imgsensor_hw_power_sequence(
				phw,
				sensor_idx,
				pwr_status,
				platform_power_sequence,
				str_index);

	imgsensor_hw_power_sequence(
			phw,
			sensor_idx,
			pwr_status, sensor_power_sequence, curr_sensor_name);

	return IMGSENSOR_RETURN_SUCCESS;
}

enum IMGSENSOR_RETURN imgsensor_hw_dump(struct IMGSENSOR_HW *phw)
{
	int i;

	for (i = 0; i < IMGSENSOR_HW_ID_MAX_NUM; i++) {
		if (phw->pdev[i]->dump != NULL)
			(phw->pdev[i]->dump)(phw->pdev[i]->pinstance);
	}
	return IMGSENSOR_RETURN_SUCCESS;
}


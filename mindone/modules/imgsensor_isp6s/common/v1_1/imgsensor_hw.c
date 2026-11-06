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
						ppwr_info->id = j;
						break;
					}
				}
			} else {
				PK_DBG("NOTICE: imgsensor_hw_cfg hw_pin:%s, dts override absent, keep default id:%d\n",
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

static enum IMGSENSOR_RETURN imgsensor_hw_power_step(
		struct IMGSENSOR_HW *phw,
		enum IMGSENSOR_SENSOR_IDX sensor_idx,
		struct IMGSENSOR_HW_POWER_INFO *info,
		enum IMGSENSOR_HW_POWER_STATUS status)
{
	struct IMGSENSOR_HW_DEVICE *device = NULL;
	enum IMGSENSOR_HW_ID id;
	enum IMGSENSOR_RETURN ret = IMGSENSOR_RETURN_SUCCESS;

	if (info->pin != IMGSENSOR_HW_PIN_UNDEF) {
		if (info->pin <= IMGSENSOR_HW_PIN_NONE ||
		    info->pin >= IMGSENSOR_HW_PIN_MAX_NUM)
			return IMGSENSOR_RETURN_ERROR;
		id = phw->sensor_pwr[sensor_idx].id[info->pin];
		if (id != IMGSENSOR_HW_ID_MAX_NUM) {
			if ((unsigned int)id >= IMGSENSOR_HW_ID_MAX_NUM)
				return IMGSENSOR_RETURN_ERROR;
			device = phw->pdev[id];
			if (!device || !device->set)
				return IMGSENSOR_RETURN_ERROR;
			ret = device->set(device->pinstance, sensor_idx, info->pin,
				status == IMGSENSOR_HW_POWER_STATUS_ON ?
				info->pin_state_on : info->pin_state_off);
		}
	}
	mdelay(info->pin_on_delay);
	if (ret != IMGSENSOR_RETURN_SUCCESS &&
	    status == IMGSENSOR_HW_POWER_STATUS_ON && device &&
	    (device->id == IMGSENSOR_HW_ID_GPIO ||
	     device->id == IMGSENSOR_HW_ID_MCLK)) {
		device->set(device->pinstance, sensor_idx, info->pin,
			info->pin_state_off);
		mdelay(info->pin_on_delay);
	}
	return ret;
}

static enum IMGSENSOR_RETURN imgsensor_hw_power_sequence(
		struct IMGSENSOR_HW *phw,
		enum IMGSENSOR_SENSOR_IDX sensor_idx,
		enum IMGSENSOR_HW_POWER_STATUS pwr_status,
		struct IMGSENSOR_HW_POWER_SEQ *sequence,
		char *name, bool optional)
{
	struct IMGSENSOR_HW_POWER_SEQ *seq = sequence;
	enum IMGSENSOR_RETURN ret = IMGSENSOR_RETURN_SUCCESS;
	enum IMGSENSOR_RETURN step_ret;
	int count = 0;
	int i;

#ifdef CONFIG_FPGA_EARLY_PORTING
	return IMGSENSOR_RETURN_SUCCESS;
#endif
	if ((unsigned int)sensor_idx >= IMGSENSOR_SENSOR_IDX_MAX_NUM)
		return IMGSENSOR_RETURN_ERROR;
	curr_sensor_id = sensor_idx;
	for (i = 0; i < IMGSENSOR_HW_SENSOR_MAX_NUM; i++) {
		seq = &sequence[i];
		if (!seq->name)
			break;
		if (!strcmp(seq->name, PLATFORM_POWER_SEQ_NAME) ?
		    sensor_idx == seq->_idx : !strcmp(seq->name, name))
			break;
	}
	if (i == IMGSENSOR_HW_SENSOR_MAX_NUM || !seq->name)
		return optional ? IMGSENSOR_RETURN_SUCCESS : IMGSENSOR_RETURN_ERROR;

	while (count < IMGSENSOR_HW_POWER_INFO_MAX &&
	       seq->pwr_info[count].pin != IMGSENSOR_HW_PIN_NONE)
		count++;

	if (pwr_status == IMGSENSOR_HW_POWER_STATUS_ON) {
		for (i = 0; i < count; i++) {
			ret = imgsensor_hw_power_step(phw, sensor_idx,
				&seq->pwr_info[i], IMGSENSOR_HW_POWER_STATUS_ON);
			if (ret != IMGSENSOR_RETURN_SUCCESS) {
				while (i > 0) {
					i--;
					imgsensor_hw_power_step(phw, sensor_idx,
						&seq->pwr_info[i], IMGSENSOR_HW_POWER_STATUS_OFF);
				}
				return ret;
			}
		}
	} else {
		for (i = count; i > 0; i--) {
			step_ret = imgsensor_hw_power_step(phw, sensor_idx,
				&seq->pwr_info[i - 1], IMGSENSOR_HW_POWER_STATUS_OFF);
			if (ret == IMGSENSOR_RETURN_SUCCESS)
				ret = step_ret;
		}
	}
	return ret;
}

enum IMGSENSOR_RETURN imgsensor_hw_power(
		struct IMGSENSOR_HW *phw,
		struct IMGSENSOR_SENSOR *psensor,
		enum IMGSENSOR_HW_POWER_STATUS pwr_status)
{
	struct IMGSENSOR_HW_POWER_SEQ *platform_sequence;
	enum IMGSENSOR_SENSOR_IDX sensor_idx;
	enum IMGSENSOR_RETURN platform_ret;
	enum IMGSENSOR_RETURN sensor_ret;
	char *name;
	char str_index[LENGTH_FOR_SNPRINTF];

	if (!phw || !psensor || !psensor->inst.psensor_list ||
	    (pwr_status != IMGSENSOR_HW_POWER_STATUS_ON &&
	     pwr_status != IMGSENSOR_HW_POWER_STATUS_OFF))
		return IMGSENSOR_RETURN_ERROR;
	sensor_idx = psensor->inst.sensor_idx;
	if ((unsigned int)sensor_idx >= IMGSENSOR_SENSOR_IDX_MAX_NUM)
		return IMGSENSOR_RETURN_ERROR;
	name = psensor->inst.psensor_list->name;
	if (phw->enable_sensor_by_index[sensor_idx] &&
	    !strstr(phw->enable_sensor_by_index[sensor_idx], name))
		return IMGSENSOR_RETURN_ERROR;
	if (snprintf(str_index, sizeof(str_index), "%d", sensor_idx) < 0)
		return IMGSENSOR_RETURN_ERROR;

	if (IS_MT6873(phw->g_platform_id) || IS_MT6853(phw->g_platform_id))
		platform_sequence = platform_power_sequence_for_mipi_switch;
	else if (IS_MT6833(phw->g_platform_id))
		platform_sequence = platform_power_sequence_for_mt6833;
	else
		platform_sequence = platform_power_sequence;

	platform_ret = imgsensor_hw_power_sequence(phw, sensor_idx, pwr_status,
		platform_sequence, str_index, true);
	if (pwr_status == IMGSENSOR_HW_POWER_STATUS_ON &&
	    platform_ret != IMGSENSOR_RETURN_SUCCESS)
		return platform_ret;

	sensor_ret = imgsensor_hw_power_sequence(phw, sensor_idx, pwr_status,
		sensor_power_sequence, name, false);
	if (pwr_status == IMGSENSOR_HW_POWER_STATUS_ON &&
	    sensor_ret != IMGSENSOR_RETURN_SUCCESS)
		imgsensor_hw_power_sequence(phw, sensor_idx,
			IMGSENSOR_HW_POWER_STATUS_OFF, platform_sequence, str_index, true);

	return platform_ret != IMGSENSOR_RETURN_SUCCESS ? platform_ret : sensor_ret;
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


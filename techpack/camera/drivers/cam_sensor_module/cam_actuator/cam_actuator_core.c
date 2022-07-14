// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 */

#include <linux/module.h>
#include <cam_sensor_cmn_header.h>
#include "cam_actuator_core.h"
#include "cam_sensor_util.h"
#include "cam_trace.h"
#include "cam_common_util.h"
#include "cam_packet_util.h"
#if defined(CONFIG_SAMSUNG_OIS_MCU_STM32)
#include "cam_ois_core.h"
#include "cam_ois_mcu_stm32g.h"
#endif


#if defined(CONFIG_SAMSUNG_OIS_MCU_STM32)
extern struct cam_ois_ctrl_t *g_o_ctrl;
#endif

#if defined(CONFIG_SAMSUNG_HW_SOFTLANDING)
#define ACTUATOR_IDLE 0x0
#define ACTUATOR_BUSY 0x1
#endif

int32_t cam_actuator_construct_default_power_setting(
	struct cam_sensor_power_ctrl_t *power_info)
{
	int rc = 0;

	power_info->power_setting_size = 1;
	power_info->power_setting =
		kzalloc(sizeof(struct cam_sensor_power_setting),
			GFP_KERNEL);
	if (!power_info->power_setting)
		return -ENOMEM;

	power_info->power_setting[0].seq_type = SENSOR_VIO;
	power_info->power_setting[0].seq_val = CAM_VIO;
	power_info->power_setting[0].config_val = 1;
	power_info->power_setting[0].delay = 1;

	power_info->power_down_setting_size = 1;
	power_info->power_down_setting =
		kzalloc(sizeof(struct cam_sensor_power_setting),
			GFP_KERNEL);
	if (!power_info->power_down_setting) {
		rc = -ENOMEM;
		goto free_power_settings;
	}
	
	power_info->power_down_setting[0].seq_type = SENSOR_VIO;
	power_info->power_down_setting[0].seq_val = CAM_VIO;
	power_info->power_down_setting[0].config_val = 0;

	return rc;

free_power_settings:
	kfree(power_info->power_setting);
	power_info->power_setting = NULL;
	power_info->power_setting_size = 0;
	return rc;
}

int32_t cam_actuator_power_up(struct cam_actuator_ctrl_t *a_ctrl)
{
	int rc = 0;
	struct cam_hw_soc_info  *soc_info =
		&a_ctrl->soc_info;
	struct cam_actuator_soc_private  *soc_private;
	struct cam_sensor_power_ctrl_t *power_info;

	soc_private =
		(struct cam_actuator_soc_private *)a_ctrl->soc_info.soc_private;
	power_info = &soc_private->power_info;

	if ((power_info->power_setting == NULL) &&
		(power_info->power_down_setting == NULL)) {
		CAM_INFO(CAM_ACTUATOR,
			"Using default power settings");
		rc = cam_actuator_construct_default_power_setting(power_info);
		if (rc < 0) {
			CAM_ERR(CAM_ACTUATOR,
				"Construct default actuator power setting failed.");
			return rc;
		}
	}

	/* Parse and fill vreg params for power up settings */
	rc = msm_camera_fill_vreg_params(
		&a_ctrl->soc_info,
		power_info->power_setting,
		power_info->power_setting_size);
	if (rc) {
		CAM_ERR(CAM_ACTUATOR,
			"failed to fill vreg params for power up rc:%d", rc);
		return rc;
	}

	/* Parse and fill vreg params for power down settings*/
	rc = msm_camera_fill_vreg_params(
		&a_ctrl->soc_info,
		power_info->power_down_setting,
		power_info->power_down_setting_size);
	if (rc) {
		CAM_ERR(CAM_ACTUATOR,
			"failed to fill vreg params power down rc:%d", rc);
		return rc;
	}

	power_info->dev = soc_info->dev;

	rc = cam_sensor_core_power_up(power_info, soc_info);
	if (rc) {
		CAM_ERR(CAM_ACTUATOR,
			"failed in actuator power up rc %d", rc);
		return rc;
	}

	rc = camera_io_init(&a_ctrl->io_master_info);
	if (rc < 0)
		CAM_ERR(CAM_ACTUATOR, "cci init failed: rc: %d", rc);

	return rc;
}

#if defined(CONFIG_SAMSUNG_HW_SOFTLANDING)
int32_t cam_actuator_i2c_read(struct cam_actuator_ctrl_t *a_ctrl, uint32_t addr,
		uint32_t *data,
        enum camera_sensor_i2c_type addr_type,
        enum camera_sensor_i2c_type data_type)
{
	uint32_t rc = 0;

	if (a_ctrl == NULL) {
		CAM_ERR(CAM_ACTUATOR, "failed. a_ctrl is NULL");
		return -EINVAL;
	}

	rc = camera_io_dev_read(&a_ctrl->io_master_info, addr,
		(uint32_t *)data, addr_type, data_type);
	if (rc < 0) {
		CAM_ERR(CAM_ACTUATOR, "Failed to read 0x%x", addr);
	}

	return rc;
}

int32_t cam_actuator_i2c_write(struct cam_actuator_ctrl_t *a_ctrl, uint32_t reg_addr,
		uint32_t reg_data, uint32_t data_type)
{
	struct cam_sensor_i2c_reg_setting reg_setting;
	struct cam_sensor_i2c_reg_array reg_arr;
	int rc = 0;

	memset(&reg_setting, 0, sizeof(reg_setting));
	memset(&reg_arr, 0, sizeof(reg_arr));

	if (a_ctrl == NULL) {
		CAM_ERR(CAM_ACTUATOR, "failed. a_ctrl is NULL");
		return -EINVAL;
	}

	reg_setting.size = 1;
	reg_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
	reg_setting.data_type = data_type;
	reg_setting.reg_setting = &reg_arr;

	reg_arr.reg_addr = reg_addr;
	reg_arr.reg_data = reg_data;
	rc = camera_io_dev_write(&a_ctrl->io_master_info, &reg_setting);

	if (rc < 0) {
 		CAM_ERR(CAM_ACTUATOR, "Failed to random write I2C settings for reg:0x%x data:0x%x err:%d", reg_addr, reg_data, rc);
	}

	return rc;
}

int32_t cam_actuator_get_status(struct cam_actuator_ctrl_t *a_ctrl, uint16_t *info)
{
	int32_t rc = 0;
	uint32_t val = 0;

	rc = cam_actuator_i2c_read(a_ctrl, 0x05, &val, CAMERA_SENSOR_I2C_TYPE_BYTE, CAMERA_SENSOR_I2C_TYPE_BYTE);
	if (rc < 0) {
		CAM_ERR(CAM_ACTUATOR, "get status i2c read fail:%d", rc);
		return -EINVAL;
	}

	*info = ((val & 0x03) == 0) ? ACTUATOR_IDLE : ACTUATOR_BUSY;

	return rc;
}

void cam_actuator_busywait(struct cam_actuator_ctrl_t *a_ctrl)
{
	uint16_t info = 0, status_check_count = 0;
	int32_t rc = 0;

	CAM_INFO(CAM_ACTUATOR, "before to check status");
	do {
		rc = cam_actuator_get_status(a_ctrl, &info);
		if (info == ACTUATOR_BUSY || rc < 0) {
			CAM_INFO(CAM_ACTUATOR, "Busy rc: %d", rc);
#if defined(CONFIG_SEC_A42XQ_PROJECT) || defined(CONFIG_SEC_A52XQ_PROJECT) || defined(CONFIG_SEC_A42XUQ_PROJECT)
			msleep(10);
#else
			msleep(5);
#endif
		}
	       status_check_count++;
	} while (info && status_check_count < 8);

	if(status_check_count == 8)
	   CAM_ERR(CAM_ACTUATOR, "status check failed");
	else
	   CAM_INFO(CAM_ACTUATOR, "Idle");
}

#if defined(CONFIG_SEC_A42XQ_PROJECT) || defined(CONFIG_SEC_A52XQ_PROJECT) || defined(CONFIG_SEC_A42XUQ_PROJECT)
void cam_actuator_check_pwr(
		struct cam_hw_soc_info *soc_info)
{
	int val = 0, i = 0;
	uint8_t size = 0;
	struct cam_soc_gpio_data *gpio_conf =
			soc_info->gpio_data;
	struct gpio *gpio_tbl = NULL;

	if (!gpio_conf) {
		CAM_INFO(CAM_SENSOR, "No GPIO data");
		return;
	}

	if (gpio_conf->cam_gpio_common_tbl_size <= 0) {
		CAM_INFO(CAM_SENSOR, "No GPIO entry");
		return;
	}

	gpio_tbl = gpio_conf->cam_gpio_req_tbl;
	size = gpio_conf->cam_gpio_req_tbl_size;

	if (!gpio_tbl || !size) {
		CAM_ERR(CAM_SENSOR, "invalid gpio_tbl %pK / size %d",
			gpio_tbl, size);
		return;
	}

	for (i = 0; i < size; i++) {
		val = gpio_get_value_cansleep(gpio_tbl[i].gpio);
		CAM_ERR(CAM_SENSOR, "gpio[%d]= %d:%s dir: %ld value: %d",
			i, gpio_tbl[i].gpio, gpio_tbl[i].label, gpio_tbl[i].flags, val);
	}
}

int32_t cam_actuator_do_soft_landing(struct cam_actuator_ctrl_t *a_ctrl)
{
	int32_t rc = 0;
	uint32_t pos1, pos2;
	uint32_t position;
	uint32_t reg_data;

	CAM_DBG(CAM_ACTUATOR, "cam_actuator_do_soft_landing() Entry ");

	// Check if IC is off
	cam_actuator_busywait(a_ctrl);
	rc = cam_actuator_i2c_read(a_ctrl, 0x02, &reg_data, CAMERA_SENSOR_I2C_TYPE_BYTE, CAMERA_SENSOR_I2C_TYPE_BYTE);
	if (rc < 0) {
		CAM_ERR(CAM_ACTUATOR, "IC status - i2c read fail err:%d", rc);
		cam_actuator_check_pwr(&a_ctrl->soc_info);
		return 0;
	}

	if ((reg_data & 0x01) == 0x01) {
		CAM_DBG(CAM_ACTUATOR, "park lens skip for dev:0x%x reg[0x02]:0x%x", a_ctrl->io_master_info.client->addr, reg_data);
		return rc;
	}

	// read DAC value to get position of lens
	rc = cam_actuator_i2c_read(a_ctrl, 0x03, &pos1, CAMERA_SENSOR_I2C_TYPE_BYTE, CAMERA_SENSOR_I2C_TYPE_BYTE);
	if (rc < 0) {
		CAM_ERR(CAM_ACTUATOR, "det pos1 - i2c read fail err:%d", rc);
		return 0;
	}

	rc = cam_actuator_i2c_read(a_ctrl, 0x04, &pos2, CAMERA_SENSOR_I2C_TYPE_BYTE, CAMERA_SENSOR_I2C_TYPE_BYTE);
	if (rc < 0) {
		CAM_ERR(CAM_ACTUATOR, "det pos2 - i2c read fail err:%d", rc);
		return 0;
	}

	// PRESET initial position
	pos1 = pos1 & 0x03;
	position = ((uint16_t)pos1 << 8) | pos2;

	CAM_INFO(CAM_ACTUATOR, "current position:%d ", position);

	/*Max position is 1023, keep half of max. lens position*/
	if( position > 512 ) {
		position = 512;

		rc = cam_actuator_i2c_write(a_ctrl, 0x03, position - 1, CAMERA_SENSOR_I2C_TYPE_WORD);
		if (rc < 0) {
			CAM_ERR(CAM_ACTUATOR, "preset register - i2c write fail err:%d", rc);
			return 0;
		}

		cam_actuator_busywait(a_ctrl);
		CAM_INFO(CAM_ACTUATOR, "current position is set to :%d ", position);
	}
	rc = cam_actuator_i2c_write(a_ctrl, 0x0A,  ((position >> 1) - 1), CAMERA_SENSOR_I2C_TYPE_BYTE);
	if (rc < 0) {
		CAM_ERR(CAM_ACTUATOR, "preset register - i2c write fail err:%d", rc);
		return 0;
	}

	CAM_INFO(CAM_ACTUATOR, "preset initial position:%d ", position);

	// NRC Time Setting
	cam_actuator_i2c_write(a_ctrl, 0x0C, 0x85,CAMERA_SENSOR_I2C_TYPE_BYTE);
	if (rc < 0) {
		CAM_ERR(CAM_ACTUATOR, "nrc timing issue- i2c write fail err:%d", rc);
		return 0;
	}

	// Enable - softlanding
	cam_actuator_i2c_write(a_ctrl, 0x0B, 0x01,CAMERA_SENSOR_I2C_TYPE_BYTE);
	if (rc < 0) {
		CAM_ERR(CAM_ACTUATOR, "softlanding register configuration failed, rc:%d", rc);
		return 0;
	}

	// Check if busy -> wait
	cam_actuator_busywait(a_ctrl);
	CAM_DBG(CAM_ACTUATOR, "cam_actuator_do_soft_landing() Exit ");

	return rc;
}
#else
int32_t cam_actuator_do_soft_landing(struct cam_actuator_ctrl_t *a_ctrl)
{
	int32_t rc = 0;
	uint32_t pos1, pos2;
	uint32_t position;
	uint32_t reg_data;
	int32_t interval = 50;
	int32_t i;

	CAM_DBG(CAM_ACTUATOR, "cam_actuator_do_soft_landing() Entry ");

	// Check if IC is off
	cam_actuator_busywait(a_ctrl);
	rc = cam_actuator_i2c_read(a_ctrl, 0x02, &reg_data, CAMERA_SENSOR_I2C_TYPE_BYTE, CAMERA_SENSOR_I2C_TYPE_BYTE);
	if (rc < 0) {
		CAM_ERR(CAM_ACTUATOR, "IC status - i2c read fail err:%d", rc);
		return -EINVAL;
	}

	if ((reg_data & 0x01) == 0x01) {
		CAM_ERR(CAM_ACTUATOR, "park lens skip for dev:0x%x reg[0x02]:0x%x", a_ctrl->io_master_info.client->addr, reg_data);
		return rc;
	}

	// read DAC value to get position of lens
	rc = cam_actuator_i2c_read(a_ctrl, 0x03, &pos1, CAMERA_SENSOR_I2C_TYPE_BYTE, CAMERA_SENSOR_I2C_TYPE_BYTE);
	if (rc < 0) {
		CAM_ERR(CAM_ACTUATOR, "det pos1 - i2c read fail err:%d", rc);
		return -EINVAL;
	}

	rc = cam_actuator_i2c_read(a_ctrl, 0x04, &pos2, CAMERA_SENSOR_I2C_TYPE_BYTE, CAMERA_SENSOR_I2C_TYPE_BYTE);
	if (rc < 0) {
		CAM_ERR(CAM_ACTUATOR, "det pos2 - i2c read fail err:%d", rc);
		return -EINVAL;
	}

	// PRESET initial position
	pos1 = pos1 & 0x03;
	position = ((uint16_t)pos1 << 8) | pos2;
	interval = (int32_t) (512 - position) / 3;
	
	CAM_INFO(CAM_ACTUATOR, "current position:%d ", position);
	CAM_INFO(CAM_ACTUATOR, "interval:%d ", interval);
	
	for(i = 0; i < 3; i++) {
		position += interval;
		rc = cam_actuator_i2c_write(a_ctrl, 0x03, position, CAMERA_SENSOR_I2C_TYPE_WORD);
		if (rc < 0) {
			CAM_ERR(CAM_ACTUATOR, "preset register - i2c write fail err:%d", rc);
			return -EINVAL;
		}

		cam_actuator_busywait(a_ctrl);
		CAM_ERR(CAM_ACTUATOR, "current position is set to :%d ", position);
	}
	CAM_DBG(CAM_ACTUATOR, "cam_actuator_do_soft_landing() Exit ");

	return rc;
}
#endif
#endif

int32_t cam_actuator_power_down(struct cam_actuator_ctrl_t *a_ctrl)
{
	int32_t rc = 0;
	struct cam_sensor_power_ctrl_t *power_info;
	struct cam_hw_soc_info *soc_info = &a_ctrl->soc_info;
	struct cam_actuator_soc_private  *soc_private;

	if (!a_ctrl) {
		CAM_ERR(CAM_ACTUATOR, "failed: a_ctrl %pK", a_ctrl);
		return -EINVAL;
	}

#if defined(CONFIG_SAMSUNG_HW_SOFTLANDING)
	rc = cam_actuator_do_soft_landing(a_ctrl);
	if (rc < 0) {
		CAM_ERR(CAM_ACTUATOR, "actuator soft landing is failed:%d", rc);
		// Even if Soft landing fails, we must Power_down
	}
#endif

	soc_private =
		(struct cam_actuator_soc_private *)a_ctrl->soc_info.soc_private;
	power_info = &soc_private->power_info;
	soc_info = &a_ctrl->soc_info;

	if (!power_info) {
		CAM_ERR(CAM_ACTUATOR, "failed: power_info %pK", power_info);
		return -EINVAL;
	}
	rc = cam_sensor_util_power_down(power_info, soc_info);
	if (rc) {
		CAM_ERR(CAM_ACTUATOR, "power down the core is failed:%d", rc);
		return rc;
	}

	camera_io_release(&a_ctrl->io_master_info);

	return rc;
}

static int32_t cam_actuator_i2c_modes_util(
	struct camera_io_master *io_master_info,
	struct i2c_settings_list *i2c_list)
{
	int32_t rc = 0;
	uint32_t i, size;

	if (i2c_list->op_code == CAM_SENSOR_I2C_WRITE_RANDOM) {
		rc = camera_io_dev_write(io_master_info,
			&(i2c_list->i2c_settings));
		if (rc < 0) {
			CAM_ERR(CAM_ACTUATOR,
				"Failed to random write I2C settings: %d",
				rc);
			return rc;
		}
	} else if (i2c_list->op_code == CAM_SENSOR_I2C_WRITE_SEQ) {
		rc = camera_io_dev_write_continuous(
			io_master_info,
			&(i2c_list->i2c_settings),
			0);
		if (rc < 0) {
			CAM_ERR(CAM_ACTUATOR,
				"Failed to seq write I2C settings: %d",
				rc);
			return rc;
			}
	} else if (i2c_list->op_code == CAM_SENSOR_I2C_WRITE_BURST) {
		rc = camera_io_dev_write_continuous(
			io_master_info,
			&(i2c_list->i2c_settings),
			1);
		if (rc < 0) {
			CAM_ERR(CAM_ACTUATOR,
				"Failed to burst write I2C settings: %d",
				rc);
			return rc;
		}
	} else if (i2c_list->op_code == CAM_SENSOR_I2C_POLL) {
		size = i2c_list->i2c_settings.size;
		for (i = 0; i < size; i++) {
			rc = camera_io_dev_poll(
			io_master_info,
			i2c_list->i2c_settings.reg_setting[i].reg_addr,
			i2c_list->i2c_settings.reg_setting[i].reg_data,
			i2c_list->i2c_settings.reg_setting[i].data_mask,
			i2c_list->i2c_settings.addr_type,
			i2c_list->i2c_settings.data_type,
			i2c_list->i2c_settings.reg_setting[i].delay);
			if (rc < 0) {
				CAM_ERR(CAM_ACTUATOR,
					"i2c poll apply setting Fail: %d", rc);
				return rc;
			}
		}
	}

	return rc;
}

int32_t cam_actuator_slaveInfo_pkt_parser(struct cam_actuator_ctrl_t *a_ctrl,
	uint32_t *cmd_buf, size_t len)
{
	int32_t rc = 0;
	struct cam_cmd_i2c_info *i2c_info;

	if (!a_ctrl || !cmd_buf || (len < sizeof(struct cam_cmd_i2c_info))) {
		CAM_ERR(CAM_ACTUATOR, "Invalid Args");
		return -EINVAL;
	}

	i2c_info = (struct cam_cmd_i2c_info *)cmd_buf;
	if (a_ctrl->io_master_info.master_type == CCI_MASTER) {
		a_ctrl->io_master_info.cci_client->cci_i2c_master =
			a_ctrl->cci_i2c_master;
		a_ctrl->io_master_info.cci_client->i2c_freq_mode =
			i2c_info->i2c_freq_mode;
		a_ctrl->io_master_info.cci_client->sid =
			i2c_info->slave_addr >> 1;
		CAM_DBG(CAM_ACTUATOR, "Slave addr: 0x%x Freq Mode: %d",
			i2c_info->slave_addr, i2c_info->i2c_freq_mode);
	} else if (a_ctrl->io_master_info.master_type == I2C_MASTER) {
		a_ctrl->io_master_info.client->addr = i2c_info->slave_addr;
		CAM_DBG(CAM_ACTUATOR, "Slave addr: 0x%x", i2c_info->slave_addr);
	} else {
		CAM_ERR(CAM_ACTUATOR, "Invalid Master type: %d",
			a_ctrl->io_master_info.master_type);
		 rc = -EINVAL;
	}

	return rc;
}

int32_t cam_actuator_apply_settings(struct cam_actuator_ctrl_t *a_ctrl,
	struct i2c_settings_array *i2c_set)
{
	struct i2c_settings_list *i2c_list;
	int32_t rc = 0;
	
#if defined(CONFIG_SAMSUNG_OIS_MCU_STM32)
	int position = -1;
	int size = 0;
	int i = 0;
#endif
	
#if defined(CONFIG_USE_CAMERA_HW_BIG_DATA)
	struct cam_hw_param *hw_param = NULL;
#endif

	if (a_ctrl == NULL || i2c_set == NULL) {
		CAM_ERR(CAM_ACTUATOR, "Invalid Args");
		return -EINVAL;
	}

	if (i2c_set->is_settings_valid != 1) {
		CAM_ERR(CAM_ACTUATOR, " Invalid settings");
		return -EINVAL;
	}

	list_for_each_entry(i2c_list,
		&(i2c_set->list_head), list) {
		rc = cam_actuator_i2c_modes_util(
			&(a_ctrl->io_master_info),
			i2c_list);
		if (rc < 0) {
			CAM_ERR(CAM_ACTUATOR,
				"Failed to apply settings: %d",
				rc);
#if defined(CONFIG_USE_CAMERA_HW_BIG_DATA)
			if ((rc < 0) && (i2c_list->i2c_settings.reg_setting->reg_data == 0x0)) {
				if (a_ctrl != NULL) {
					switch (a_ctrl->soc_info.index) {
					case CAMERA_0:
						if (!msm_is_sec_get_rear_hw_param(&hw_param)) {
							if (hw_param != NULL) {
								CAM_ERR(CAM_HWB, "[R][AF] Err\n");
								hw_param->i2c_af_err_cnt++;
								hw_param->need_update_to_file = TRUE;
							}
						}
						break;

					case CAMERA_1:
						if (!msm_is_sec_get_front_hw_param(&hw_param)) {
							if (hw_param != NULL) {
								CAM_ERR(CAM_HWB, "[F][AF] Err\n");
								hw_param->i2c_af_err_cnt++;
								hw_param->need_update_to_file = TRUE;
							}
						}
						break;

#if defined(CONFIG_SAMSUNG_REAR_TRIPLE)
					case CAMERA_2:
						if (!msm_is_sec_get_rear3_hw_param(&hw_param)) {
							if (hw_param != NULL) {
								CAM_ERR(CAM_HWB, "[R3][AF] Err\n");
								hw_param->i2c_af_err_cnt++;
								hw_param->need_update_to_file = TRUE;
							}
						}
						break;
#endif

#if defined(CONFIG_SAMSUNG_SECURE_CAMERA)
					case CAMERA_3:
						if (!msm_is_sec_get_iris_hw_param(&hw_param)) {
							if (hw_param != NULL) {
								CAM_ERR(CAM_HWB, "[I][AF] Err\n");
								hw_param->i2c_af_err_cnt++;
								hw_param->need_update_to_file = TRUE;
							}
						}
						break;
#endif

					default:
						CAM_ERR(CAM_HWB, "[NON][AF] Unsupport\n");
						break;
					}
				}
			}
#endif
		} else {
			CAM_DBG(CAM_ACTUATOR,
				"Success:request ID: %d",
				i2c_set->request_id);
		}
		
#if defined(CONFIG_SAMSUNG_OIS_MCU_STM32)
		if (a_ctrl->soc_info.index == 0) { //apply on reear case
			size = i2c_list->i2c_settings.size;
			for (i = 0; i < size; i++) {
				if (i2c_list->i2c_settings.reg_setting[i].reg_addr == 0x00) {
					position = i2c_list->i2c_settings.reg_setting[i].reg_data >> 7; //using word data
					CAM_DBG(CAM_ACTUATOR, "Position : %d\n", position);
					break;
				}
			}
		}
#endif
 
	}

#if defined(CONFIG_SAMSUNG_OIS_MCU_STM32)
	if (a_ctrl->soc_info.index == 0) { //not apply on front case
		 if (g_o_ctrl != NULL) {
			 mutex_lock(&(g_o_ctrl->ois_mutex));
			 if (position >= 0 && position < 512)
				 // 1bit right shift af position, because OIS use 8bit af position
				 cam_ois_shift_calibration(g_o_ctrl, (position >> 1), a_ctrl->soc_info.index);
			 else
				 CAM_DBG(CAM_ACTUATOR, "Position is invalid %d \n", position);
			 mutex_unlock(&(g_o_ctrl->ois_mutex));
		}
	}
#endif


	return rc;
}

int32_t cam_actuator_apply_request(struct cam_req_mgr_apply_request *apply)
{
	int32_t rc = 0, request_id, del_req_id;
	struct cam_actuator_ctrl_t *a_ctrl = NULL;

	if (!apply) {
		CAM_ERR(CAM_ACTUATOR, "Invalid Input Args");
		return -EINVAL;
	}

	a_ctrl = (struct cam_actuator_ctrl_t *)
		cam_get_device_priv(apply->dev_hdl);
	if (!a_ctrl) {
		CAM_ERR(CAM_ACTUATOR, "Device data is NULL");
		return -EINVAL;
	}
	request_id = apply->request_id % MAX_PER_FRAME_ARRAY;

	trace_cam_apply_req("Actuator", apply->request_id);

	CAM_DBG(CAM_ACTUATOR, "Request Id: %lld", apply->request_id);
	mutex_lock(&(a_ctrl->actuator_mutex));
	if ((apply->request_id ==
		a_ctrl->i2c_data.per_frame[request_id].request_id) &&
		(a_ctrl->i2c_data.per_frame[request_id].is_settings_valid)
		== 1) {
		rc = cam_actuator_apply_settings(a_ctrl,
			&a_ctrl->i2c_data.per_frame[request_id]);
		if (rc < 0) {
			CAM_ERR(CAM_ACTUATOR,
				"Failed in applying the request: %lld\n",
				apply->request_id);
			goto release_mutex;
		}
	}
	del_req_id = (request_id +
		MAX_PER_FRAME_ARRAY - MAX_SYSTEM_PIPELINE_DELAY) %
		MAX_PER_FRAME_ARRAY;

	if (apply->request_id >
		a_ctrl->i2c_data.per_frame[del_req_id].request_id) {
		a_ctrl->i2c_data.per_frame[del_req_id].request_id = 0;
		rc = delete_request(&a_ctrl->i2c_data.per_frame[del_req_id]);
		if (rc < 0) {
			CAM_ERR(CAM_ACTUATOR,
				"Fail deleting the req: %d err: %d\n",
				del_req_id, rc);
			goto release_mutex;
		}
	} else {
		CAM_DBG(CAM_ACTUATOR, "No Valid Req to clean Up");
	}

release_mutex:
	mutex_unlock(&(a_ctrl->actuator_mutex));
	return rc;
}

int32_t cam_actuator_establish_link(
	struct cam_req_mgr_core_dev_link_setup *link)
{
	struct cam_actuator_ctrl_t *a_ctrl = NULL;

	if (!link) {
		CAM_ERR(CAM_ACTUATOR, "Invalid Args");
		return -EINVAL;
	}

	a_ctrl = (struct cam_actuator_ctrl_t *)
		cam_get_device_priv(link->dev_hdl);
	if (!a_ctrl) {
		CAM_ERR(CAM_ACTUATOR, "Device data is NULL");
		return -EINVAL;
	}

	mutex_lock(&(a_ctrl->actuator_mutex));
	if (link->link_enable) {
		a_ctrl->bridge_intf.link_hdl = link->link_hdl;
		a_ctrl->bridge_intf.crm_cb = link->crm_cb;
	} else {
		a_ctrl->bridge_intf.link_hdl = -1;
		a_ctrl->bridge_intf.crm_cb = NULL;
	}
	mutex_unlock(&(a_ctrl->actuator_mutex));

	return 0;
}

static void cam_actuator_update_req_mgr(
	struct cam_actuator_ctrl_t *a_ctrl,
	struct cam_packet *csl_packet)
{
	struct cam_req_mgr_add_request add_req;

	add_req.link_hdl = a_ctrl->bridge_intf.link_hdl;
	add_req.req_id = csl_packet->header.request_id;
	add_req.dev_hdl = a_ctrl->bridge_intf.device_hdl;
	add_req.skip_before_applying = 0;

	if (a_ctrl->bridge_intf.crm_cb &&
		a_ctrl->bridge_intf.crm_cb->add_req) {
		a_ctrl->bridge_intf.crm_cb->add_req(&add_req);
		CAM_DBG(CAM_ACTUATOR, "Request Id: %lld added to CRM",
			add_req.req_id);
	} else {
		CAM_ERR(CAM_ACTUATOR, "Can't add Request ID: %lld to CRM",
			csl_packet->header.request_id);
	}
}

int32_t cam_actuator_publish_dev_info(struct cam_req_mgr_device_info *info)
{
	if (!info) {
		CAM_ERR(CAM_ACTUATOR, "Invalid Args");
		return -EINVAL;
	}

	info->dev_id = CAM_REQ_MGR_DEVICE_ACTUATOR;
	strlcpy(info->name, CAM_ACTUATOR_NAME, sizeof(info->name));
	info->p_delay = 1;
	info->trigger = CAM_TRIGGER_POINT_SOF;

	return 0;
}

int32_t cam_actuator_i2c_pkt_parse(struct cam_actuator_ctrl_t *a_ctrl,
	void *arg)
{
	int32_t  rc = 0;
	int32_t  i = 0;
	uint32_t total_cmd_buf_in_bytes = 0;
	size_t   len_of_buff = 0;
	size_t   remain_len = 0;
	uint32_t *offset = NULL;
	uint32_t *cmd_buf = NULL;
	uintptr_t generic_ptr;
	uintptr_t generic_pkt_ptr;
	struct common_header      *cmm_hdr = NULL;
	struct cam_control        *ioctl_ctrl = NULL;
	struct cam_packet         *csl_packet = NULL;
	struct cam_config_dev_cmd config;
	struct i2c_data_settings  *i2c_data = NULL;
	struct i2c_settings_array *i2c_reg_settings = NULL;
	struct cam_cmd_buf_desc   *cmd_desc = NULL;
	struct cam_actuator_soc_private *soc_private = NULL;
	struct cam_sensor_power_ctrl_t  *power_info = NULL;
#if defined(CONFIG_SEC_A42XQ_PROJECT)
	int32_t retry = 0;
#endif

	if (!a_ctrl || !arg) {
		CAM_ERR(CAM_ACTUATOR, "Invalid Args");
		return -EINVAL;
	}

	soc_private =
		(struct cam_actuator_soc_private *)a_ctrl->soc_info.soc_private;

	power_info = &soc_private->power_info;

	ioctl_ctrl = (struct cam_control *)arg;
	if (copy_from_user(&config,
		u64_to_user_ptr(ioctl_ctrl->handle),
		sizeof(config)))
		return -EFAULT;
	rc = cam_mem_get_cpu_buf(config.packet_handle,
		&generic_pkt_ptr, &len_of_buff);
	if (rc < 0) {
		CAM_ERR(CAM_ACTUATOR, "Error in converting command Handle %d",
			rc);
		return rc;
	}

	remain_len = len_of_buff;
	if ((sizeof(struct cam_packet) > len_of_buff) ||
		((size_t)config.offset >= len_of_buff -
		sizeof(struct cam_packet))) {
		CAM_ERR(CAM_ACTUATOR,
			"Inval cam_packet strut size: %zu, len_of_buff: %zu",
			 sizeof(struct cam_packet), len_of_buff);
		rc = -EINVAL;
		goto end;
	}

	remain_len -= (size_t)config.offset;
	csl_packet = (struct cam_packet *)
			(generic_pkt_ptr + (uint32_t)config.offset);

	if (cam_packet_util_validate_packet(csl_packet,
		remain_len)) {
		CAM_ERR(CAM_ACTUATOR, "Invalid packet params");
		rc = -EINVAL;
		goto end;
	}

	CAM_DBG(CAM_ACTUATOR, "Pkt opcode: %d",	csl_packet->header.op_code);

	if ((csl_packet->header.op_code & 0xFFFFFF) !=
		CAM_ACTUATOR_PACKET_OPCODE_INIT &&
		csl_packet->header.request_id <= a_ctrl->last_flush_req
		&& a_ctrl->last_flush_req != 0) {
		CAM_DBG(CAM_ACTUATOR,
			"reject request %lld, last request to flush %lld",
			csl_packet->header.request_id, a_ctrl->last_flush_req);
		rc = -EINVAL;
		goto end;
	}

	if (csl_packet->header.request_id > a_ctrl->last_flush_req)
		a_ctrl->last_flush_req = 0;

	switch (csl_packet->header.op_code & 0xFFFFFF) {
	case CAM_ACTUATOR_PACKET_OPCODE_INIT:
		offset = (uint32_t *)&csl_packet->payload;
		offset += (csl_packet->cmd_buf_offset / sizeof(uint32_t));
		cmd_desc = (struct cam_cmd_buf_desc *)(offset);

		/* Loop through multiple command buffers */
		for (i = 0; i < csl_packet->num_cmd_buf; i++) {
			total_cmd_buf_in_bytes = cmd_desc[i].length;
			if (!total_cmd_buf_in_bytes)
				continue;
			rc = cam_mem_get_cpu_buf(cmd_desc[i].mem_handle,
					&generic_ptr, &len_of_buff);
			if (rc < 0) {
				CAM_ERR(CAM_ACTUATOR, "Failed to get cpu buf");
				goto end;
			}
			cmd_buf = (uint32_t *)generic_ptr;
			if (!cmd_buf) {
				CAM_ERR(CAM_ACTUATOR, "invalid cmd buf");
				rc = -EINVAL;
				goto end;
			}
			if ((len_of_buff < sizeof(struct common_header)) ||
				(cmd_desc[i].offset > (len_of_buff -
				sizeof(struct common_header)))) {
				CAM_ERR(CAM_ACTUATOR,
					"Invalid length for sensor cmd");
				rc = -EINVAL;
				goto end;
			}
			remain_len = len_of_buff - cmd_desc[i].offset;
			cmd_buf += cmd_desc[i].offset / sizeof(uint32_t);
			cmm_hdr = (struct common_header *)cmd_buf;

			switch (cmm_hdr->cmd_type) {
			case CAMERA_SENSOR_CMD_TYPE_I2C_INFO:
				CAM_DBG(CAM_ACTUATOR,
					"Received slave info buffer");
				rc = cam_actuator_slaveInfo_pkt_parser(
					a_ctrl, cmd_buf, remain_len);
				if (rc < 0) {
					CAM_ERR(CAM_ACTUATOR,
					"Failed to parse slave info: %d", rc);
					goto end;
				}
				break;
			case CAMERA_SENSOR_CMD_TYPE_PWR_UP:
			case CAMERA_SENSOR_CMD_TYPE_PWR_DOWN:
				CAM_DBG(CAM_ACTUATOR,
					"Received power settings buffer");
				rc = cam_sensor_update_power_settings(
					cmd_buf,
					total_cmd_buf_in_bytes,
					power_info, remain_len);
				if (rc) {
					CAM_ERR(CAM_ACTUATOR,
					"Failed:parse power settings: %d",
					rc);
					goto end;
				}
				break;
			default:
				CAM_DBG(CAM_ACTUATOR,
					"Received initSettings buffer");
				i2c_data = &(a_ctrl->i2c_data);
				i2c_reg_settings =
					&i2c_data->init_settings;

				i2c_reg_settings->request_id = 0;
				i2c_reg_settings->is_settings_valid = 1;
				rc = cam_sensor_i2c_command_parser(
					&a_ctrl->io_master_info,
					i2c_reg_settings,
					&cmd_desc[i], 1, NULL);
				if (rc < 0) {
					CAM_ERR(CAM_ACTUATOR,
					"Failed:parse init settings: %d",
					rc);
					goto end;
				}
				break;
			}
		}

		if (a_ctrl->cam_act_state == CAM_ACTUATOR_ACQUIRE) {
			rc = cam_actuator_power_up(a_ctrl);
			if (rc < 0) {
				CAM_ERR(CAM_ACTUATOR,
					" Actuator Power up failed");
				goto end;
			}
			a_ctrl->cam_act_state = CAM_ACTUATOR_CONFIG;
		}

#if defined(CONFIG_SEC_A42XQ_PROJECT)
		for (retry = 0; retry < 3; retry++)
		{
			rc = cam_actuator_apply_settings(a_ctrl,
				&a_ctrl->i2c_data.init_settings);
			if (rc < 0) {
				CAM_ERR(CAM_ACTUATOR, "Cannot apply Init settings, retry %d", retry);
				cam_actuator_power_down(a_ctrl);
				msleep(20);
				cam_actuator_power_up(a_ctrl);
			} else {
				break;
			}
		}
		if (rc < 0) {
			CAM_ERR(CAM_ACTUATOR, "Cannot apply Init settings");
			goto end;
		}
#else
		rc = cam_actuator_apply_settings(a_ctrl,
			&a_ctrl->i2c_data.init_settings);
		if (rc < 0) {
			CAM_ERR(CAM_ACTUATOR, "Cannot apply Init settings");
			goto end;
		}
#endif

		/* Delete the request even if the apply is failed */
		rc = delete_request(&a_ctrl->i2c_data.init_settings);
		if (rc < 0) {
			CAM_WARN(CAM_ACTUATOR,
				"Fail in deleting the Init settings");
			rc = 0;
		}
		break;
	case CAM_ACTUATOR_PACKET_AUTO_MOVE_LENS:
		if (a_ctrl->cam_act_state < CAM_ACTUATOR_CONFIG) {
			rc = -EINVAL;
			CAM_WARN(CAM_ACTUATOR,
				"Not in right state to move lens: %d",
				a_ctrl->cam_act_state);
			goto end;
		}
		a_ctrl->setting_apply_state = ACT_APPLY_SETTINGS_NOW;

		i2c_data = &(a_ctrl->i2c_data);
		i2c_reg_settings = &i2c_data->init_settings;

		i2c_data->init_settings.request_id =
			csl_packet->header.request_id;
		i2c_reg_settings->is_settings_valid = 1;
		offset = (uint32_t *)&csl_packet->payload;
		offset += csl_packet->cmd_buf_offset / sizeof(uint32_t);
		cmd_desc = (struct cam_cmd_buf_desc *)(offset);
		rc = cam_sensor_i2c_command_parser(
			&a_ctrl->io_master_info,
			i2c_reg_settings,
			cmd_desc, 1, NULL);
		if (rc < 0) {
			CAM_ERR(CAM_ACTUATOR,
				"Auto move lens parsing failed: %d", rc);
			goto end;
		}
		cam_actuator_update_req_mgr(a_ctrl, csl_packet);
		break;
	case CAM_ACTUATOR_PACKET_MANUAL_MOVE_LENS:
		if (a_ctrl->cam_act_state < CAM_ACTUATOR_CONFIG) {
			rc = -EINVAL;
			CAM_WARN(CAM_ACTUATOR,
				"Not in right state to move lens: %d",
				a_ctrl->cam_act_state);
			goto end;
		}

		a_ctrl->setting_apply_state = ACT_APPLY_SETTINGS_LATER;
		i2c_data = &(a_ctrl->i2c_data);
		i2c_reg_settings = &i2c_data->per_frame[
			csl_packet->header.request_id % MAX_PER_FRAME_ARRAY];

		 i2c_reg_settings->request_id =
			csl_packet->header.request_id;
		i2c_reg_settings->is_settings_valid = 1;
		offset = (uint32_t *)&csl_packet->payload;
		offset += csl_packet->cmd_buf_offset / sizeof(uint32_t);
		cmd_desc = (struct cam_cmd_buf_desc *)(offset);
		rc = cam_sensor_i2c_command_parser(
			&a_ctrl->io_master_info,
			i2c_reg_settings,
			cmd_desc, 1, NULL);
		if (rc < 0) {
			CAM_ERR(CAM_ACTUATOR,
				"Manual move lens parsing failed: %d", rc);
			goto end;
		}

		cam_actuator_update_req_mgr(a_ctrl, csl_packet);
		break;
	case CAM_PKT_NOP_OPCODE:
		if (a_ctrl->cam_act_state < CAM_ACTUATOR_CONFIG) {
			CAM_WARN(CAM_ACTUATOR,
				"Received NOP packets in invalid state: %d",
				a_ctrl->cam_act_state);
			rc = -EINVAL;
			goto end;
		}
		cam_actuator_update_req_mgr(a_ctrl, csl_packet);
		break;
	case CAM_ACTUATOR_PACKET_OPCODE_READ: {
		struct cam_buf_io_cfg *io_cfg;
		struct i2c_settings_array i2c_read_settings;

		if (a_ctrl->cam_act_state < CAM_ACTUATOR_CONFIG) {
			rc = -EINVAL;
			CAM_WARN(CAM_ACTUATOR,
				"Not in right state to read actuator: %d",
				a_ctrl->cam_act_state);
			goto end;
		}
		CAM_DBG(CAM_ACTUATOR, "number of I/O configs: %d:",
			csl_packet->num_io_configs);
		if (csl_packet->num_io_configs == 0) {
			CAM_ERR(CAM_ACTUATOR, "No I/O configs to process");
			rc = -EINVAL;
			goto end;
		}

		INIT_LIST_HEAD(&(i2c_read_settings.list_head));

		io_cfg = (struct cam_buf_io_cfg *) ((uint8_t *)
			&csl_packet->payload +
			csl_packet->io_configs_offset);

		if (io_cfg == NULL) {
			CAM_ERR(CAM_ACTUATOR, "I/O config is invalid(NULL)");
			rc = -EINVAL;
			goto end;
		}

		offset = (uint32_t *)&csl_packet->payload;
		offset += (csl_packet->cmd_buf_offset / sizeof(uint32_t));
		cmd_desc = (struct cam_cmd_buf_desc *)(offset);
		i2c_read_settings.is_settings_valid = 1;
		i2c_read_settings.request_id = 0;
		rc = cam_sensor_i2c_command_parser(&a_ctrl->io_master_info,
			&i2c_read_settings,
			cmd_desc, 1, io_cfg);
		if (rc < 0) {
			CAM_ERR(CAM_ACTUATOR,
				"actuator read pkt parsing failed: %d", rc);
			goto end;
		}

		rc = cam_sensor_i2c_read_data(
			&i2c_read_settings,
			&a_ctrl->io_master_info);
		if (rc < 0) {
			CAM_ERR(CAM_ACTUATOR, "cannot read data, rc:%d", rc);
			delete_request(&i2c_read_settings);
			goto end;
		}

		rc = delete_request(&i2c_read_settings);
		if (rc < 0) {
			CAM_ERR(CAM_ACTUATOR,
				"Failed in deleting the read settings");
			goto end;
		}
		break;
		}
	default:
		CAM_ERR(CAM_ACTUATOR, "Wrong Opcode: %d",
			csl_packet->header.op_code & 0xFFFFFF);
		rc = -EINVAL;
		goto end;
	}

end:
	return rc;
}

void cam_actuator_shutdown(struct cam_actuator_ctrl_t *a_ctrl)
{
	int rc = 0;
	struct cam_actuator_soc_private  *soc_private =
		(struct cam_actuator_soc_private *)a_ctrl->soc_info.soc_private;
	struct cam_sensor_power_ctrl_t *power_info =
		&soc_private->power_info;

	if (a_ctrl->cam_act_state == CAM_ACTUATOR_INIT)
		return;

	if (a_ctrl->cam_act_state >= CAM_ACTUATOR_CONFIG) {
		rc = cam_actuator_power_down(a_ctrl);
		if (rc < 0)
			CAM_ERR(CAM_ACTUATOR, "Actuator Power down failed");
		a_ctrl->cam_act_state = CAM_ACTUATOR_ACQUIRE;
	}

	if (a_ctrl->cam_act_state >= CAM_ACTUATOR_ACQUIRE) {
		rc = cam_destroy_device_hdl(a_ctrl->bridge_intf.device_hdl);
		if (rc < 0)
			CAM_ERR(CAM_ACTUATOR, "destroying  dhdl failed");
		a_ctrl->bridge_intf.device_hdl = -1;
		a_ctrl->bridge_intf.link_hdl = -1;
		a_ctrl->bridge_intf.session_hdl = -1;
	}

	kfree(power_info->power_setting);
	kfree(power_info->power_down_setting);
	power_info->power_setting = NULL;
	power_info->power_down_setting = NULL;
	power_info->power_setting_size = 0;
	power_info->power_down_setting_size = 0;

	a_ctrl->cam_act_state = CAM_ACTUATOR_INIT;
}

int32_t cam_actuator_driver_cmd(struct cam_actuator_ctrl_t *a_ctrl,
	void *arg)
{
	int rc = 0;
	struct cam_control *cmd = (struct cam_control *)arg;
	struct cam_actuator_soc_private *soc_private = NULL;
	struct cam_sensor_power_ctrl_t  *power_info = NULL;

	if (!a_ctrl || !cmd) {
		CAM_ERR(CAM_ACTUATOR, "Invalid Args");
		return -EINVAL;
	}

	soc_private =
		(struct cam_actuator_soc_private *)a_ctrl->soc_info.soc_private;

	power_info = &soc_private->power_info;

	if (cmd->handle_type != CAM_HANDLE_USER_POINTER) {
		CAM_ERR(CAM_ACTUATOR, "Invalid handle type: %d",
			cmd->handle_type);
		return -EINVAL;
	}

	CAM_DBG(CAM_ACTUATOR, "Opcode to Actuator: %d", cmd->op_code);

	mutex_lock(&(a_ctrl->actuator_mutex));
	switch (cmd->op_code) {
	case CAM_ACQUIRE_DEV: {
		struct cam_sensor_acquire_dev actuator_acq_dev;
		struct cam_create_dev_hdl bridge_params;

		if (a_ctrl->bridge_intf.device_hdl != -1) {
			CAM_ERR(CAM_ACTUATOR, "Device is already acquired");
			rc = -EINVAL;
			goto release_mutex;
		}
		rc = copy_from_user(&actuator_acq_dev,
			u64_to_user_ptr(cmd->handle),
			sizeof(actuator_acq_dev));
		if (rc < 0) {
			CAM_ERR(CAM_ACTUATOR, "Failed Copying from user\n");
			goto release_mutex;
		}

		bridge_params.session_hdl = actuator_acq_dev.session_handle;
		bridge_params.ops = &a_ctrl->bridge_intf.ops;
		bridge_params.v4l2_sub_dev_flag = 0;
		bridge_params.media_entity_flag = 0;
		bridge_params.priv = a_ctrl;

		actuator_acq_dev.device_handle =
			cam_create_device_hdl(&bridge_params);
		a_ctrl->bridge_intf.device_hdl = actuator_acq_dev.device_handle;
		a_ctrl->bridge_intf.session_hdl =
			actuator_acq_dev.session_handle;

		CAM_DBG(CAM_ACTUATOR, "Device Handle: %d",
			actuator_acq_dev.device_handle);
		if (copy_to_user(u64_to_user_ptr(cmd->handle),
			&actuator_acq_dev,
			sizeof(struct cam_sensor_acquire_dev))) {
			CAM_ERR(CAM_ACTUATOR, "Failed Copy to User");
			rc = -EFAULT;
			goto release_mutex;
		}

		a_ctrl->cam_act_state = CAM_ACTUATOR_ACQUIRE;
	}
		break;
	case CAM_RELEASE_DEV: {
		if (a_ctrl->cam_act_state == CAM_ACTUATOR_START) {
			rc = -EINVAL;
			CAM_WARN(CAM_ACTUATOR,
				"Cant release actuator: in start state");
			goto release_mutex;
		}

		if (a_ctrl->cam_act_state == CAM_ACTUATOR_CONFIG) {
			rc = cam_actuator_power_down(a_ctrl);
			if (rc < 0) {
				CAM_ERR(CAM_ACTUATOR,
					"Actuator Power down failed");
				goto release_mutex;
			}
		}

		if (a_ctrl->bridge_intf.device_hdl == -1) {
			CAM_ERR(CAM_ACTUATOR, "link hdl: %d device hdl: %d",
				a_ctrl->bridge_intf.device_hdl,
				a_ctrl->bridge_intf.link_hdl);
			rc = -EINVAL;
			goto release_mutex;
		}

		if (a_ctrl->bridge_intf.link_hdl != -1) {
			CAM_ERR(CAM_ACTUATOR,
				"Device [%d] still active on link 0x%x",
				a_ctrl->cam_act_state,
				a_ctrl->bridge_intf.link_hdl);
			rc = -EAGAIN;
			goto release_mutex;
		}

		rc = cam_destroy_device_hdl(a_ctrl->bridge_intf.device_hdl);
		if (rc < 0)
			CAM_ERR(CAM_ACTUATOR, "destroying the device hdl");
		a_ctrl->bridge_intf.device_hdl = -1;
		a_ctrl->bridge_intf.link_hdl = -1;
		a_ctrl->bridge_intf.session_hdl = -1;
		a_ctrl->cam_act_state = CAM_ACTUATOR_INIT;
		a_ctrl->last_flush_req = 0;
		kfree(power_info->power_setting);
		kfree(power_info->power_down_setting);
		power_info->power_setting = NULL;
		power_info->power_down_setting = NULL;
		power_info->power_down_setting_size = 0;
		power_info->power_setting_size = 0;
	}
		break;
	case CAM_QUERY_CAP: {
		struct cam_actuator_query_cap actuator_cap = {0};

		actuator_cap.slot_info = a_ctrl->soc_info.index;
		if (copy_to_user(u64_to_user_ptr(cmd->handle),
			&actuator_cap,
			sizeof(struct cam_actuator_query_cap))) {
			CAM_ERR(CAM_ACTUATOR, "Failed Copy to User");
			rc = -EFAULT;
			goto release_mutex;
		}
	}
		break;
	case CAM_START_DEV: {
		if (a_ctrl->cam_act_state != CAM_ACTUATOR_CONFIG) {
			rc = -EINVAL;
			CAM_WARN(CAM_ACTUATOR,
			"Not in right state to start : %d",
			a_ctrl->cam_act_state);
			goto release_mutex;
		}
		a_ctrl->cam_act_state = CAM_ACTUATOR_START;
		a_ctrl->last_flush_req = 0;
	}
		break;
	case CAM_STOP_DEV: {
		struct i2c_settings_array *i2c_set = NULL;
		int i;

		if (a_ctrl->cam_act_state != CAM_ACTUATOR_START) {
			rc = -EINVAL;
			CAM_WARN(CAM_ACTUATOR,
			"Not in right state to stop : %d",
			a_ctrl->cam_act_state);
			goto release_mutex;
		}

		for (i = 0; i < MAX_PER_FRAME_ARRAY; i++) {
			i2c_set = &(a_ctrl->i2c_data.per_frame[i]);

			if (i2c_set->is_settings_valid == 1) {
				rc = delete_request(i2c_set);
				if (rc < 0)
					CAM_ERR(CAM_SENSOR,
						"delete request: %lld rc: %d",
						i2c_set->request_id, rc);
			}
		}
		a_ctrl->last_flush_req = 0;
		a_ctrl->cam_act_state = CAM_ACTUATOR_CONFIG;
	}
		break;
	case CAM_CONFIG_DEV: {
		a_ctrl->setting_apply_state =
			ACT_APPLY_SETTINGS_LATER;
		rc = cam_actuator_i2c_pkt_parse(a_ctrl, arg);
		if (rc < 0) {
			CAM_ERR(CAM_ACTUATOR, "Failed in actuator Parsing");
			goto release_mutex;
		}

		if (a_ctrl->setting_apply_state ==
			ACT_APPLY_SETTINGS_NOW) {
			rc = cam_actuator_apply_settings(a_ctrl,
				&a_ctrl->i2c_data.init_settings);
			if ((rc == -EAGAIN) &&
			(a_ctrl->io_master_info.master_type == CCI_MASTER)) {
				CAM_WARN(CAM_ACTUATOR,
					"CCI HW is in resetting mode:: Reapplying Init settings");
				usleep_range(1000, 1010);
				rc = cam_actuator_apply_settings(a_ctrl,
					&a_ctrl->i2c_data.init_settings);
			}

			if (rc < 0)
				CAM_ERR(CAM_ACTUATOR,
					"Failed to apply Init settings: rc = %d",
					rc);
			/* Delete the request even if the apply is failed */
			rc = delete_request(&a_ctrl->i2c_data.init_settings);
			if (rc < 0) {
				CAM_ERR(CAM_ACTUATOR,
					"Failed in Deleting the Init Pkt: %d",
					rc);
				goto release_mutex;
			}
		}
	}
		break;
	default:
		CAM_ERR(CAM_ACTUATOR, "Invalid Opcode %d", cmd->op_code);
	}

release_mutex:
	mutex_unlock(&(a_ctrl->actuator_mutex));

	return rc;
}

int32_t cam_actuator_flush_request(struct cam_req_mgr_flush_request *flush_req)
{
	int32_t rc = 0, i;
	uint32_t cancel_req_id_found = 0;
	struct cam_actuator_ctrl_t *a_ctrl = NULL;
	struct i2c_settings_array *i2c_set = NULL;

	if (!flush_req)
		return -EINVAL;

	a_ctrl = (struct cam_actuator_ctrl_t *)
		cam_get_device_priv(flush_req->dev_hdl);
	if (!a_ctrl) {
		CAM_ERR(CAM_ACTUATOR, "Device data is NULL");
		return -EINVAL;
	}

	if (a_ctrl->i2c_data.per_frame == NULL) {
		CAM_ERR(CAM_ACTUATOR, "i2c frame data is NULL");
		return -EINVAL;
	}

	mutex_lock(&(a_ctrl->actuator_mutex));
	if (flush_req->type == CAM_REQ_MGR_FLUSH_TYPE_ALL) {
		a_ctrl->last_flush_req = flush_req->req_id;
		CAM_DBG(CAM_ACTUATOR, "last reqest to flush is %lld",
			flush_req->req_id);
	}

	for (i = 0; i < MAX_PER_FRAME_ARRAY; i++) {
		i2c_set = &(a_ctrl->i2c_data.per_frame[i]);

		if ((flush_req->type == CAM_REQ_MGR_FLUSH_TYPE_CANCEL_REQ)
				&& (i2c_set->request_id != flush_req->req_id))
			continue;

		if (i2c_set->is_settings_valid == 1) {
			rc = delete_request(i2c_set);
			if (rc < 0)
				CAM_ERR(CAM_ACTUATOR,
					"delete request: %lld rc: %d",
					i2c_set->request_id, rc);

			if (flush_req->type ==
				CAM_REQ_MGR_FLUSH_TYPE_CANCEL_REQ) {
				cancel_req_id_found = 1;
				break;
			}
		}
	}

	if (flush_req->type == CAM_REQ_MGR_FLUSH_TYPE_CANCEL_REQ &&
		!cancel_req_id_found)
		CAM_DBG(CAM_ACTUATOR,
			"Flush request id:%lld not found in the pending list",
			flush_req->req_id);
	mutex_unlock(&(a_ctrl->actuator_mutex));
	return rc;
}

#if defined(CONFIG_SAMSUNG_OIS_MCU_STM32)
/***** for only ois selftest , set the actuator initial position to 256 *****/
int16_t cam_actuator_move_for_ois_test(struct cam_actuator_ctrl_t *a_ctrl)
{
	struct cam_sensor_i2c_reg_setting reg_setting;
	int rc = 0;
	int size = 0;

	memset(&reg_setting, 0, sizeof(reg_setting));
	if (a_ctrl == NULL) {
		CAM_ERR(CAM_ACTUATOR, "failed. a_ctrl is NULL");
		return -EINVAL;
	}

	reg_setting.reg_setting = kmalloc(sizeof(struct cam_sensor_i2c_reg_array) * 4, GFP_KERNEL);
	if (!reg_setting.reg_setting) {
		return -ENOMEM;
	}
	memset(reg_setting.reg_setting, 0, sizeof(struct cam_sensor_i2c_reg_array));

	/* Init setting for ak7377 */
	/* SET Standby Mode */
	reg_setting.reg_setting[size].reg_addr = 0x02;
	reg_setting.reg_setting[size].reg_data = 0x40;
	reg_setting.reg_setting[size].delay = 2000;
	size++;

	/* SET Position MSB - 0x00 */
	reg_setting.reg_setting[size].reg_addr = 0x00;
	reg_setting.reg_setting[size].reg_data = 0x80;
	size++;

	/* SET Position LSB - 0x00 */
	reg_setting.reg_setting[size].reg_addr = 0x01;
	reg_setting.reg_setting[size].reg_data = 0x00;
	size++;

	/* SET Active Mode */
	reg_setting.reg_setting[size].reg_addr = 0x02;
	reg_setting.reg_setting[size].reg_data = 0x00;
	size++;

	reg_setting.size = size;
	reg_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
	reg_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;

	rc = camera_io_dev_write(&a_ctrl->io_master_info,
		&reg_setting);
	if (rc < 0)
		CAM_ERR(CAM_ACTUATOR,
			"Failed to random write I2C settings: %d",
			rc);

	if (reg_setting.reg_setting) {
		kfree(reg_setting.reg_setting);
		reg_setting.reg_setting = NULL;
	}

	return rc;
}
#endif
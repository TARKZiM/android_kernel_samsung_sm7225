/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 */
#ifndef _CAM_EEPROM_DEV_H_
#define _CAM_EEPROM_DEV_H_

#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/gpio.h>
#include <media/v4l2-event.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-ioctl.h>
#include <media/cam_sensor.h>
#include <cam_sensor_i2c.h>
#include <cam_sensor_spi.h>
#include <cam_sensor_io.h>
#include <cam_cci_dev.h>
#include <cam_req_mgr_util.h>
#include <cam_req_mgr_interface.h>
#include <cam_mem_mgr.h>
#include <cam_subdev.h>
#include <media/cam_sensor.h>
#include "cam_soc_util.h"

/*************************************************************************************************/

#if defined(CONFIG_SAMSUNG_OIS_MCU_STM32) || defined(CONFIG_SAMSUNG_OIS_RUMBA_S4)
#define OIS_XYGG_SIZE                               8
#define OIS_CENTER_SHIFT_SIZE                       4
#define OIS_XYSR_SIZE                               4
#define OIS_CROSSTALK_SIZE                          4
#define OIS_CAL_START_ADDRESS                       0x0170
#define OIS_CAL_MARK_START_OFFSET                   0x30
#define OIS_XYGG_START_OFFSET                       0x10
#define OIS_XYSR_START_OFFSET                       0x38
#if defined(CONFIG_SAMSUNG_REAR_TRIPLE)
#define WIDE_OIS_CENTER_SHIFT_START_OFFSET          0x2AE
#define TELE_OIS_CENTER_SHIFT_START_OFFSET          0x2AA
#define TELE_OIS_CROSSTALK_START_OFFSET             0x1C
#endif
#endif


#define DEFINE_MSM_MUTEX(mutexname) \
	static struct mutex mutexname = __MUTEX_INITIALIZER(mutexname)
#define OK 1
#define CRASH 0
#define PROPERTY_MAXSIZE 32

#define MSM_EEPROM_MEMORY_MAP_MAX_SIZE          80
#define MSM_EEPROM_MAX_MEM_MAP_CNT              50
#define MSM_EEPROM_MEM_MAP_PROPERTIES_CNT       6
#define PROJECT_CAL_TYPE_MAX_SIZE               (20)

#if defined(CONFIG_SEC_A42XQ_PROJECT) || defined(CONFIG_SEC_A51XQ_PROJECT)
#define REAR3_MODULE_FW_VERSION                 0x0077
#define REAR3_MODULE_ID_ADDR                    0x1280
#elif defined(CONFIG_SEC_A52XQ_PROJECT)
#define REAR3_MODULE_FW_VERSION                 0x0077
#define REAR3_MODULE_ID_ADDR                    0x0056
#elif defined(CONFIG_SEC_A42XUQ_PROJECT)
#define REAR3_MODULE_FW_VERSION                 0x0000
#define REAR3_MODULE_ID_ADDR                    0x00AE
#else
#define REAR3_MODULE_FW_VERSION                 0x1560
#endif

#if defined(CONFIG_SEC_A42XQ_PROJECT) || defined(CONFIG_SEC_A51XQ_PROJECT) || defined(CONFIG_SEC_A42XUQ_PROJECT)
#define REAR2_MODULE_FW_VERSION                 0x005E
#elif defined(CONFIG_SEC_A52XQ_PROJECT)
#define REAR2_MODULE_FW_VERSION                 0x000E
#else
#define REAR2_MODULE_FW_VERSION                 0x0048
#endif

#define FROM_MODULE_FW_INFO_SIZE                11
#define FROM_REAR_HEADER_SIZE                   0x0100
#define REAR_CAL_VERSION_ADDR                   0x2D44
#define REAR3_CAL_VERSION_ADDR                  0x1618
#define REAR2_CAL_VERSION_ADDR                  0x00E0


#if defined(CONFIG_SEC_A42XQ_PROJECT) || defined(CONFIG_SEC_A51XQ_PROJECT) || defined(CONFIG_SEC_A52XQ_PROJECT) || defined(CONFIG_SEC_A42XUQ_PROJECT) || defined(CONFIG_SEC_GTS7XLLITE_PROJECT) || defined(CONFIG_SEC_GTS7XLLITEWIFI_PROJECT)
#define REAR_CAM_MAP_VERSION_ADDR               0x0090
#define REAR_DLL_VERSION_ADDR                   0x0094
#define REAR2_CAM_MAP_VERSION_ADDR              0x0090
#else
#define REAR_CAM_MAP_VERSION_ADDR               0x00E0
#define REAR_DLL_VERSION_ADDR                   0x00E4
#define REAR2_CAM_MAP_VERSION_ADDR              0x00E0
#endif
#if defined(CONFIG_SEC_A42XQ_PROJECT) || defined(CONFIG_SEC_A51XQ_PROJECT) || defined(CONFIG_SEC_A52XQ_PROJECT) || defined(CONFIG_SEC_A42XUQ_PROJECT)
#define REAR3_CAM_MAP_VERSION_ADDR        0x1160
#else
#define REAR3_CAM_MAP_VERSION_ADDR        0x1618
#endif


#if defined(CONFIG_SEC_A42XQ_PROJECT) || defined(CONFIG_SEC_A51XQ_PROJECT) || defined(CONFIG_SEC_A52XQ_PROJECT) || defined(CONFIG_SEC_A42XUQ_PROJECT)
#define REAR3_DLL_VERSION_ADDR      0x1180
#else
#define REAR3_DLL_VERSION_ADDR      0x161C
#endif

#if defined(CONFIG_SEC_A42XQ_PROJECT) || defined(CONFIG_SEC_A51XQ_PROJECT) || defined(CONFIG_SEC_A52XQ_PROJECT) || defined(CONFIG_SEC_A42XUQ_PROJECT)
#define REAR2_DLL_VERSION_ADDR       0x0094
#else
#define REAR2_DLL_VERSION_ADDR       0x00E4
#endif


/* Module ID : 0x00A8~0x00B7(16Byte) for FROM, EEPROM (Don't support OTP)*/
#define FROM_MODULE_ID_ADDR                     0x00AE
#define FROM_MODULE_ID_SIZE                     10
#define FROM_REAR_SENSOR_ID_ADDR                0x00B8

#if defined(CONFIG_SEC_A42XQ_PROJECT) || defined(CONFIG_SEC_A51XQ_PROJECT) || defined(CONFIG_SEC_A42XUQ_PROJECT)
#define REAR_MODULE_FW_VERSION                  0x005E
#elif defined(CONFIG_SEC_A52XQ_PROJECT)
#define REAR_MODULE_FW_VERSION                  0x006E
#elif defined(CONFIG_SEC_GTS7XLLITE_PROJECT) || defined(CONFIG_SEC_GTS7XLLITEWIFI_PROJECT)
#define REAR_MODULE_FW_VERSION                  0x0056
#else
#define REAR_MODULE_FW_VERSION                  0x0048
#endif

#if defined(CONFIG_SEC_A42XQ_PROJECT) || defined(CONFIG_SEC_A51XQ_PROJECT)
#define FRONT_CAL_VERSION_ADDR                  0x0090
#define FRONT_CAM_MAP_VERSION_ADDR              0x0090
#define FROM_FRONT_MODULE_ID_ADDR               0x00AE
#define FROM_FRONT_SENSOR_ID_ADDR               0x00B8
#define FRONT_MODULE_FW_VERSION                 0x005E
#elif defined(CONFIG_SEC_A52XQ_PROJECT)
#define FRONT_CAL_VERSION_ADDR                  0x0030
#define FRONT_CAM_MAP_VERSION_ADDR              0x0030
#define FROM_FRONT_MODULE_ID_ADDR               0x0056
#define FROM_FRONT_SENSOR_ID_ADDR               0x0060
#define FRONT_MODULE_FW_VERSION                 0x001E
#elif defined(CONFIG_SEC_GTS7XLLITE_PROJECT) || defined(CONFIG_SEC_GTS7XLLITEWIFI_PROJECT) 
#define FRONT_CAL_VERSION_ADDR                  0x001A
#define FRONT_CAM_MAP_VERSION_ADDR              0x001E
#define FROM_FRONT_MODULE_ID_ADDR               0x0032
#define FROM_FRONT_SENSOR_ID_ADDR               0x003C
#define FRONT_MODULE_FW_VERSION                 0x000E
#elif defined(CONFIG_SEC_A42XUQ_PROJECT)
#define FRONT_CAL_VERSION_ADDR                  0x001A
#define FRONT_CAM_MAP_VERSION_ADDR              0x001E
#define FROM_FRONT_MODULE_ID_ADDR               0x0032
#define FROM_FRONT_SENSOR_ID_ADDR               0x003C
#define FRONT_MODULE_FW_VERSION                 0x000E
#else
#define FRONT_CAL_VERSION_ADDR                  0x10E0
#define FRONT_CAM_MAP_VERSION_ADDR              0x00CB
#define FROM_FRONT_MODULE_ID_ADDR               0x00A3
#define FROM_FRONT_SENSOR_ID_ADDR               0x00B3
#define FRONT_MODULE_FW_VERSION                 0x10E0
#endif

#if defined(CONFIG_SEC_A52XQ_PROJECT)
#define FRONT_DLL_VERSION_ADDR                  0x0034
#else
#define FRONT_DLL_VERSION_ADDR                  0x0094
#endif
#define FROM_FRONT_MODULE_ID_SIZE               10
#define FRONT_FROM_CAL_MAP_VERSION              0x43
#define FRONT_MODULE_VER_ON_PVR                 0x72
#define FRONT_MODULE_VER_ON_SRA                 0x78

#if defined(CONFIG_SEC_A42XQ_PROJECT) || defined(CONFIG_SEC_A51XQ_PROJECT) || defined(CONFIG_SEC_A52XQ_PROJECT) || defined(CONFIG_SEC_A42XUQ_PROJECT) || defined(CONFIG_SEC_GTS7XLLITE_PROJECT) || defined(CONFIG_SEC_GTS7XLLITEWIFI_PROJECT)
#define FROM_REAR_AF_CAL_D10_ADDR               0x010C
#define FROM_REAR_AF_CAL_PAN_ADDR               0x0110
#define FROM_REAR_AF_CAL_MACRO_ADDR             0x010C
#else
#define FROM_REAR_AF_CAL_D10_ADDR               0x0818
#define FROM_REAR_AF_CAL_PAN_ADDR               0x081C
#define FROM_REAR_AF_CAL_MACRO_ADDR             0x0818
#endif

//#define FROM_REAR_AF_CAL_D50_ADDR             0x4688
//#define FROM_REAR_AF_CAL_D80_ADDR             0x4680
//#define FROM_FRONT_AF_CAL_PAN_ADDR              0x0104
//#define FROM_FRONT_AF_CAL_MACRO_ADDR            0x0110

#define FROM_PAF_CAL_DATA_START_ADDR            0x80C
//#define FROM_F2_PAF_CAL_DATA_START_ADDR         0x3250
/* REAR PAF OFFSET MID (30CM, WIDE) */
#define FROM_PAF_OFFSET_MID_ADDR                (FROM_PAF_CAL_DATA_START_ADDR + 0x0730)
#define FROM_PAF_OFFSET_MID_SIZE                936
/* REAR PAF OFFSET FAR (1M, WIDE) */
#define FROM_PAF_OFFSET_FAR_ADDR                (FROM_PAF_CAL_DATA_START_ADDR + 0x0CD0)
#define FROM_PAF_OFFSET_FAR_SIZE                234

/* REAR F2 PAF OFFSET MID (30CM, WIDE) */
//#define FROM_F2_PAF_OFFSET_MID_ADDR             (FROM_F2_PAF_CAL_DATA_START_ADDR + 0x0730)
#define FROM_F2_PAF_OFFSET_MID_SIZE             936
/* REAR F2 PAF OFFSET FAR (1M, WIDE) */
//#define FROM_F2_PAF_OFFSET_FAR_ADDR             (FROM_F2_PAF_CAL_DATA_START_ADDR + 0x0CD0)
#define FROM_F2_PAF_OFFSET_FAR_SIZE             234
#define FROM_PAF_CAL_ERR_CHECK_OFFSET		0x14

#if defined(CONFIG_SAMSUNG_REAR_TRIPLE)
//#define FROM_REAR3_PAF_CAL_DATA_START_ADDR      0x0900
//rear2
//#define FROM_REAR3_AF_CAL_D50_ADDR		0x0814
/*#define FROM_REAR3_AF_CAL_D20_ADDR*/
/*#define FROM_REAR3_AF_CAL_D30_ADDR*/
/*#define FROM_REAR3_AF_CAL_D40_ADDR            0x754C*/
//#define FROM_REAR3_AF_CAL_D50_ADDR              0x0814
/*#define FROM_REAR3_AF_CAL_D60_ADDR*/
/*#define FROM_REAR3_AF_CAL_D70_ADDR*/
/*#define FROM_REAR3_AF_CAL_D80_ADDR            0x7540*/
//#define FROM_REAR3_AF_CAL_MACRO_ADDR            0x0818
//#define FROM_REAR3_AF_CAL_PAN_ADDR              0x081C

#if defined(CONFIG_SEC_A42XQ_PROJECT) || defined(CONFIG_SEC_A42XUQ_PROJECT)
#define REAR2_MODULE_ID_ADDR                    0xAE
#define FROM_REAR2_SENSOR_ID_ADDR               0xB8
#define FROM_REAR2_DUAL_TILT_X                  0x0D4A
#define FROM_REAR2_DUAL_TILT_Y                  0x0D4E
#define FROM_REAR2_DUAL_TILT_Z                  0x0D52
#define FROM_REAR2_DUAL_TILT_SX                 0x0DAA
#define FROM_REAR2_DUAL_TILT_SY                 0x0DAE
#define FROM_REAR2_DUAL_TILT_RANGE              0x10D6
#define FROM_REAR2_DUAL_TILT_MAX_ERR            0x10D2
#define FROM_REAR2_DUAL_TILT_AVG_ERR            0x10CE
#define FROM_REAR2_DUAL_TILT_DLL_VERSION        0x1E80
#define FROM_REAR2_DUAL_CAL_ADDR                0x1E80
#define FROM_REAR2_DUAL_CAL_SIZE                1024
#elif defined(CONFIG_SEC_A51XQ_PROJECT)
#define REAR2_MODULE_ID_ADDR                    0xAE
#define FROM_REAR2_SENSOR_ID_ADDR               0xB8
#define FROM_REAR2_DUAL_TILT_X                  0x1CD8
#define FROM_REAR2_DUAL_TILT_Y                  0x1CDC
#define FROM_REAR2_DUAL_TILT_Z                  0x1CE0
#define FROM_REAR2_DUAL_TILT_SX                 0x1CE4
#define FROM_REAR2_DUAL_TILT_SY                 0x1CE8
#define FROM_REAR2_DUAL_TILT_RANGE              0x1F24
#define FROM_REAR2_DUAL_TILT_MAX_ERR            0x1F28
#define FROM_REAR2_DUAL_TILT_AVG_ERR            0x1F2C
#define FROM_REAR2_DUAL_TILT_DLL_VERSION        0x1B40
#define FROM_REAR2_DUAL_CAL_ADDR                0x1B40
#define FROM_REAR2_DUAL_CAL_SIZE                1024
#elif defined(CONFIG_SEC_A52XQ_PROJECT)
#define REAR2_MODULE_ID_ADDR                    0x32
#define FROM_REAR2_SENSOR_ID_ADDR               0x40
#define FROM_REAR2_DUAL_TILT_X                  0x0D4A
#define FROM_REAR2_DUAL_TILT_Y                  0x0D4E
#define FROM_REAR2_DUAL_TILT_Z                  0x0D52
#define FROM_REAR2_DUAL_TILT_SX                 0x0DAA
#define FROM_REAR2_DUAL_TILT_SY                 0x0DAE
#define FROM_REAR2_DUAL_TILT_RANGE              0x10D6
#define FROM_REAR2_DUAL_TILT_MAX_ERR            0x10D2
#define FROM_REAR2_DUAL_TILT_AVG_ERR            0x10CE
#define FROM_REAR2_DUAL_TILT_DLL_VERSION        0x1E80
#define FROM_REAR2_DUAL_CAL_ADDR                0x1E80
#define FROM_REAR2_DUAL_CAL_SIZE                1024
#else
#define REAR2_MODULE_ID_ADDR                    0x00AE
#define FROM_REAR2_SENSOR_ID_ADDR               0x00B8
#define FROM_REAR2_DUAL_TILT_X                  0x0948
#define FROM_REAR2_DUAL_TILT_Y                  0x094C
#define FROM_REAR2_DUAL_TILT_Z                  0x0950
#define FROM_REAR2_DUAL_TILT_SX                 0x09A8
#define FROM_REAR2_DUAL_TILT_SY                 0x09AC
#define FROM_REAR2_DUAL_TILT_RANGE              0x0FCC
#define FROM_REAR2_DUAL_TILT_MAX_ERR            0x0FD0
#define FROM_REAR2_DUAL_TILT_AVG_ERR            0x0FD4
#define FROM_REAR2_DUAL_TILT_DLL_VERSION        0x08E8
#define FROM_REAR2_DUAL_CAL_ADDR                0x08E8
#define FROM_REAR2_DUAL_CAL_SIZE                2048
#endif

#if defined(CONFIG_SEC_A42XQ_PROJECT) || defined(CONFIG_SEC_A52XQ_PROJECT) || defined(CONFIG_SEC_A42XUQ_PROJECT)
#define FROM_REAR_DUAL_CAL_ADDR                 0x1A80
#define FROM_REAR_DUAL_CAL_SIZE                 1024
#elif defined(CONFIG_SEC_A51XQ_PROJECT)
#define FROM_REAR_DUAL_CAL_ADDR                 0x1740
#define FROM_REAR_DUAL_CAL_SIZE                 1024
#else
#define FROM_REAR_DUAL_CAL_ADDR                 0x08E8
#define FROM_REAR_DUAL_CAL_SIZE                 1024
#endif

//rear3
#if defined(CONFIG_SEC_A42XQ_PROJECT) || defined(CONFIG_SEC_A42XUQ_PROJECT)
#define FROM_REAR3_SENSOR_ID_ADDR               0x1300
#define FROM_REAR3_DUAL_TILT_X                  0x1C18
#define FROM_REAR3_DUAL_TILT_Y                  0x1C1C
#define FROM_REAR3_DUAL_TILT_Z                  0x1C20
#define FROM_REAR3_DUAL_TILT_SX                 0x1C24
#define FROM_REAR3_DUAL_TILT_SY                 0x1C28
#define FROM_REAR3_DUAL_TILT_PROJECT_CAL_TYPE   0x1C2C
#define FROM_REAR3_DUAL_TILT_RANGE              0x1E64
#define FROM_REAR3_DUAL_TILT_MAX_ERR            0x1E68
#define FROM_REAR3_DUAL_TILT_AVG_ERR            0x1E6C
#define FROM_REAR3_DUAL_TILT_DLL_VERSION        0x1A80
#define FROM_REAR3_DUAL_CAL_ADDR                0x2C50
#define FROM_REAR3_DUAL_CAL_SIZE                2060
#elif defined(CONFIG_SEC_A52XQ_PROJECT)
#define FROM_REAR3_SENSOR_ID_ADDR               0x1300
#define FROM_REAR3_DUAL_TILT_X                  0x1C18
#define FROM_REAR3_DUAL_TILT_Y                  0x1C1C
#define FROM_REAR3_DUAL_TILT_Z                  0x1C20
#define FROM_REAR3_DUAL_TILT_SX                 0x1C24
#define FROM_REAR3_DUAL_TILT_SY                 0x1C28
#define FROM_REAR3_DUAL_TILT_PROJECT_CAL_TYPE   0x1C2C
#define FROM_REAR3_DUAL_TILT_RANGE              0x1E64
#define FROM_REAR3_DUAL_TILT_MAX_ERR            0x1E68
#define FROM_REAR3_DUAL_TILT_AVG_ERR            0x1E6C
#define FROM_REAR3_DUAL_TILT_DLL_VERSION        0x1A80
#define FROM_REAR3_DUAL_CAL_ADDR                0x1070
#define FROM_REAR3_DUAL_CAL_SIZE                2060
#elif defined(CONFIG_SEC_A51XQ_PROJECT)
#define FROM_REAR3_SENSOR_ID_ADDR               0xC8
#define FROM_REAR3_DUAL_TILT_X                  0x18D8
#define FROM_REAR3_DUAL_TILT_Y                  0x18DC
#define FROM_REAR3_DUAL_TILT_Z                  0x18E0
#define FROM_REAR3_DUAL_TILT_SX                 0x18E4
#define FROM_REAR3_DUAL_TILT_SY                 0x18E8
#define FROM_REAR3_DUAL_TILT_PROJECT_CAL_TYPE   0x18EC
#define FROM_REAR3_DUAL_TILT_RANGE              0x1B24
#define FROM_REAR3_DUAL_TILT_MAX_ERR            0x1B28
#define FROM_REAR3_DUAL_TILT_AVG_ERR            0x1B2C
#define FROM_REAR3_DUAL_TILT_DLL_VERSION        0x1740
#define FROM_REAR3_DUAL_CAL_ADDR                0x1740
#define FROM_REAR3_DUAL_CAL_SIZE                1024
#else
#define FROM_REAR3_SENSOR_ID_ADDR               0x1600
#define FROM_REAR3_DUAL_TILT_X                  0x0948
#define FROM_REAR3_DUAL_TILT_Y                  0x094C
#define FROM_REAR3_DUAL_TILT_Z                  0x0950
#define FROM_REAR3_DUAL_TILT_SX                 0x09A8
#define FROM_REAR3_DUAL_TILT_SY                 0x09AC
#define FROM_REAR3_DUAL_TILT_RANGE              0x0FCC
#define FROM_REAR3_DUAL_TILT_MAX_ERR            0x0FD0
#define FROM_REAR3_DUAL_TILT_AVG_ERR            0x0FD4
#define FROM_REAR3_DUAL_TILT_DLL_VERSION        0x08E8
#define FROM_REAR3_DUAL_CAL_ADDR                0x08E8
#define FROM_REAR3_DUAL_CAL_SIZE                1024
#endif
#endif

#define FROM_REAR_AF_CAL_SIZE                   10
#define FROM_SENSOR_ID_SIZE                     16

/* MTF exif : 0x0064~0x0099(54Byte) for FROM, EEPROM */
#define FROM_REAR_MTF_ADDR                      0x0160
#define FROM_REAR_MTF2_ADDR                     0x0196
#define FROM_FRONT_MTF_ADDR                     0x0080
#if defined(CONFIG_SAMSUNG_REAR_TRIPLE)
#define FROM_REAR3_MTF_ADDR                     0x084A
#define FROM_REAR2_MTF_ADDR                     0x0072
#endif
#define FROM_MTF_SIZE                           54
#define SYSFS_FW_VER_SIZE                       40
#define SYSFS_MODULE_INFO_SIZE                  96
#define FROM_CAL_MAP_VERSION                    0x32
#define MODULE_VER_ON_PVR                       0x42
#define MODULE_VER_ON_SRA                       0x4D
#define REAR_PAF_CAL_INFO_SIZE                  1024

/*extern uint32_t front_af_cal_pan;
extern uint32_t front_af_cal_macro;*/
#if defined(CONFIG_SAMSUNG_REAR_TRIPLE)
extern uint8_t rear_dual_cal[FROM_REAR_DUAL_CAL_SIZE + 1];
extern uint8_t rear2_dual_cal[FROM_REAR2_DUAL_CAL_SIZE + 1];
extern uint8_t rear3_dual_cal[FROM_REAR3_DUAL_CAL_SIZE + 1];
extern int rear2_af_cal[FROM_REAR_AF_CAL_SIZE + 1];
extern int rear3_af_cal[FROM_REAR_AF_CAL_SIZE + 1];
extern int front_af_cal[FROM_REAR_AF_CAL_SIZE + 1];
#endif
extern int rear_af_cal[FROM_REAR_AF_CAL_SIZE + 1];
extern char rear_sensor_id[FROM_SENSOR_ID_SIZE + 1];
extern char front_sensor_id[FROM_SENSOR_ID_SIZE + 1];
#if defined(CONFIG_SAMSUNG_REAR_TRIPLE)
extern char rear2_sensor_id[FROM_SENSOR_ID_SIZE + 1];
extern int rear2_dual_tilt_x;
extern int rear2_dual_tilt_y;
extern int rear2_dual_tilt_z;
extern int rear2_dual_tilt_sx;
extern int rear2_dual_tilt_sy;
extern int rear2_dual_tilt_range;
extern int rear2_dual_tilt_max_err;
extern int rear2_dual_tilt_avg_err;
extern int rear2_dual_tilt_dll_ver;
extern char rear2_project_cal_type[PROJECT_CAL_TYPE_MAX_SIZE+1];
extern char rear3_sensor_id[FROM_SENSOR_ID_SIZE + 1];
extern int rear3_dual_tilt_x;
extern int rear3_dual_tilt_y;
extern int rear3_dual_tilt_z;
extern int rear3_dual_tilt_sx;
extern int rear3_dual_tilt_sy;
extern int rear3_dual_tilt_range;
extern int rear3_dual_tilt_max_err;
extern int rear3_dual_tilt_avg_err;
extern int rear3_dual_tilt_dll_ver;
extern char rear3_project_cal_type[PROJECT_CAL_TYPE_MAX_SIZE+1];
#endif
extern char rear_paf_cal_data_far[REAR_PAF_CAL_INFO_SIZE];
extern char rear_paf_cal_data_mid[REAR_PAF_CAL_INFO_SIZE];
extern uint32_t paf_err_data_result;
extern char rear_f2_paf_cal_data_far[REAR_PAF_CAL_INFO_SIZE];
extern char rear_f2_paf_cal_data_mid[REAR_PAF_CAL_INFO_SIZE];
extern uint32_t f2_paf_err_data_result;

extern uint8_t rear_module_id[FROM_MODULE_ID_SIZE + 1];
extern uint8_t front_module_id[FROM_MODULE_ID_SIZE + 1];

extern char front_mtf_exif[FROM_MTF_SIZE + 1];
extern char rear_mtf_exif[FROM_MTF_SIZE + 1];
extern char rear_mtf2_exif[FROM_MTF_SIZE + 1];

#if defined(CONFIG_SAMSUNG_REAR_TRIPLE)
extern char rear2_mtf_exif[FROM_MTF_SIZE + 1];
extern uint8_t rear2_module_id[FROM_MODULE_ID_SIZE + 1];
extern char rear2_mtf_exif[FROM_MTF_SIZE + 1];
extern char cam2_fw_ver[SYSFS_FW_VER_SIZE];
extern char cam2_fw_full_ver[SYSFS_FW_VER_SIZE];
extern char rear3_mtf_exif[FROM_MTF_SIZE + 1];
extern char cam3_fw_ver[SYSFS_FW_VER_SIZE];
extern char cam3_fw_full_ver[SYSFS_FW_VER_SIZE];
#endif

extern char cam_fw_ver[SYSFS_FW_VER_SIZE];
extern char cam_fw_full_ver[SYSFS_FW_VER_SIZE];
extern char front_cam_fw_ver[SYSFS_FW_VER_SIZE];
extern char front_cam_fw_full_ver[SYSFS_FW_VER_SIZE];
extern char cam_fw_factory_ver[SYSFS_FW_VER_SIZE];
extern char cam_fw_user_ver[SYSFS_FW_VER_SIZE];
extern char front_cam_fw_user_ver[SYSFS_FW_VER_SIZE];
extern char front_cam_fw_factory_ver[SYSFS_FW_VER_SIZE];
extern char cal_crc[SYSFS_FW_VER_SIZE];

extern char front_module_info[SYSFS_MODULE_INFO_SIZE];
extern char module_info[SYSFS_MODULE_INFO_SIZE];

/* phone fw info */
#define HW_INFO_MAX_SIZE 6
#define SW_INFO_MAX_SIZE 5
#define VENDOR_INFO_MAX_SIZE 2
#define PROCESS_INFO_MAX_SIZE 2

#if defined(CONFIG_SAMSUNG_REAR_TRIPLE)

#if defined(CONFIG_SEC_A42XQ_PROJECT) || defined(CONFIG_SEC_A42XUQ_PROJECT)
#define HW_INFO                                 ("E48QL")
#define SW_INFO                                 ("NDR0")
#define VENDOR_INFO                             ("N")
#define PROCESS_INFO                            ("M")
#define CRITERION_REV                           (0)
#elif defined(CONFIG_SEC_A51XQ_PROJECT)
#define HW_INFO                                 ("C48QS")
#define SW_INFO                                 ("NCR0")
#define VENDOR_INFO                             ("C")
#define PROCESS_INFO                            ("A")
#define CRITERION_REV                           (0)
#elif defined(CONFIG_SEC_A52XQ_PROJECT)
#define HW_INFO                                 ("F64ES")
#define SW_INFO                                 ("NGR0")
#define VENDOR_INFO                             ("S")
#define PROCESS_INFO                            ("A")
#define CRITERION_REV                           (0)
#elif defined(CONFIG_SEC_GTS7XLLITE_PROJECT) || defined(CONFIG_SEC_GTS7XLLITEWIFI_PROJECT)
#define HW_INFO                                 ("M08ES")
#define SW_INFO                                 ("NKR0")
#define VENDOR_INFO                             ("A")
#define PROCESS_INFO                            ("A")
#define CRITERION_REV                           (0)
#else
#define HW_INFO                                 ("B32QL")
#define SW_INFO                                 ("LA00")
#define VENDOR_INFO                             ("M")
#define PROCESS_INFO                            ("A")
#define CRITERION_REV                           (0)
#endif

#if defined(CONFIG_SEC_A42XQ_PROJECT) || defined(CONFIG_SEC_A42XUQ_PROJECT)
#define HW_INFO_ULTRA_WIDE                      ("D08QL")
#define SW_INFO_ULTRA_WIDE                      ("MDR0")
#define VENDOR_INFO_ULTRA_WIDE                  ("H")
#define PROCESS_INFO_ULTRA_WIDE                 ("M")
#define CRITERION_REV_ULTRA_WIDE                (0)
#elif defined(CONFIG_SEC_A52XQ_PROJECT)
#define HW_INFO_ULTRA_WIDE                      ("E12EL")
#define SW_INFO_ULTRA_WIDE                      ("NGR0")
#define VENDOR_INFO_ULTRA_WIDE                  ("C")
#define PROCESS_INFO_ULTRA_WIDE                 ("A")
#define CRITERION_REV_ULTRA_WIDE                 (0)
#elif defined(CONFIG_SEC_A51XQ_PROJECT)
#define HW_INFO_ULTRA_WIDE                      ("I13QF")
#define SW_INFO_ULTRA_WIDE                      ("MHR0")
#define VENDOR_INFO_ULTRA_WIDE                  ("C")
#define PROCESS_INFO_ULTRA_WIDE                 ("A")
#define CRITERION_REV_ULTRA_WIDE                (0)
#else
#define HW_INFO_ULTRA_WIDE                      ("C08QL")
#define SW_INFO_ULTRA_WIDE                      ("LA00")
#define VENDOR_INFO_ULTRA_WIDE                  ("P")
#define PROCESS_INFO_ULTRA_WIDE                 ("A")
#define CRITERION_REV_ULTRA_WIDE                (0)
#endif

#if defined(CONFIG_SEC_A42XQ_PROJECT)
#define HW_INFO_BOKEH                           ("E05QG")
#define SW_INFO_BOKEH                           ("MLR0")
#define VENDOR_INFO_BOKEH                       ("N")
#define PROCESS_INFO_BOKEH                      ("M")
#define CRITERION_REV_BOKEH                      (0)
#elif defined(CONFIG_SEC_A42XUQ_PROJECT)
#define HW_INFO_BOKEH                           ("C02EG")
#define SW_INFO_BOKEH                           ("NER0")
#define VENDOR_INFO_BOKEH                       ("A")
#define PROCESS_INFO_BOKEH                      ("M")
#define CRITERION_REV_BOKEH                      (0)
#elif defined(CONFIG_SEC_A52XQ_PROJECT)
#define HW_INFO_BOKEH                           ("E05QG")
#define SW_INFO_BOKEH                           ("MLR0")
#define VENDOR_INFO_BOKEH                       ("H")
#define PROCESS_INFO_BOKEH                      ("A")
#define CRITERION_REV_BOKEH                      (0)
#elif defined(CONFIG_SEC_A51XQ_PROJECT)
#define HW_INFO_BOKEH                           ("E05QG")
#define SW_INFO_BOKEH                           ("MLR0")
#define VENDOR_INFO_BOKEH                       ("C")
#define PROCESS_INFO_BOKEH                      ("A")
#define CRITERION_REV_BOKEH                      (0)
#else
#define HW_INFO_BOKEH                           ("U05QG")
#define SW_INFO_BOKEH                           ("LA00")
#define VENDOR_INFO_BOKEH                       ("M")
#define PROCESS_INFO_BOKEH                      ("A")
#define CRITERION_REV_BOKEH                      (0)
#endif
extern uint8_t rear3_module_id[FROM_MODULE_ID_SIZE + 1];

#else
#define HW_INFO                                 ("H12QS")
#define SW_INFO                                 ("KK01")
#define VENDOR_INFO                             ("V")
#define PROCESS_INFO                            ("A")
#define CRITERION_REV                           (14)
#endif

#if defined(CONFIG_SEC_A52XQ_PROJECT)
#define HW_INFO_MACRO                           ("N05EG")
#define SW_INFO_MACRO                           ("NGR0")
#define VENDOR_INFO_MACRO                       ("H")
#define PROCESS_INFO_MACRO                      ("A")
#define CRITERION_REV_MACRO                      (0)
#endif

#if defined(CONFIG_SEC_A42XQ_PROJECT)
#define FRONT_HW_INFO                           ("A20QF")
#define FRONT_SW_INFO                           ("MDF0")
#define FRONT_VENDOR_INFO                       ("H")
#define FRONT_PROCESS_INFO                      ("M")
#define CRITERION_REV_FRONT                      (0)
#elif defined(CONFIG_SEC_A51XQ_PROJECT)
#define FRONT_HW_INFO                           ("A32QS")
#define FRONT_SW_INFO                           ("NCF0")
#define FRONT_VENDOR_INFO                       ("N")
#define FRONT_PROCESS_INFO                      ("A")
#define CRITERION_REV_FRONT                      (0)
#elif defined(CONFIG_SEC_A42XUQ_PROJECT)
#define FRONT_HW_INFO                           ("D13EL")
#define FRONT_SW_INFO                           ("NEF0")
#define FRONT_VENDOR_INFO                       ("P")
#define FRONT_PROCESS_INFO                      ("A")
#define CRITERION_REV_FRONT                      (0)
#elif defined(CONFIG_SEC_A52XQ_PROJECT)
#define FRONT_HW_INFO                           ("C32ES")
#define FRONT_SW_INFO                           ("NGF0")
#define FRONT_VENDOR_INFO                       ("N")
#define FRONT_PROCESS_INFO                      ("A")
#define CRITERION_REV_FRONT                      (0)
#elif defined(CONFIG_SEC_GTS7XLLITE_PROJECT) || defined(CONFIG_SEC_GTS7XLLITEWIFI_PROJECT)
#define FRONT_HW_INFO                           ("S05EG")
#define FRONT_SW_INFO                           ("NKF0")
#define FRONT_VENDOR_INFO                       ("M")
#define FRONT_PROCESS_INFO                      ("A")
#define CRITERION_REV_FRONT                      (0)
#else
#define FRONT_HW_INFO                           ("B32QL")
#define FRONT_SW_INFO                           ("LA00")
#define FRONT_VENDOR_INFO                       ("M")
#define FRONT_PROCESS_INFO                      ("A")
#define CRITERION_REV_FRONT                      (10)
#endif



#define SW_CAM_MAP_VERSION_ADDR                 (0x0050 + 0x03)

#if defined(CONFIG_SAMSUNG_REAR_TOF)
#define REAR_TOF_FROM_CAL_MAP_VERSION        ('3')
#define REAR_TOF_MODULE_VER_ON_PVR           ('B')
#define REAR_TOF_MODULE_VER_ON_SRA           ('M')
#define REAR_TOF_CAM_MAP_VERSION_ADDR        (0x0050 + 0x03)
#define REAR_TOF_DLL_VERSION_ADDR            (0x0054 + 0x03)
#define REAR_TOF_MODULE_FW_VERSION           0x0040
#define REAR_TOF_MODULE_ID_ADDR              0x00A2
#define REAR_TOF_SENSOR_ID_ADDR              0x00B2

#define REAR_TOFCAL_START_ADDR               0x0100
#define REAR_TOFCAL_END_ADDR                 0x11A3
#define REAR_TOFCAL_TOTAL_SIZE	             (REAR_TOFCAL_END_ADDR - REAR_TOFCAL_START_ADDR + 1)
#define REAR_TOFCAL_SIZE                     (4096 - 1)
#define REAR_TOFCAL_EXTRA_SIZE               (REAR_TOFCAL_TOTAL_SIZE - REAR_TOFCAL_SIZE)
#define REAR_TOFCAL_UID_ADDR                 0x11A4 + 16 //kkpark TEMP
#define REAR_TOFCAL_UID                      (REAR_TOFCAL_UID_ADDR + 0x0000)
#define REAR_TOFCAL_RESULT_ADDR              0x00CA

#define REAR_TOF_DUAL_CAL_ADDR               0x08E6
#define REAR_TOF_DUAL_CAL_END_ADDR           0x0CE5
#define REAR_TOF_DUAL_CAL_SIZE               (REAR_TOF_DUAL_CAL_END_ADDR - REAR_TOF_DUAL_CAL_ADDR + 1)
#define REAR_TOF_DUAL_TILT_DLL_VERSION       0x08E6
#define REAR_TOF_DUAL_TILT_X                 0x09A6
#define REAR_TOF_DUAL_TILT_Y                 0x09AA
#define REAR_TOF_DUAL_TILT_Z                 0x09AE
#define REAR_TOF_DUAL_TILT_SX                0x09CA
#define REAR_TOF_DUAL_TILT_SY                0x09CE
#define REAR_TOF_DUAL_TILT_AVG_ERR           0x0CCE
#define REAR_TOF_DUAL_TILT_MAX_ERR           0x0CD2
#define REAR_TOF_DUAL_TILT_RANGE             0x0CD6

#define REAR2_TOF_DUAL_CAL_ADDR              0xB800
#define REAR2_TOF_DUAL_TILT_DLL_VERSION      (REAR2_TOF_DUAL_CAL_ADDR + 0x0000)
#define REAR2_TOF_DUAL_TILT_X                (REAR2_TOF_DUAL_CAL_ADDR + 0x0160)
#define REAR2_TOF_DUAL_TILT_Y                (REAR2_TOF_DUAL_CAL_ADDR + 0x0164)
#define REAR2_TOF_DUAL_TILT_Z                (REAR2_TOF_DUAL_CAL_ADDR + 0x0168)
#define REAR2_TOF_DUAL_TILT_SX               (REAR2_TOF_DUAL_CAL_ADDR + 0x05C8)
#define REAR2_TOF_DUAL_TILT_SY               (REAR2_TOF_DUAL_CAL_ADDR + 0x05CC)
#define REAR2_TOF_DUAL_TILT_RANGE            (REAR2_TOF_DUAL_CAL_ADDR + 0x06E8)
#define REAR2_TOF_DUAL_TILT_MAX_ERR          (REAR2_TOF_DUAL_CAL_ADDR + 0x06EC)
#define REAR2_TOF_DUAL_TILT_AVG_ERR          (REAR2_TOF_DUAL_CAL_ADDR + 0x06F0)

extern char cam_tof_fw_ver[SYSFS_FW_VER_SIZE];
extern char cam_tof_fw_full_ver[SYSFS_FW_VER_SIZE];
extern char cam_tof_fw_user_ver[SYSFS_FW_VER_SIZE];
extern char cam_tof_fw_factory_ver[SYSFS_FW_VER_SIZE];
extern char rear_tof_module_info[SYSFS_MODULE_INFO_SIZE];
extern char rear_tof_sensor_id[FROM_SENSOR_ID_SIZE + 1];
extern uint8_t rear3_module_id[FROM_MODULE_ID_SIZE + 1];

extern int rear_tof_uid;
extern uint8_t rear_tof_cal[REAR_TOFCAL_SIZE + 1];
extern uint8_t rear_tof_cal_extra[REAR_TOFCAL_EXTRA_SIZE + 1];
extern uint8_t rear_tof_cal_result;

extern uint8_t rear_tof_dual_cal[REAR_TOF_DUAL_CAL_SIZE + 1];
extern int rear_tof_dual_tilt_x;
extern int rear_tof_dual_tilt_y;
extern int rear_tof_dual_tilt_z;
extern int rear_tof_dual_tilt_sx;
extern int rear_tof_dual_tilt_sy;
extern int rear_tof_dual_tilt_range;
extern int rear_tof_dual_tilt_max_err;
extern int rear_tof_dual_tilt_avg_err;
extern int rear_tof_dual_tilt_dll_ver;

extern int rear2_tof_dual_tilt_x;
extern int rear2_tof_dual_tilt_y;
extern int rear2_tof_dual_tilt_z;
extern int rear2_tof_dual_tilt_sx;
extern int rear2_tof_dual_tilt_sy;
extern int rear2_tof_dual_tilt_range;
extern int rear2_tof_dual_tilt_max_err;
extern int rear2_tof_dual_tilt_avg_err;
extern int rear2_tof_dual_tilt_dll_ver;
#endif
#if defined(CONFIG_SAMSUNG_REAR_QUAD)
#define REAR4_FROM_CAL_MAP_VERSION        ('3')
#define REAR4_MODULE_VER_ON_PVR           ('B')
#define REAR4_MODULE_VER_ON_SRA           ('M')
#if defined (CONFIG_SEC_A52XQ_PROJECT)
#define REAR4_CAM_MAP_VERSION_ADDR        0x10D0
#define REAR4_DLL_VERSION_ADDR            0x10F0
#define REAR4_MODULE_FW_VERSION           0x1070
#define REAR4_MODULE_ID_ADDR              0x0032
#define REAR4_SENSOR_ID_ADDR              0x11E0
#else
#define REAR4_CAM_MAP_VERSION_ADDR        0x0090
#define REAR4_DLL_VERSION_ADDR            0x0094
#define REAR4_MODULE_FW_VERSION           0x005E
#define REAR4_MODULE_ID_ADDR              0x00AE
#define REAR4_SENSOR_ID_ADDR              0x00B8
#endif
#define FROM_REAR4_AF_CAL_D10_ADDR               0x010C
#define FROM_REAR4_AF_CAL_PAN_ADDR               0x0110
#define FROM_REAR4_AF_CAL_MACRO_ADDR             0x010C

extern char cam4_fw_ver[SYSFS_FW_VER_SIZE];
extern char cam4_fw_full_ver[SYSFS_FW_VER_SIZE];
extern char cam4_fw_user_ver[SYSFS_FW_VER_SIZE];
extern char cam4_fw_factory_ver[SYSFS_FW_VER_SIZE];
extern char rear4_module_info[SYSFS_MODULE_INFO_SIZE];
extern char rear4_sensor_id[FROM_SENSOR_ID_SIZE + 1];
extern uint8_t rear4_module_id[FROM_MODULE_ID_SIZE + 1];
extern char rear4_mtf_exif[FROM_MTF_SIZE + 1];
extern int rear4_af_cal[FROM_REAR_AF_CAL_SIZE + 1];
#endif

#if defined(CONFIG_SAMSUNG_FRONT_TOF)
#define FRONT_TOF_FROM_CAL_MAP_VERSION       ('3')
#define FRONT_TOF_MODULE_VER_ON_PVR          ('B')
#define FRONT_TOF_MODULE_VER_ON_SRA          ('M')
#define FRONT_TOF_CAM_MAP_VERSION_ADDR       (0x0050 + 0x03)
#define FRONT_TOF_DLL_VERSION_ADDR           (0x0054 + 0x03)
#define FRONT_TOF_MODULE_FW_VERSION          0x0040
#define FRONT_TOF_MODULE_ID_ADDR             0x00A2
#define FRONT_TOF_SENSOR_ID_ADDR             0x00B2

#define FRONT_TOFCAL_START_ADDR              0x0100
#define FRONT_TOFCAL_END_ADDR                0x11A3
#define FRONT_TOFCAL_TOTAL_SIZE              (FRONT_TOFCAL_END_ADDR - FRONT_TOFCAL_START_ADDR + 1)
#define FRONT_TOFCAL_SIZE                    (4096 - 1)
#define FRONT_TOFCAL_EXTRA_SIZE              (FRONT_TOFCAL_TOTAL_SIZE - FRONT_TOFCAL_SIZE)
#define FRONT_TOFCAL_UID_ADDR                0x11A4
#define FRONT_TOFCAL_UID                     (FRONT_TOFCAL_UID_ADDR + 0x0000)
#define FRONT_TOFCAL_RESULT_ADDR             0x00CA

#if 1
#define FRONT_TOF_DUAL_CAL_ADDR               0x08E6
#define FRONT_TOF_DUAL_CAL_END_ADDR           0x0CE5
#define FRONT_TOF_DUAL_CAL_SIZE               (FRONT_TOF_DUAL_CAL_END_ADDR - FRONT_TOF_DUAL_CAL_ADDR + 1)
#define FRONT_TOF_DUAL_TILT_DLL_VERSION       0x08E6
#define FRONT_TOF_DUAL_TILT_X                 0x09A6
#define FRONT_TOF_DUAL_TILT_Y                 0x09AA
#define FRONT_TOF_DUAL_TILT_Z                 0x09AE
#define FRONT_TOF_DUAL_TILT_SX                0x09CA
#define FRONT_TOF_DUAL_TILT_SY                0x09CE
#define FRONT_TOF_DUAL_TILT_AVG_ERR           0x0CCE
#define FRONT_TOF_DUAL_TILT_MAX_ERR           0x0CD2
#define FRONT_TOF_DUAL_TILT_RANGE             0x0CD6
#else
#define FRONT_TOF_DUAL_CAL_ADDR              0x2200
#define FRONT_TOF_DUAL_CAL_END_ADDR          0x29FF
#define FRONT_TOF_DUAL_CAL_SIZE              (FRONT_TOF_DUAL_CAL_END_ADDR - FRONT_TOF_DUAL_CAL_ADDR + 1)
#define FRONT_TOF_DUAL_TILT_DLL_VERSION      (FRONT_TOF_DUAL_CAL_ADDR + 0x0000)
#define FRONT_TOF_DUAL_TILT_X                (FRONT_TOF_DUAL_CAL_ADDR + 0x04B8)
#define FRONT_TOF_DUAL_TILT_Y                (FRONT_TOF_DUAL_CAL_ADDR + 0x04BC)
#define FRONT_TOF_DUAL_TILT_Z                (FRONT_TOF_DUAL_CAL_ADDR + 0x04C0)
#define FRONT_TOF_DUAL_TILT_SX               (FRONT_TOF_DUAL_CAL_ADDR + 0x04DC)
#define FRONT_TOF_DUAL_TILT_SY               (FRONT_TOF_DUAL_CAL_ADDR + 0x04E0)
#define FRONT_TOF_DUAL_TILT_RANGE            (FRONT_TOF_DUAL_CAL_ADDR + 0x07E4)
#define FRONT_TOF_DUAL_TILT_MAX_ERR          (FRONT_TOF_DUAL_CAL_ADDR + 0x07E8)
#define FRONT_TOF_DUAL_TILT_AVG_ERR          (FRONT_TOF_DUAL_CAL_ADDR + 0x07EC)
#endif

extern char front_tof_cam_fw_ver[SYSFS_FW_VER_SIZE];
extern char front_tof_cam_fw_full_ver[SYSFS_FW_VER_SIZE];
extern char front_tof_cam_fw_user_ver[SYSFS_FW_VER_SIZE];
extern char front_tof_cam_fw_factory_ver[SYSFS_FW_VER_SIZE];
extern char front_tof_module_info[SYSFS_MODULE_INFO_SIZE];
extern char front_tof_sensor_id[FROM_SENSOR_ID_SIZE + 1];
extern uint8_t front2_module_id[FROM_MODULE_ID_SIZE + 1];

extern int front_tof_uid;
extern uint8_t front_tof_cal[FRONT_TOFCAL_SIZE + 1];
extern uint8_t front_tof_cal_extra[FRONT_TOFCAL_EXTRA_SIZE+1];
extern uint8_t front_tof_cal_result;

extern uint8_t front_tof_dual_cal[FRONT_TOF_DUAL_CAL_SIZE + 1];
extern int front_tof_dual_tilt_x;
extern int front_tof_dual_tilt_y;
extern int front_tof_dual_tilt_z;
extern int front_tof_dual_tilt_sx;
extern int front_tof_dual_tilt_sy;
extern int front_tof_dual_tilt_range;
extern int front_tof_dual_tilt_max_err;
extern int front_tof_dual_tilt_avg_err;
extern int front_tof_dual_tilt_dll_ver;
#endif

/**
 * struct cam_eeprom_map_t - eeprom map
 * @data_type       :   Data type
 * @addr_type       :   Address type
 * @addr            :   Address
 * @data            :   data
 * @delay           :   Delay
 *
 */
struct cam_eeprom_map_t {
	uint32_t valid_size;
	uint32_t addr;
	uint32_t addr_type;
	uint32_t data;
	uint32_t data_type;
	uint32_t delay;
};

enum cam_eeprom_state {
	CAM_EEPROM_INIT,
	CAM_EEPROM_ACQUIRE,
	CAM_EEPROM_CONFIG,
};

/**
 * struct cam_eeprom_memory_map_t - eeprom memory map types
 * @page            :   page memory
 * @pageen          :   pageen memory
 * @poll            :   poll memory
 * @mem             :   mem
 * @saddr           :   slave addr
 *
 */
struct cam_eeprom_memory_map_t {
	struct cam_eeprom_map_t page;
	struct cam_eeprom_map_t pageen;
	struct cam_eeprom_map_t poll;
	struct cam_eeprom_map_t mem;
	uint32_t saddr;
};

/**
 * struct cam_eeprom_memory_block_t - eeprom mem block info
 * @map             :   eeprom memory map
 * @num_map         :   number of map blocks
 * @mapdata         :   map data
 * @cmd_type        :   size of total mapdata
 *
 */
struct cam_eeprom_memory_block_t {
	struct cam_eeprom_memory_map_t *map;
	uint32_t num_map;
	uint8_t *mapdata;
	uint32_t num_data;
	uint16_t is_supported;
};

/**
 * struct cam_eeprom_cmm_t - camera multimodule
 * @cmm_support     :   cmm support flag
 * @cmm_compression :   cmm compression flag
 * @cmm_offset      :   cmm data start offset
 * @cmm_size        :   cmm data size
 *
 */
struct cam_eeprom_cmm_t {
	uint32_t cmm_support;
	uint32_t cmm_compression;
	uint32_t cmm_offset;
	uint32_t cmm_size;
};

/**
 * struct cam_eeprom_i2c_info_t - I2C info
 * @slave_addr      :   slave address
 * @i2c_freq_mode   :   i2c frequency mode
 *
 */
struct cam_eeprom_i2c_info_t {
	uint16_t slave_addr;
	uint8_t i2c_freq_mode;
};

/**
 * struct cam_eeprom_soc_private - eeprom soc private data structure
 * @eeprom_name     :   eeprom name
 * @i2c_info        :   i2c info structure
 * @power_info      :   eeprom power info
 * @cmm_data        :   cmm data
 *
 */
struct cam_eeprom_soc_private {
	const char *eeprom_name;
	struct cam_eeprom_i2c_info_t i2c_info;
	struct cam_sensor_power_ctrl_t power_info;
	struct cam_eeprom_cmm_t cmm_data;
};

/**
 * struct cam_eeprom_intf_params - bridge interface params
 * @device_hdl   : Device Handle
 * @session_hdl  : Session Handle
 * @ops          : KMD operations
 * @crm_cb       : Callback API pointers
 */
struct cam_eeprom_intf_params {
	int32_t device_hdl;
	int32_t session_hdl;
	int32_t link_hdl;
	struct cam_req_mgr_kmd_ops ops;
	struct cam_req_mgr_crm_cb *crm_cb;
};

struct eebin_info {
	uint32_t start_address;
	uint32_t size;
	uint32_t is_valid;
};

/**
 * struct cam_cmd_conditional_wait - Conditional wait command
 * @pdev            :   platform device
 * @spi             :   spi device
 * @eeprom_mutex    :   eeprom mutex
 * @soc_info        :   eeprom soc related info
 * @io_master_info  :   Information about the communication master
 * @gpio_num_info   :   gpio info
 * @cci_i2c_master  :   I2C structure
 * @v4l2_dev_str    :   V4L2 device structure
 * @bridge_intf     :   bridge interface params
 * @cam_eeprom_state:   eeprom_device_state
 * @userspace_probe :   flag indicates userspace or kernel probe
 * @cal_data        :   Calibration data
 * @device_name     :   Device name
 *
 */
struct cam_eeprom_ctrl_t {
	struct platform_device *pdev;
	struct spi_device *spi;
	struct mutex eeprom_mutex;
	struct cam_hw_soc_info soc_info;
	struct camera_io_master io_master_info;
	struct msm_camera_gpio_num_info *gpio_num_info;
	enum cci_i2c_master_t cci_i2c_master;
	enum cci_device_num cci_num;
	struct cam_subdev v4l2_dev_str;
	struct cam_eeprom_intf_params bridge_intf;
	enum msm_camera_device_type_t eeprom_device_type;
	enum cam_eeprom_state cam_eeprom_state;
	bool userspace_probe;
	struct cam_eeprom_memory_block_t cal_data;
	char device_name[20];
	uint32_t is_supported;
	uint16_t is_multimodule_node;
	struct i2c_settings_array wr_settings;
	struct eebin_info eebin_info;
	uint32_t open_cnt;
};

typedef enum{
	EEPROM_FW_VER = 1,
	PHONE_FW_VER,
	LOAD_FW_VER
} cam_eeprom_fw_version_idx;

typedef enum{
	CAM_EEPROM_IDX_WIDE,
	CAM_EEPROM_IDX_FRONT,
	CAM_EEPROM_IDX_ULTRA_WIDE,
	CAM_EEPROM_IDX_BOKEH,
#if defined(CONFIG_SAMSUNG_REAR_QUAD)
	CAM_EEPROM_IDX_BACK_MACRO,
#endif
#if defined(CONFIG_SAMSUNG_REAR_TOF)
	CAM_EEPROM_IDX_BACK_TOF,
#endif
#if defined(CONFIG_SAMSUNG_FRONT_TOF)
	CAM_EEPROM_IDX_FRONT_TOF,
#endif
	CAM_EEPROM_IDX_MAX
} cam_eeprom_idx_type;


int32_t cam_eeprom_update_i2c_info(struct cam_eeprom_ctrl_t *e_ctrl,
	struct cam_eeprom_i2c_info_t *i2c_info);

#endif /*_CAM_EEPROM_DEV_H_ */

#pragma once
#include <stdint.h>
#include "esp_cam_sensor_types.h"

#define OV02C10_SCCB_ADDR    0x36
#define OV02C10_PID          0x5602
#define OV02C10_SENSOR_NAME  "OV02C10"

#define OV02C10_EXPOSURE_MIN 4
#define OV02C10_EXPOSURE_MAX 2320
#define OV02C10_GAIN_MIN     0x10    // 1배
#define OV02C10_GAIN_MAX     0xf8
#define OV02C10_DGAIN_MIN    0x0400  // 1배
#define OV02C10_DGAIN_MAX    0x1000  // 4배, 그 이상은 잡음이 심하다

esp_cam_sensor_device_t *ov02c10_detect(esp_cam_sensor_config_t *config);

// ISP 자동 노출이 없는 구성이라 응용 코드가 밝기를 보고 직접 조절한다
esp_err_t ov02c10_set_exposure(uint16_t lines, uint8_t gain, uint16_t digital_gain);

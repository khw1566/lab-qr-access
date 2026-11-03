// OV02C10 MIPI 2레인 RAW10 드라이버. esp_cam_sensor 2.6에 없는 센서라 같은 구조로 작성했다.
// 레지스터 값은 Linux drivers/media/i2c/ov02c10.c (GPL-2.0, Intel)의 1928x1092 2레인 설정에서 가져왔고,
// ESP32-P4 ISP 최대 해상도에 맞춰 출력 크기만 1920x1080으로 줄였다.
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_cam_sensor.h"
#include "esp_cam_sensor_detect.h"
#include "esp_sccb_intf.h"
#include "ov02c10.h"

static const char *TAG = "ov02c10";

#define REG_END    0xffff
#define REG_DELAY  0xfffe
#define COUNT(a)   (sizeof(a) / sizeof((a)[0]))

// Linux 드라이버 기준 값. 보드의 센서 클럭이 19.2 MHz가 아니면 레인 속도도 같은 비율로 달라진다.
#define XCLK_HZ        19200000
#define LANE_RATE_HZ   800000000ULL
#define PIXEL_RATE_HZ  160000000

typedef struct {
    uint16_t reg;
    uint8_t val;
} reg_t;

static const reg_t s_reset_regs[] = {
    {0x0100, 0x00}, {0x0103, 0x01}, {REG_DELAY, 10}, {REG_END, 0},
};

static const reg_t s_1920x1080_regs[] = {
    {0x0301, 0x08}, {0x0303, 0x06}, {0x0304, 0x01}, {0x0305, 0xe0}, {0x0313, 0x40}, {0x031c, 0x4f},
    {0x3020, 0x97}, {0x3022, 0x01}, {0x3026, 0xb4}, {0x303b, 0x00}, {0x303c, 0x4f}, {0x303d, 0xe6},
    {0x303e, 0x00}, {0x303f, 0x03}, {0x3021, 0x23}, {0x3501, 0x04}, {0x3502, 0x6c}, {0x3504, 0x0c},
    {0x3507, 0x00}, {0x3508, 0x08}, {0x3509, 0x00}, {0x350a, 0x01}, {0x350b, 0x00}, {0x350c, 0x41},
    {0x3600, 0x84}, {0x3603, 0x08}, {0x3610, 0x57}, {0x3611, 0x1b}, {0x3613, 0x78}, {0x3623, 0x00},
    {0x3632, 0xa0}, {0x3642, 0xe8}, {0x364c, 0x70}, {0x365f, 0x0f}, {0x3708, 0x30}, {0x3714, 0x24},
    {0x3725, 0x02}, {0x3737, 0x08}, {0x3739, 0x28}, {0x3749, 0x32}, {0x374a, 0x32}, {0x374b, 0x32},
    {0x374c, 0x32}, {0x374d, 0x81}, {0x374e, 0x81}, {0x374f, 0x81}, {0x3752, 0x36}, {0x3753, 0x36},
    {0x3754, 0x36}, {0x3761, 0x00}, {0x376c, 0x81}, {0x3774, 0x18}, {0x3776, 0x08}, {0x377c, 0x81},
    {0x377d, 0x81}, {0x377e, 0x81}, {0x37a0, 0x44}, {0x37a6, 0x44}, {0x37aa, 0x0d}, {0x37ae, 0x00},
    {0x37cb, 0x03}, {0x37cc, 0x01}, {0x37d8, 0x02}, {0x37d9, 0x10}, {0x37e1, 0x10}, {0x37e2, 0x18},
    {0x37e3, 0x08}, {0x37e4, 0x08}, {0x37e5, 0x02}, {0x37e6, 0x08},
    {0x3800, 0x00}, {0x3801, 0x00}, {0x3802, 0x00}, {0x3803, 0x00}, {0x3804, 0x07}, {0x3805, 0x8f},
    {0x3806, 0x04}, {0x3807, 0x47}, {0x3808, 0x07}, {0x3809, 0x80}, {0x380a, 0x04}, {0x380b, 0x38},
    {0x3814, 0x01}, {0x3815, 0x01}, {0x3816, 0x01}, {0x3817, 0x01},
    {0x3820, 0xa8}, {0x3821, 0x00}, {0x3822, 0x80}, {0x3823, 0x08}, {0x3824, 0x00}, {0x3825, 0x20},
    {0x3826, 0x00}, {0x3827, 0x08}, {0x382a, 0x00}, {0x382b, 0x08}, {0x382d, 0x00}, {0x382e, 0x00},
    {0x382f, 0x23}, {0x3834, 0x00}, {0x3839, 0x00}, {0x383a, 0xd1}, {0x383e, 0x03}, {0x393d, 0x29},
    {0x393f, 0x6e}, {0x394b, 0x06}, {0x394c, 0x06}, {0x394d, 0x08}, {0x394f, 0x01}, {0x3950, 0x01},
    {0x3951, 0x01}, {0x3952, 0x01}, {0x3953, 0x01}, {0x3954, 0x01}, {0x3955, 0x01}, {0x3956, 0x01},
    {0x3957, 0x0e}, {0x3958, 0x08}, {0x3959, 0x08}, {0x395a, 0x08}, {0x395b, 0x13}, {0x395c, 0x09},
    {0x395d, 0x05}, {0x395e, 0x02}, {0x395f, 0x00}, {0x395f, 0x00}, {0x3960, 0x00}, {0x3961, 0x00},
    {0x3962, 0x00}, {0x3963, 0x00}, {0x3964, 0x00}, {0x3965, 0x00}, {0x3966, 0x00}, {0x3967, 0x00},
    {0x3968, 0x01}, {0x3969, 0x01}, {0x396a, 0x01}, {0x396b, 0x01}, {0x396c, 0x10}, {0x396d, 0xf0},
    {0x396e, 0x11}, {0x396f, 0x00}, {0x3970, 0x37}, {0x3971, 0x37}, {0x3972, 0x37}, {0x3973, 0x37},
    {0x3974, 0x00}, {0x3975, 0x3c}, {0x3976, 0x3c}, {0x3977, 0x3c}, {0x3978, 0x3c}, {0x3c00, 0x0f},
    {0x3c20, 0x01}, {0x3c21, 0x08}, {0x3f00, 0x8b}, {0x3f02, 0x0f}, {0x4000, 0xc3}, {0x4001, 0xe0},
    {0x4002, 0x00}, {0x4003, 0x40}, {0x4008, 0x04}, {0x4009, 0x23}, {0x400a, 0x04}, {0x400b, 0x01},
    {0x4077, 0x06}, {0x4078, 0x00}, {0x4079, 0x1a}, {0x407a, 0x7f}, {0x407b, 0x01}, {0x4080, 0x03},
    {0x4081, 0x84}, {0x4308, 0x03}, {0x4309, 0xff}, {0x430d, 0x00}, {0x4806, 0x00}, {0x4813, 0x00},
    {0x4837, 0x10}, {0x4857, 0x05}, {0x4500, 0x07}, {0x4501, 0x00}, {0x4503, 0x00}, {0x450a, 0x04},
    {0x450e, 0x00}, {0x450f, 0x00}, {0x4900, 0x00}, {0x4901, 0x00}, {0x4902, 0x01}, {0x5001, 0x50},
    {0x5006, 0x00}, {0x5080, 0x40}, {0x5181, 0x2b}, {0x5202, 0xa3}, {0x5206, 0x01}, {0x5207, 0x00},
    {0x520a, 0x01}, {0x520b, 0x00}, {0x365d, 0x00}, {0x4815, 0x40}, {0x4816, 0x12}, {0x4f00, 0x01},
    // 2레인 설정
    {0x301b, 0xf0}, {0x3027, 0xf1}, {0x380c, 0x04}, {0x380d, 0x74}, {0x380e, 0x09}, {0x380f, 0x18},
    {0x394e, 0x0a}, {0x4041, 0x20}, {0x4884, 0x04}, {0x4800, 0x64}, {0x4d00, 0x03}, {0x4d01, 0xd8},
    {0x4d02, 0xba}, {0x4d03, 0xa0}, {0x4d04, 0xb7}, {0x4d05, 0x34}, {0x4d0d, 0x00}, {0x5000, 0xfd},
    {0x481f, 0x30}, {0x0303, 0x05}, {0x0305, 0x90}, {0x0316, 0x90}, {0x3016, 0x32},
    // Linux가 스트림 시작 시 컨트롤 기본값으로 쓰는 값
    {0x3810, 0x00}, {0x3811, 0x01}, {0x3812, 0x00}, {0x3813, 0x01},
    {0x3501, 0x09}, {0x3502, 0x10}, {0x3508, 0x01}, {0x3509, 0x00},
    {REG_END, 0},
};

static const esp_cam_sensor_isp_info_t s_isp_info = {
    .isp_v1_info = {
        .version = SENSOR_ISP_INFO_VERSION_DEFAULT,
        .pclk = PIXEL_RATE_HZ,
        .vts = 2328,
        .hts = 1140,
        .bayer_type = ESP_CAM_SENSOR_BAYER_GRBG,
    },
};

static const esp_cam_sensor_format_t s_formats[] = {
    {
        .name = "MIPI_2lane_RAW10_1920x1080_30fps",
        .format = ESP_CAM_SENSOR_PIXFORMAT_RAW10,
        .port = ESP_CAM_SENSOR_MIPI_CSI,
        .xclk = XCLK_HZ,
        .width = 1920,
        .height = 1080,
        .regs = s_1920x1080_regs,
        .regs_size = COUNT(s_1920x1080_regs),
        .fps = 30,
        .isp_info = &s_isp_info,
        .mipi_info = {
            .mipi_clk = LANE_RATE_HZ,
            .lane_num = 2,
            .line_sync_en = false,
        },
        .reserved = NULL,
    },
};

static esp_cam_sensor_device_t *s_dev;

static esp_err_t reg_read(esp_sccb_io_handle_t sccb, uint16_t reg, uint8_t *val)
{
    return esp_sccb_transmit_receive_reg_a16v8(sccb, reg, val);
}

static esp_err_t reg_write(esp_sccb_io_handle_t sccb, uint16_t reg, uint8_t val)
{
    return esp_sccb_transmit_reg_a16v8(sccb, reg, val);
}

static esp_err_t reg_write_array(esp_sccb_io_handle_t sccb, const reg_t *regs)
{
    for (; regs->reg != REG_END; regs++) {
        if (regs->reg == REG_DELAY) {
            vTaskDelay(pdMS_TO_TICKS(regs->val));
        } else {
            ESP_RETURN_ON_ERROR(reg_write(sccb, regs->reg, regs->val), TAG, "write 0x%04x", regs->reg);
        }
    }
    return ESP_OK;
}

esp_err_t ov02c10_set_exposure(uint16_t lines, uint8_t gain, uint16_t digital_gain)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    uint32_t digital = (uint32_t)digital_gain << 6;
    esp_err_t err = reg_write(s_dev->sccb_handle, 0x3501, lines >> 8);
    err |= reg_write(s_dev->sccb_handle, 0x3502, lines & 0xff);
    err |= reg_write(s_dev->sccb_handle, 0x3508, gain >> 4);
    err |= reg_write(s_dev->sccb_handle, 0x3509, (gain & 0x0f) << 4);
    err |= reg_write(s_dev->sccb_handle, 0x350a, digital >> 16);
    err |= reg_write(s_dev->sccb_handle, 0x350b, (digital >> 8) & 0xff);
    err |= reg_write(s_dev->sccb_handle, 0x350c, digital & 0xff);
    return err ? ESP_FAIL : ESP_OK;
}

static esp_err_t read_id(esp_cam_sensor_device_t *dev, esp_cam_sensor_id_t *id)
{
    uint8_t high, low;
    ESP_RETURN_ON_ERROR(reg_read(dev->sccb_handle, 0x300a, &high), TAG, "read id");
    ESP_RETURN_ON_ERROR(reg_read(dev->sccb_handle, 0x300b, &low), TAG, "read id");
    id->pid = (high << 8) | low;
    return ESP_OK;
}

static esp_err_t set_stream(esp_cam_sensor_device_t *dev, int enable)
{
    ESP_RETURN_ON_ERROR(reg_write(dev->sccb_handle, 0x0100, enable ? 1 : 0), TAG, "stream");
    dev->stream_status = enable;
    return ESP_OK;
}

static esp_err_t query_para_desc(esp_cam_sensor_device_t *dev, esp_cam_sensor_param_desc_t *desc)
{
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t get_para_value(esp_cam_sensor_device_t *dev, uint32_t id, void *arg, size_t size)
{
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t set_para_value(esp_cam_sensor_device_t *dev, uint32_t id, const void *arg, size_t size)
{
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t query_formats(esp_cam_sensor_device_t *dev, esp_cam_sensor_format_array_t *formats)
{
    formats->count = COUNT(s_formats);
    formats->format_array = s_formats;
    return ESP_OK;
}

static esp_err_t query_capability(esp_cam_sensor_device_t *dev, esp_cam_sensor_capability_t *cap)
{
    cap->fmt_raw = 1;
    return ESP_OK;
}

static esp_err_t set_format(esp_cam_sensor_device_t *dev, const esp_cam_sensor_format_t *format)
{
    if (!format) format = &s_formats[0];
    ESP_RETURN_ON_ERROR(reg_write_array(dev->sccb_handle, s_reset_regs), TAG, "reset");
    ESP_RETURN_ON_ERROR(reg_write_array(dev->sccb_handle, format->regs), TAG, "format");
    dev->cur_format = format;
    return ESP_OK;
}

static esp_err_t get_format(esp_cam_sensor_device_t *dev, esp_cam_sensor_format_t *format)
{
    if (!dev->cur_format) return ESP_FAIL;
    memcpy(format, dev->cur_format, sizeof(*format));
    return ESP_OK;
}

static esp_err_t priv_ioctl(esp_cam_sensor_device_t *dev, uint32_t cmd, void *arg)
{
    esp_cam_sensor_reg_val_t *reg = arg;
    uint8_t val;
    esp_err_t err;

    switch (cmd) {
    case ESP_CAM_SENSOR_IOC_HW_RESET:
        return ESP_OK;
    case ESP_CAM_SENSOR_IOC_SW_RESET:
        return reg_write_array(dev->sccb_handle, s_reset_regs);
    case ESP_CAM_SENSOR_IOC_S_REG:
        return reg_write(dev->sccb_handle, reg->regaddr, reg->value);
    case ESP_CAM_SENSOR_IOC_G_REG:
        err = reg_read(dev->sccb_handle, reg->regaddr, &val);
        if (err == ESP_OK) reg->value = val;
        return err;
    case ESP_CAM_SENSOR_IOC_S_STREAM:
        return set_stream(dev, *(int *)arg);
    case ESP_CAM_SENSOR_IOC_G_CHIP_ID:
        return read_id(dev, arg);
    default:
        return ESP_ERR_INVALID_ARG;
    }
}

static esp_err_t del(esp_cam_sensor_device_t *dev)
{
    if (dev == s_dev) s_dev = NULL;
    free(dev);
    return ESP_OK;
}

static const esp_cam_sensor_ops_t s_ops = {
    .query_para_desc = query_para_desc,
    .get_para_value = get_para_value,
    .set_para_value = set_para_value,
    .query_support_formats = query_formats,
    .query_support_capability = query_capability,
    .set_format = set_format,
    .get_format = get_format,
    .priv_ioctl = priv_ioctl,
    .del = del,
};

esp_cam_sensor_device_t *ov02c10_detect(esp_cam_sensor_config_t *config)
{
    if (!config) return NULL;
    esp_cam_sensor_device_t *dev = calloc(1, sizeof(*dev));
    if (!dev) return NULL;

    dev->name = (char *)OV02C10_SENSOR_NAME;
    dev->sccb_handle = config->sccb_handle;
    dev->xclk_pin = config->xclk_pin;
    dev->reset_pin = config->reset_pin;
    dev->pwdn_pin = config->pwdn_pin;
    dev->sensor_port = config->sensor_port;
    dev->ops = &s_ops;
    dev->cur_format = &s_formats[0];

    uint8_t rev = 0;
    if (read_id(dev, &dev->id) != ESP_OK || dev->id.pid != OV02C10_PID) {
        ESP_LOGE(TAG, "센서 ID 불일치: 0x%04x", dev->id.pid);
        free(dev);
        return NULL;
    }
    reg_read(dev->sccb_handle, 0x300c, &rev);
    ESP_LOGI(TAG, "OV02C10 감지, ID 0x%04x%02x", dev->id.pid, rev);
    s_dev = dev;
    return dev;
}

ESP_CAM_SENSOR_DETECT_FN(ov02c10_detect, ESP_CAM_SENSOR_MIPI_CSI, OV02C10_SCCB_ADDR)
{
    ((esp_cam_sensor_config_t *)config)->sensor_port = ESP_CAM_SENSOR_MIPI_CSI;
    return ov02c10_detect(config);
}

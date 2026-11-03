// MIPI-CSI 카메라 → (ISP) RGB565 → 그레이스케일 축소 → quirc 로 QR 디코딩
// esp_video(V4L2 스타일 API) 사용. ESP-IDF 5.5 + espressif/esp_video 1.x 기준.
#include <fcntl.h>
#include <math.h>
#include <string.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "linux/videodev2.h"
#include "esp_video_init.h"
#include "esp_video_device.h"
#include "ov02c10.h"
#include "quirc.h"
#include "sdkconfig.h"
#include "app.h"

static const char *TAG = "cam_qr";

#define BUF_COUNT 2
#define AE_TARGET 80
#define AE_MARGIN 15
#define AE_EVERY_FRAMES 3
#define DECODE_W 540            // QR 틀이 있는 가운데만 인식해 속도를 올린다
#define DECODE_EVERY_FRAMES 3
#define WB_GAIN_MAX 3.0f
#define BOOST_MAX 4.0f
#define PREVIEW_MID 0.35f
#define BLACK_LEVEL 0.05f
#define GAMMA 0.6f

static uint32_t s_exposure = OV02C10_EXPOSURE_MAX, s_gain = OV02C10_GAIN_MIN, s_digital = OV02C10_DGAIN_MIN;
static uint32_t s_decode_w, s_r_mean, s_g_mean, s_b_mean;
static uint16_t s_lut_r[32], s_lut_g[64], s_lut_b[32];
static qr_callback_t s_cb;
static int s_fd = -1;
static uint8_t *s_bufs[BUF_COUNT];
static uint32_t s_w, s_h, s_scale;

static esp_err_t camera_open(void)
{
    // 내장 카메라 SCCB는 보드 공용 I2C 버스(GPIO7/8)를 같이 쓴다
    esp_video_init_csi_config_t csi_cfg = {
        .sccb_config = {
            .init_sccb = false,
            .i2c_handle = board_i2c_bus(),
            .freq = 100000,
        },
        .reset_pin = CONFIG_LAB_CAM_RESET_GPIO,
        .pwdn_pin = CONFIG_LAB_CAM_PWDN_GPIO,
        .dont_init_ldo = true,  // CSI와 DSI가 같은 LDO를 쓰며 display.c가 이미 켰다
    };
    const esp_video_init_config_t cfg = { .csi = &csi_cfg };
    ESP_RETURN_ON_ERROR(esp_video_init(&cfg), TAG, "esp_video_init 실패 (센서/핀 설정 확인)");

    s_fd = open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDONLY);
    if (s_fd < 0) { ESP_LOGE(TAG, "카메라 장치 열기 실패"); return ESP_FAIL; }

    struct v4l2_format fmt = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE };
    ioctl(s_fd, VIDIOC_G_FMT, &fmt);       // 센서 기본 해상도 사용
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
    if (ioctl(s_fd, VIDIOC_S_FMT, &fmt) != 0) { ESP_LOGE(TAG, "RGB565 설정 실패"); return ESP_FAIL; }
    s_w = fmt.fmt.pix.width;
    s_h = fmt.fmt.pix.height;
    s_scale = (s_w > 800) ? 2 : 1;          // QR 인식 속도를 위해 큰 해상도는 1/2 축소
    s_decode_w = MIN(DECODE_W, s_w / s_scale);
    ESP_LOGI(TAG, "카메라 %lux%lu, 디코딩 %lux%lu", s_w, s_h, s_decode_w, s_h / s_scale);

    struct v4l2_requestbuffers req = { .count = BUF_COUNT, .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP };
    if (ioctl(s_fd, VIDIOC_REQBUFS, &req) != 0) return ESP_FAIL;
    for (int i = 0; i < BUF_COUNT; i++) {
        struct v4l2_buffer b = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP, .index = i };
        if (ioctl(s_fd, VIDIOC_QUERYBUF, &b) != 0) return ESP_FAIL;
        s_bufs[i] = mmap(NULL, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, s_fd, b.m.offset);
        if (ioctl(s_fd, VIDIOC_QBUF, &b) != 0) return ESP_FAIL;
    }
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    return ioctl(s_fd, VIDIOC_STREAMON, &type) == 0 ? ESP_OK : ESP_FAIL;
}

// 미리보기용 색 보정표. 프레임의 평균 색이 회색이 되도록 채널 이득을 잡고(gray world) 감마를 적용한다.
static void color_update(uint32_t r_mean, uint32_t g_mean, uint32_t b_mean)
{
    if (!r_mean || !g_mean || !b_mean) return;
    float r_gain = (float)g_mean / r_mean, b_gain = (float)g_mean / b_mean;
    if (r_gain > WB_GAIN_MAX) r_gain = WB_GAIN_MAX;
    if (r_gain < 1.0f / WB_GAIN_MAX) r_gain = 1.0f / WB_GAIN_MAX;
    if (b_gain > WB_GAIN_MAX) b_gain = WB_GAIN_MAX;
    if (b_gain < 1.0f / WB_GAIN_MAX) b_gain = 1.0f / WB_GAIN_MAX;
    // 평균 밝기가 낮아도 알아볼 수 있게 평균을 화면 중간 밝기로 올린다
    float boost = PREVIEW_MID / ((float)g_mean / 255.0f);
    if (boost > BOOST_MAX) boost = BOOST_MAX;
    if (boost < 1.0f) boost = 1.0f;
    for (int i = 0; i < 32; i++) {
        float value = (i * 8) / 255.0f - BLACK_LEVEL;
        if (value < 0) value = 0;
        float r = powf(fminf(value * boost * r_gain, 1.0f), GAMMA), b = powf(fminf(value * boost * b_gain, 1.0f), GAMMA);
        s_lut_r[i] = (uint16_t)(r * 31.0f + 0.5f) << 11;
        s_lut_b[i] = (uint16_t)(b * 31.0f + 0.5f);
    }
    for (int i = 0; i < 64; i++) {
        float value = (i * 4) / 255.0f - BLACK_LEVEL;
        if (value < 0) value = 0;
        s_lut_g[i] = (uint16_t)(powf(fminf(value * boost, 1.0f), GAMMA) * 63.0f + 0.5f) << 5;
    }
}

// RGB565(LE) → 8bit 그레이(가운데 DECODE_W 폭만), s_scale 배 축소.
// preview가 있으면 색 보정한 거울상으로 같이 채운다. 평균 밝기를 돌려준다.
static uint32_t downscale(const uint8_t *src, uint8_t *dst, uint16_t *preview)
{
    const uint16_t *px = (const uint16_t *)src;
    uint32_t ow = s_w / s_scale, oh = s_h / s_scale;
    uint32_t left = (ow - s_decode_w) / 2, right = left + s_decode_w;
    uint64_t sum = 0, r_sum = 0, g_sum = 0, b_sum = 0;
    for (uint32_t y = 0; y < oh; y++) {
        const uint16_t *row = px + (y * s_scale) * s_w;
        uint16_t *out = preview ? preview + (y + 1) * ow - 1 : NULL;
        for (uint32_t x = 0; x < ow; x++) {
            uint16_t p = row[x * s_scale];
            uint32_t r5 = p >> 11, g6 = (p >> 5) & 0x3F, b5 = p & 0x1F;
            uint32_t r = r5 << 3, g = g6 << 2, b = b5 << 3;
            uint8_t gray = (uint8_t)((r * 77 + g * 150 + b * 29) >> 8);
            if (x >= left && x < right) *dst++ = gray;
            sum += gray;
            r_sum += r;
            g_sum += g;
            b_sum += b;
            if (out) *out-- = s_lut_r[r5] | s_lut_g[g6] | s_lut_b[b5];
        }
    }
    uint32_t pixels = ow * oh;
    s_r_mean = r_sum / pixels;
    s_g_mean = g_sum / pixels;
    s_b_mean = b_sum / pixels;
    return sum / pixels;
}

// ISP 자동 노출을 쓰지 않으므로 평균 밝기로 노출, 게인, 디지털 게인 순서로 조절한다
static void auto_exposure(uint32_t mean)
{
    if (mean >= AE_TARGET - AE_MARGIN && mean <= AE_TARGET + AE_MARGIN) return;
    uint32_t lines = s_exposure, gain = s_gain, digital = s_digital;
    if (mean < AE_TARGET) {
        if (lines < OV02C10_EXPOSURE_MAX) lines = lines * 5 / 4 + 1;
        else if (gain < OV02C10_GAIN_MAX) gain = gain * 5 / 4 + 1;
        else digital = digital * 5 / 4;
    } else {
        if (digital > OV02C10_DGAIN_MIN) digital = digital * 4 / 5;
        else if (gain > OV02C10_GAIN_MIN) gain = gain * 4 / 5;
        else lines = lines * 4 / 5;
    }
    if (lines > OV02C10_EXPOSURE_MAX) lines = OV02C10_EXPOSURE_MAX;
    if (lines < OV02C10_EXPOSURE_MIN) lines = OV02C10_EXPOSURE_MIN;
    if (gain > OV02C10_GAIN_MAX) gain = OV02C10_GAIN_MAX;
    if (gain < OV02C10_GAIN_MIN) gain = OV02C10_GAIN_MIN;
    if (digital > OV02C10_DGAIN_MAX) digital = OV02C10_DGAIN_MAX;
    if (digital < OV02C10_DGAIN_MIN) digital = OV02C10_DGAIN_MIN;
    if (lines == s_exposure && gain == s_gain && digital == s_digital) return;
    s_exposure = lines;
    s_gain = gain;
    s_digital = digital;
    ov02c10_set_exposure(s_exposure, s_gain, s_digital);
}

static void scan_task(void *arg)
{
    struct quirc *q = quirc_new();
    if (!q || quirc_resize(q, s_decode_w, s_h / s_scale) < 0) {
        ESP_LOGE(TAG, "quirc 메모리 부족 (PSRAM 활성화 확인)");
        vTaskDelete(NULL);
    }
    struct quirc_code *code = heap_caps_malloc(sizeof(*code), MALLOC_CAP_SPIRAM);
    struct quirc_data *data = heap_caps_malloc(sizeof(*data), MALLOC_CAP_SPIRAM);
    uint32_t frames = 0;
    color_update(1, 1, 1);

    for (;;) {
        struct v4l2_buffer b = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP };
        if (ioctl(s_fd, VIDIOC_DQBUF, &b) != 0) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }

        int w, h;
        uint8_t *gray = quirc_begin(q, &w, &h);
        uint8_t *preview = (s_w / s_scale == UI_PREVIEW_W && s_h / s_scale == UI_PREVIEW_H) ? ui_preview_buffer() : NULL;
        uint32_t mean = downscale(s_bufs[b.index], gray, (uint16_t *)preview);
        ioctl(s_fd, VIDIOC_QBUF, &b);  // 변환 끝났으면 버퍼 즉시 반납
        if (preview) ui_preview_commit();
        if (++frames % AE_EVERY_FRAMES == 0) {
            auto_exposure(mean);
            color_update(s_r_mean, s_g_mean, s_b_mean);
        }
        if (frames == 1 || frames % 30 == 0) {
            ESP_LOGI(TAG, "프레임 %lu, 밝기 %lu, RGB %lu %lu %lu, 노출 %lu, 게인 %lu/%lu",
                     frames, mean, s_r_mean, s_g_mean, s_b_mean, s_exposure, s_gain, s_digital);
        }
        vTaskDelay(1);  // 쉬지 않고 돌면 코어 1의 IDLE 태스크가 굶어 워치독이 울린다
        if (frames % DECODE_EVERY_FRAMES != 0) continue;  // 미리보기가 끊기지 않게 인식은 띄엄띄엄 한다
        quirc_end(q);

        int n = quirc_count(q);
        for (int i = 0; i < n; i++) {
            quirc_extract(q, i, code);
            quirc_decode_error_t err = quirc_decode(code, data);
            if (err == QUIRC_ERROR_DATA_ECC) {  // 좌우 반전(거울 이미지) 대응
                quirc_flip(code);
                err = quirc_decode(code, data);
            }
            if (err != QUIRC_SUCCESS) continue;
            data->payload[data->payload_len < (int)sizeof(data->payload) ? data->payload_len : (int)sizeof(data->payload) - 1] = 0;
            s_cb((const char *)data->payload);
        }
    }
}

esp_err_t camera_qr_start(qr_callback_t cb)
{
    s_cb = cb;
    ESP_RETURN_ON_ERROR(camera_open(), TAG, "카메라 시작 실패");
    xTaskCreatePinnedToCore(scan_task, "qr_scan", 16 * 1024, NULL, 5, NULL, 1);
    return ESP_OK;
}

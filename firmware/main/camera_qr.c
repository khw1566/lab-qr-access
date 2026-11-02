// MIPI-CSI 카메라 → (ISP) RGB565 → 그레이스케일 축소 → quirc 로 QR 디코딩
// esp_video(V4L2 스타일 API) 사용. ESP-IDF 5.5 + espressif/esp_video 1.x 기준.
#include <fcntl.h>
#include <string.h>
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
#include "quirc.h"
#include "sdkconfig.h"
#include "app.h"

static const char *TAG = "cam_qr";

#define BUF_COUNT 2

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
    ESP_LOGI(TAG, "카메라 %lux%lu, 디코딩 %lux%lu", s_w, s_h, s_w / s_scale, s_h / s_scale);

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

// RGB565(LE) → 8bit 그레이, s_scale 배 축소
static void to_gray(const uint8_t *src, uint8_t *dst)
{
    const uint16_t *px = (const uint16_t *)src;
    uint32_t ow = s_w / s_scale, oh = s_h / s_scale;
    for (uint32_t y = 0; y < oh; y++) {
        const uint16_t *row = px + (y * s_scale) * s_w;
        for (uint32_t x = 0; x < ow; x++) {
            uint16_t p = row[x * s_scale];
            uint32_t r = (p >> 11) << 3, g = ((p >> 5) & 0x3F) << 2, b = (p & 0x1F) << 3;
            *dst++ = (uint8_t)((r * 77 + g * 150 + b * 29) >> 8);
        }
    }
}

static void scan_task(void *arg)
{
    struct quirc *q = quirc_new();
    if (!q || quirc_resize(q, s_w / s_scale, s_h / s_scale) < 0) {
        ESP_LOGE(TAG, "quirc 메모리 부족 (PSRAM 활성화 확인)");
        vTaskDelete(NULL);
    }
    struct quirc_code *code = heap_caps_malloc(sizeof(*code), MALLOC_CAP_SPIRAM);
    struct quirc_data *data = heap_caps_malloc(sizeof(*data), MALLOC_CAP_SPIRAM);

    for (;;) {
        struct v4l2_buffer b = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP };
        if (ioctl(s_fd, VIDIOC_DQBUF, &b) != 0) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }

        int w, h;
        uint8_t *gray = quirc_begin(q, &w, &h);
        to_gray(s_bufs[b.index], gray);
        ioctl(s_fd, VIDIOC_QBUF, &b);  // 변환 끝났으면 버퍼 즉시 반납
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

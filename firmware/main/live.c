// 관리자 실시간 보기: 서버가 롱 폴링으로 'live' 명령을 주면 WebSocket(wss)으로 서버 중계(Durable Object)에 붙어
//  - 카메라 화면을 하드웨어 JPEG로 압축해 보내고 (0x01)
//  - 관리자가 '소리 듣기'를 켜면 마이크 소리를 8 kHz PCM으로 보내고 (0x02, 기능 설정에서 허용한 경우만)
//  - 관리자가 말하면 받은 8 kHz PCM(0x03)을 스피커로 재생한다.
// 보드에도 서버에도 영상·음성을 저장하지 않는다. 연결 중에는 화면 위에 표시를 띄우고 시작할 때 알림음을 낸다.
// 보는 관리자가 없으면 서버가 {"t":"stop"}을 보내고, 그래도 LIVE_MAX_MS가 지나면 스스로 끊는다.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "driver/jpeg_encode.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "app.h"

#if CONFIG_LAB_LIVE_ENABLE

static const char *TAG = "live";

#define LIVE_MAX_MS       (11 * 60 * 1000)   // 서버 제한(10분)보다 조금 길게. 서버와 연락이 끊겨도 여기서 끝난다.
#define CONNECT_WAIT_MS   15000
#define FRAME_WAIT_MS     500
#define SEND_TIMEOUT_MS   3000
#define MIC_SAMPLES       512                 // 8 kHz 64 ms
#define DEFAULT_FPS       5
#define WS_BUFFER         8192                // 말하기 덩어리(최대 4 KB)가 한 번에 들어오도록

#define EV_CONNECTED  BIT0
#define EV_CLOSED     BIT1

enum { FRAME_IDLE, FRAME_WANTED, FRAME_FILLED };

static TaskHandle_t s_task, s_mic_task;
static EventGroupHandle_t s_events;
static esp_websocket_client_handle_t s_ws;
static volatile int s_frame_state = FRAME_IDLE;
static uint16_t *s_frame;          // 카메라가 채우는 RGB565 (JPEG 입력 버퍼)
static uint32_t s_w, s_h;          // 16의 배수로 자른 크기
static volatile bool s_stop, s_mic_on, s_mic_allowed;
static volatile int s_fps = DEFAULT_FPS;

// ---- 카메라 태스크(camera_qr.c)에서 부른다 ----
uint16_t *live_frame_claim(uint32_t *w, uint32_t *h)
{
    if (s_frame_state != FRAME_WANTED || !s_frame) return NULL;
    *w = s_w;
    *h = s_h;
    return s_frame;
}

void live_frame_ready(void)
{
    s_frame_state = FRAME_FILLED;
    if (s_task) xTaskNotifyGive(s_task);
}

// ---- WebSocket ----
static void handle_text(const char *data, int len)
{
    char buf[96];
    if (len >= (int)sizeof(buf)) return;
    memcpy(buf, data, len);
    buf[len] = 0;
    if (strstr(buf, "\"t\":\"stop\"")) {
        s_stop = true;
    } else if (strstr(buf, "\"t\":\"mic\"")) {
        bool on = strstr(buf, "\"on\":true") != NULL;
        s_mic_on = on && s_mic_allowed && voice_mic_available();
        ui_live(true, s_mic_on);
        ESP_LOGI(TAG, "소리 듣기 %s", s_mic_on ? "켜짐" : "꺼짐");
    } else if (strstr(buf, "\"t\":\"fps\"")) {
        const char *v = strstr(buf, "\"v\":");
        int fps = v ? atoi(v + 4) : DEFAULT_FPS;
        s_fps = fps < 1 ? 1 : fps > 10 ? 10 : fps;
    }
}

static void ws_event(void *arg, esp_event_base_t base, int32_t id, void *event_data)
{
    esp_websocket_event_data_t *e = event_data;
    switch (id) {
    case WEBSOCKET_EVENT_CONNECTED:
        xEventGroupSetBits(s_events, EV_CONNECTED);
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_CLOSED:
    case WEBSOCKET_EVENT_ERROR:
        xEventGroupSetBits(s_events, EV_CLOSED);
        break;
    case WEBSOCKET_EVENT_DATA:
        // 조각난 메시지는 쓰지 않는다 (서버가 보내는 메시지는 모두 WS_BUFFER보다 작다)
        if (e->payload_offset != 0 || e->data_len != e->payload_len || e->data_len <= 0) break;
        if (e->op_code == 0x1) handle_text(e->data_ptr, e->data_len);
        else if (e->op_code == 0x2 && (uint8_t)e->data_ptr[0] == 0x03) voice_talk_write((const uint8_t *)e->data_ptr + 1, e->data_len - 1);
        else if (e->op_code == 0x8) xEventGroupSetBits(s_events, EV_CLOSED);
        break;
    default:
        break;
    }
}

static bool ws_open(void)
{
    const char *base = CONFIG_LAB_DEVICE_API_URL[0] ? CONFIG_LAB_DEVICE_API_URL : CONFIG_LAB_SERVER_URL;
    bool tls = strncmp(base, "https://", 8) == 0;
    // 카메라·음성을 내보내므로 인증서로 서버를 확인할 수 있는 https 서버에만 붙는다
    if (!tls) { ESP_LOGW(TAG, "https 서버가 아니라 실시간 보기를 하지 않습니다"); return false; }
    const char *host = strstr(base, "://");
    host = host ? host + 3 : base;
    static char uri[192], headers[128];
    snprintf(uri, sizeof(uri), "%s://%s/api/device/live", tls ? "wss" : "ws", host);
    snprintf(headers, sizeof(headers), "Authorization: Bearer %s\r\n", CONFIG_LAB_DEVICE_KEY);
    esp_websocket_client_config_t cfg = {
        .uri = uri,
        .headers = headers,
        .buffer_size = WS_BUFFER,
        .task_stack = 6144,
        .network_timeout_ms = 10000,
        .disable_auto_reconnect = true,
        .ping_interval_sec = 10,
    };
    if (tls) cfg.crt_bundle_attach = esp_crt_bundle_attach;
    xEventGroupClearBits(s_events, EV_CONNECTED | EV_CLOSED);
    s_ws = esp_websocket_client_init(&cfg);
    if (!s_ws) return false;
    esp_websocket_register_events(s_ws, WEBSOCKET_EVENT_ANY, ws_event, NULL);
    if (esp_websocket_client_start(s_ws) != ESP_OK) return false;
    EventBits_t bits = xEventGroupWaitBits(s_events, EV_CONNECTED | EV_CLOSED, pdFALSE, pdFALSE, pdMS_TO_TICKS(CONNECT_WAIT_MS));
    return (bits & EV_CONNECTED) && !(bits & EV_CLOSED);
}

static void ws_close(void)
{
    if (!s_ws) return;
    if (esp_websocket_client_is_connected(s_ws)) esp_websocket_client_close(s_ws, pdMS_TO_TICKS(2000));
    esp_websocket_client_destroy(s_ws);
    s_ws = NULL;
}

// ---- 마이크 ----
static void mic_task(void *arg)
{
    static uint8_t msg[1 + MIC_SAMPLES * 2];
    msg[0] = 0x02;
    int16_t *pcm = heap_caps_malloc(MIC_SAMPLES * 2, MALLOC_CAP_DEFAULT);
    while (pcm && !s_stop) {
        if (!s_mic_on) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
        if (voice_mic_read(pcm, MIC_SAMPLES) != MIC_SAMPLES) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
        // 관리자가 말하는 동안은 스피커 소리가 마이크로 되돌아가지 않게 보내지 않는다
        if (voice_talk_active() || !s_ws || !esp_websocket_client_is_connected(s_ws)) continue;
        memcpy(msg + 1, pcm, MIC_SAMPLES * 2);
        esp_websocket_client_send_bin(s_ws, (const char *)msg, sizeof(msg), pdMS_TO_TICKS(500));
    }
    free(pcm);
    s_mic_task = NULL;
    vTaskDelete(NULL);
}

// ---- 세션 ----
static void live_task(void *arg)
{
    jpeg_encoder_handle_t enc = NULL;
    uint8_t *jpeg = NULL, *msg = NULL;
    size_t in_size = 0, out_size = 0;
    uint32_t cam_w, cam_h;
    camera_frame_size(&cam_w, &cam_h);
    s_w = cam_w & ~15u;
    s_h = cam_h & ~15u;
    s_stop = false;
    s_mic_on = false;
    s_fps = DEFAULT_FPS;

    jpeg_encode_engine_cfg_t ecfg = { .timeout_ms = 100 };
    jpeg_encode_memory_alloc_cfg_t in_cfg = { .buffer_direction = JPEG_ENC_ALLOC_INPUT_BUFFER };
    jpeg_encode_memory_alloc_cfg_t out_cfg = { .buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER };
    size_t want_out = s_w * s_h / 2;    // 품질 50 정도면 넉넉하다
    if (!s_w || !s_h || jpeg_new_encoder_engine(&ecfg, &enc) != ESP_OK ||
        !(s_frame = jpeg_alloc_encoder_mem(s_w * s_h * 2, &in_cfg, &in_size)) ||
        !(jpeg = jpeg_alloc_encoder_mem(want_out, &out_cfg, &out_size)) ||
        !(msg = heap_caps_malloc(out_size + 1, MALLOC_CAP_SPIRAM))) {
        ESP_LOGE(TAG, "실시간 보기 준비 실패 (카메라 %lux%lu, 메모리 부족)", cam_w, cam_h);
        goto done;
    }
    if (!ws_open()) {
        ESP_LOGW(TAG, "서버 중계에 연결하지 못함");
        goto done;
    }
    ESP_LOGI(TAG, "실시간 보기 시작 (%lux%lu, 마이크 %s)", s_w, s_h, s_mic_allowed ? "허용" : "꺼짐");
    ui_live(true, false);
    voice_chime();
    char hello[96];
    int hl = snprintf(hello, sizeof(hello), "{\"t\":\"hello\",\"w\":%lu,\"h\":%lu,\"mic\":%s}", s_w, s_h,
                      voice_mic_available() && s_mic_allowed ? "true" : "false");
    esp_websocket_client_send_text(s_ws, hello, hl, pdMS_TO_TICKS(SEND_TIMEOUT_MS));
    if (voice_mic_available() && s_mic_allowed) xTaskCreate(mic_task, "live_mic", 4096, NULL, 5, &s_mic_task);

    jpeg_encode_cfg_t jcfg = {
        .width = s_w,
        .height = s_h,
        .src_type = JPEG_ENCODE_IN_FORMAT_RGB565,
        .sub_sample = JPEG_DOWN_SAMPLING_YUV420,
        .image_quality = CONFIG_LAB_LIVE_JPEG_QUALITY,
    };
    int64_t started = esp_timer_get_time() / 1000, next = 0;
    int failures = 0;
    msg[0] = 0x01;
    while (!s_stop && !(xEventGroupGetBits(s_events) & EV_CLOSED) && esp_timer_get_time() / 1000 - started < LIVE_MAX_MS) {
        int64_t now = esp_timer_get_time() / 1000;
        if (now < next) { vTaskDelay(pdMS_TO_TICKS(next - now)); continue; }
        next = now + 1000 / s_fps;
        ulTaskNotifyTake(pdTRUE, 0);
        s_frame_state = FRAME_WANTED;
        if (!ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(FRAME_WAIT_MS)) || s_frame_state != FRAME_FILLED) {
            s_frame_state = FRAME_IDLE;
            continue;   // 카메라가 멈췄거나 바쁨
        }
        uint32_t len = 0;
        esp_err_t err = jpeg_encoder_process(enc, &jcfg, (const uint8_t *)s_frame, s_w * s_h * 2, jpeg, out_size, &len);
        s_frame_state = FRAME_IDLE;
        if (err != ESP_OK || !len) { ESP_LOGW(TAG, "JPEG 실패 %s", esp_err_to_name(err)); continue; }
        memcpy(msg + 1, jpeg, len);
        if (esp_websocket_client_send_bin(s_ws, (const char *)msg, len + 1, pdMS_TO_TICKS(SEND_TIMEOUT_MS)) < 0) {
            if (++failures >= 5) { ESP_LOGW(TAG, "전송이 계속 실패해 끝냄"); break; }
        } else {
            failures = 0;
        }
    }
    ESP_LOGI(TAG, "실시간 보기 끝");

done:
    s_stop = true;
    s_frame_state = FRAME_IDLE;
    while (s_mic_task) vTaskDelay(pdMS_TO_TICKS(20));
    ws_close();
    voice_talk_flush();
    ui_live(false, false);
    if (enc) jpeg_del_encoder_engine(enc);
    vTaskDelay(pdMS_TO_TICKS(200));  // 카메라 태스크가 이전 포인터를 다 쓰고 나가도록
    free(s_frame);
    s_frame = NULL;
    free(jpeg);
    free(msg);
    s_task = NULL;
    vTaskDelete(NULL);
}

void live_request(bool mic_allowed)
{
    if (s_task) return;   // 이미 연결 중이면 그대로 둔다 (새 관리자도 같은 중계로 본다)
    if (!s_events) s_events = xEventGroupCreate();
    s_mic_allowed = mic_allowed;
    if (xTaskCreatePinnedToCore(live_task, "live", 8192, NULL, 4, &s_task, 0) != pdPASS) {
        s_task = NULL;
        ESP_LOGE(TAG, "실시간 보기 태스크 메모리 부족");
    }
}

#else   // 실시간 보기를 빼고 빌드한 경우

uint16_t *live_frame_claim(uint32_t *w, uint32_t *h) { return NULL; }
void live_frame_ready(void) {}
void live_request(bool mic_allowed) {}

#endif

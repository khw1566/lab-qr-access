// 실습실 출입 단말: 카메라로 QR 인식 → 서버 검증 → 릴레이로 문 개방
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "app.h"

#ifndef CONFIG_LAB_WEATHER_REFRESH_MIN
#define CONFIG_LAB_WEATHER_REFRESH_MIN 15
#endif

static const char *TAG = "lab_door";

#define SAME_CODE_COOLDOWN_MS 3000  // 같은 QR이 이 시간 동안 안 보여야 다시 인식한다

static char s_last[96];
static int64_t s_last_at;
static QueueHandle_t s_verify_queue;
static volatile bool s_busy;
static portMUX_TYPE s_submit_mux = portMUX_INITIALIZER_UNLOCKED;
typedef struct { char value[96]; bool manual; } verify_request_t;

static void release_busy(void)
{
    portENTER_CRITICAL(&s_submit_mux);
    s_busy = false;
    portEXIT_CRITICAL(&s_submit_mux);
}

static bool submit_request(const char *value, bool manual)
{
    if (!s_verify_queue || strlen(value) >= sizeof(((verify_request_t *)0)->value)) return false;
    portENTER_CRITICAL(&s_submit_mux);
    bool available = !s_busy;
    if (available) s_busy = true;
    portEXIT_CRITICAL(&s_submit_mux);
    if (!available) return false;
    verify_request_t request = { .manual = manual };
    strlcpy(request.value, value, sizeof(request.value));
    ui_busy("서버와 통신 중입니다");
    voice_play(VOICE_CHECKING);
    if (xQueueSend(s_verify_queue, &request, 0) != pdPASS) {
        ui_idle();
        release_busy();
        return false;
    }
    return true;
}

bool access_submit_code(const char *code)
{
    return strlen(code) == 8 && strspn(code, "0123456789") == 8 && submit_request(code, true);
}

// 카메라 태스크에서 호출된다. 서버 통신은 verify_task가 맡아 미리보기가 멈추지 않는다.
static void on_qr(const char *payload)
{
    if (ui_manual_active()) return;
    int64_t now = esp_timer_get_time() / 1000;
    if (strncmp(payload, "LA1.", 4) != 0) {
        ESP_LOGW(TAG, "출입 QR이 아님: %.40s", payload);
        return;
    }
    // 같은 QR은 카메라에서 치웠다가 다시 대야 인식한다. 보일 때마다 시각을 갱신해 계속 대고 있어도 반복되지 않는다.
    bool same = strcmp(payload, s_last) == 0;
    bool repeated = same && now - s_last_at < SAME_CODE_COOLDOWN_MS;
    if (same) s_last_at = now;
    if (s_busy || repeated) return;

    strlcpy(s_last, payload, sizeof(s_last));
    s_last_at = now;
    submit_request(s_last, false);
}

// 결과 화면을 띄웠으면 true를 돌려준다
static bool verify_one(const verify_request_t *request)
{
    verify_result_t r = request->manual ? server_verify_code(request->value) : server_verify(request->value);
    if (!request->manual && r.reachable && strcmp(r.reason, "duplicate_scan") == 0) {
        // 방금 통과한 QR이 다시 찍힌 경우로, 거부가 아니므로 조용히 넘어간다
        ESP_LOGI(TAG, "중복 인식 무시");
        ui_idle();
        return false;
    }
    if (r.ok) {
        ESP_LOGI(TAG, "출입 허용 → 문 열림 %d ms", r.open_ms);
        ui_result(true, "출입이 승인되었습니다");
        voice_play(VOICE_GRANTED);
        door_signal(true);
        door_open(r.open_ms);
    } else {
        ESP_LOGW(TAG, "출입 거부: %s", r.reachable ? r.reason : "서버 연결 실패");
        if (request->manual) ui_code_error(r.reachable ? r.reason : "offline");
        else ui_result(false, r.reachable ? "출입이 거부되었습니다" : "서버에 연결할 수 없습니다");
        voice_play(r.reachable ? VOICE_DENIED : VOICE_OFFLINE);
        door_signal(false);
    }
    return r.ok || !request->manual;
}

static void verify_task(void *arg)
{
    verify_request_t request;
    for (;;) {
        if (xQueueReceive(s_verify_queue, &request, portMAX_DELAY) != pdTRUE) continue;
        if (verify_one(&request)) vTaskDelay(pdMS_TO_TICKS(UI_RESULT_SHOW_MS));
        release_busy();
    }
}

#define WEATHER_RETRY_MS  (60 * 1000)
#define WEATHER_STALE_MS  (3 * 3600 * 1000LL)   // 이보다 오래 못 받으면 낡은 날씨는 숨긴다

static void ping_task(void *arg)
{
    int64_t weather_due = 0, weather_ok_at = 0;
    for (;;) {
        // 연결되면 바로 첫 신호를 보내고, 끊겨 있는 동안은 짧게 다시 확인한다
        if (!wifi_is_connected()) {
            ui_server_status(UI_SERVER_CHECKING);   // Wi-Fi가 없으면 서버 상태를 알 수 없다
            wifi_recover();
            vTaskDelay(pdMS_TO_TICKS(2 * 1000));
            continue;
        }
        bool online = server_ping();
        int64_t now = esp_timer_get_time() / 1000;
        if (now >= weather_due) {
            if (weather_fetch()) {
                weather_ok_at = now;
                weather_due = now + CONFIG_LAB_WEATHER_REFRESH_MIN * 60 * 1000LL;
            } else {
                weather_due = now + WEATHER_RETRY_MS;
                if (weather_ok_at && now - weather_ok_at > WEATHER_STALE_MS) ui_weather_set(-1, true, 0);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(online ? 60 * 1000 : 5 * 1000));
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    door_init();
    s_verify_queue = xQueueCreate(1, sizeof(verify_request_t));
    // 화면이나 카메라가 실패해도 재부팅을 반복하지 않고 나머지 기능은 계속 동작한다
    err = display_start();
    if (err != ESP_OK) ESP_LOGE(TAG, "화면 시작 실패: %s", esp_err_to_name(err));
    err = voice_start();
    if (err != ESP_OK) ESP_LOGE(TAG, "음성 안내 사용 불가: %s", esp_err_to_name(err));
    wifi_start();
    xTaskCreate(ping_task, "ping", 10240, NULL, 3, NULL);
    if (s_verify_queue) xTaskCreate(verify_task, "verify", 10240, NULL, 4, NULL);
    else ESP_LOGE(TAG, "출입 확인 큐 메모리 부족");
    err = camera_qr_start(on_qr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "카메라 시작 실패: %s", esp_err_to_name(err));
        ui_camera_failed();
    }
}

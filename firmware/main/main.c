// 실습실 출입 단말: 카메라로 QR 인식 → 서버 검증 → 릴레이로 문 개방
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "app.h"

static const char *TAG = "lab_door";

#define SAME_CODE_COOLDOWN_MS 3000  // 같은 QR이 이 시간 동안 안 보여야 다시 인식한다

static char s_last[96];
static int64_t s_last_at;
static QueueHandle_t s_verify_queue;
static volatile bool s_busy;

// 카메라 태스크에서 호출된다. 서버 통신은 verify_task가 맡아 미리보기가 멈추지 않는다.
static void on_qr(const char *payload)
{
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
    s_busy = true;
    ui_busy("서버와 통신 중입니다");
    voice_play(VOICE_CHECKING);
    xQueueOverwrite(s_verify_queue, s_last);
}

// 결과 화면을 띄웠으면 true를 돌려준다
static bool verify_one(const char *payload)
{
    verify_result_t r = server_verify(payload);
    if (r.reachable && strcmp(r.reason, "duplicate_scan") == 0) {
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
        ui_result(false, r.reachable ? "출입이 거부되었습니다" : "서버에 연결할 수 없습니다");
        voice_play(r.reachable ? VOICE_DENIED : VOICE_OFFLINE);
        door_signal(false);
    }
    return true;
}

static void verify_task(void *arg)
{
    char payload[sizeof(s_last)];
    for (;;) {
        if (xQueueReceive(s_verify_queue, payload, portMAX_DELAY) != pdTRUE) continue;
        if (verify_one(payload)) vTaskDelay(pdMS_TO_TICKS(UI_RESULT_SHOW_MS));
        s_busy = false;
    }
}

static void ping_task(void *arg)
{
    for (;;) {
        // 연결되면 바로 첫 신호를 보내고, 끊겨 있는 동안은 짧게 다시 확인한다
        if (wifi_is_connected()) {
            vTaskDelay(pdMS_TO_TICKS(server_ping() ? 60 * 1000 : 5 * 1000));
        } else {
            vTaskDelay(pdMS_TO_TICKS(2 * 1000));
        }
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
    // 화면이나 카메라가 실패해도 재부팅을 반복하지 않고 나머지 기능은 계속 동작한다
    err = display_start();
    if (err != ESP_OK) ESP_LOGE(TAG, "화면 시작 실패: %s", esp_err_to_name(err));
    err = voice_start();
    if (err != ESP_OK) ESP_LOGE(TAG, "음성 안내 사용 불가: %s", esp_err_to_name(err));
    wifi_start();
    xTaskCreate(ping_task, "ping", 10240, NULL, 3, NULL);
    s_verify_queue = xQueueCreate(1, sizeof(s_last));
    xTaskCreate(verify_task, "verify", 10240, NULL, 4, NULL);
    err = camera_qr_start(on_qr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "카메라 시작 실패: %s", esp_err_to_name(err));
        ui_camera_failed();
    }
}

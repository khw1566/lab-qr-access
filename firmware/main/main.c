// 실습실 출입 단말: 카메라로 QR 인식 → 서버 검증 → 릴레이로 문 개방
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "app.h"

static const char *TAG = "lab_door";

#define SAME_CODE_COOLDOWN_MS 4000  // 같은 QR을 연속으로 다시 보내지 않는 시간

static char s_last[96];
static int64_t s_last_at;

static void on_qr(const char *payload)
{
    int64_t now = esp_timer_get_time() / 1000;
    if (strncmp(payload, "LA1.", 4) != 0) {
        ESP_LOGW(TAG, "출입 QR이 아님: %.40s", payload);
        return;
    }
    if (strcmp(payload, s_last) == 0 && now - s_last_at < SAME_CODE_COOLDOWN_MS) return;
    strlcpy(s_last, payload, sizeof(s_last));
    s_last_at = now;

    verify_result_t r = server_verify(payload);
    if (r.ok) {
        ESP_LOGI(TAG, "출입 허용 → 문 열림 %d ms", r.open_ms);
        door_signal(true);
        door_open(r.open_ms);
    } else {
        ESP_LOGW(TAG, "출입 거부: %s", r.reachable ? r.reason : "서버 연결 실패");
        door_signal(false);
    }
}

static void ping_task(void *arg)
{
    for (;;) {
        if (wifi_is_connected()) server_ping();
        vTaskDelay(pdMS_TO_TICKS(60 * 1000));
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
    ESP_ERROR_CHECK(wifi_start_and_wait());
    xTaskCreate(ping_task, "ping", 6144, NULL, 3, NULL);
    ESP_ERROR_CHECK(camera_qr_start(on_qr));
}

// 관리자 원격 출입문 열기: 서버에 명령을 기다리는 요청(롱 폴링)을 계속 걸어 두고,
// 관리자가 웹 관리자 화면에서 '원격 열기'를 누르면 릴레이를 켜고 화면과 음성으로 안내한다.
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "app.h"

static const char *TAG = "remote";

static void remote_task(void *arg)
{
    for (;;) {
        if (!wifi_is_connected()) {
            vTaskDelay(pdMS_TO_TICKS(2 * 1000));
            continue;
        }
        remote_command_t cmd;
        int status = server_poll_command(&cmd);
        if (status == 200) {
            if (cmd.open) {
                ESP_LOGI(TAG, "관리자 원격 열기 → 문 열림 %d ms", cmd.open_ms);
                door_open(cmd.open_ms);
                ui_remote_open();
                voice_play(VOICE_GRANTED);
                door_signal(true);
            }
            continue;   // 바로 다음 대기 요청을 건다
        }
        // 404: 원격 열기를 지원하지 않는 서버 버전. 그 밖의 실패는 잠시 뒤 다시 시도한다.
        ESP_LOGW(TAG, "원격 명령 대기 실패 (HTTP %d)", status);
        vTaskDelay(pdMS_TO_TICKS(status == 404 ? 60 * 1000 : 5 * 1000));
    }
}

void remote_start(void)
{
    if (xTaskCreate(remote_task, "remote", 10240, NULL, 4, NULL) != pdPASS) ESP_LOGE(TAG, "원격 열기 태스크 메모리 부족");
}

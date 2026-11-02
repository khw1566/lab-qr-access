// 도어락 릴레이 제어: 2x13 헤더의 GPIO로 1채널 릴레이 모듈의 IN 핀을 직접 구동
// 문 열림 펄스는 esp_timer로 닫아서 QR 스캔 루프를 막지 않는다.
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "app.h"

static esp_timer_handle_t s_close_timer;

#define RELAY_ON  (CONFIG_LAB_RELAY_ACTIVE_HIGH ? 1 : 0)
#define RELAY_OFF (CONFIG_LAB_RELAY_ACTIVE_HIGH ? 0 : 1)

static void relay_set(bool on) { gpio_set_level(CONFIG_LAB_RELAY_GPIO, on ? RELAY_ON : RELAY_OFF); }

static void relay_init(void)
{
    // 부팅 직후 문이 열리지 않도록 출력 레벨을 먼저 잡고 방향 설정
    gpio_set_level(CONFIG_LAB_RELAY_GPIO, RELAY_OFF);
    gpio_config_t io = { .pin_bit_mask = 1ULL << CONFIG_LAB_RELAY_GPIO, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&io);
    relay_set(false);
}

static void close_cb(void *arg) { relay_set(false); }

void door_init(void)
{
    relay_init();
#if CONFIG_LAB_BUZZER_GPIO >= 0
    gpio_config_t bz = { .pin_bit_mask = 1ULL << CONFIG_LAB_BUZZER_GPIO, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&bz);
#endif
    const esp_timer_create_args_t args = { .callback = close_cb, .dispatch_method = ESP_TIMER_TASK, .name = "door_close" };
    ESP_ERROR_CHECK(esp_timer_create(&args, &s_close_timer));
}

void door_open(int ms)
{
    if (ms <= 0) ms = 3000;
    if (ms > CONFIG_LAB_DOOR_MAX_OPEN_MS) ms = CONFIG_LAB_DOOR_MAX_OPEN_MS;
    esp_timer_stop(s_close_timer);
    relay_set(true);
    esp_timer_start_once(s_close_timer, (uint64_t)ms * 1000);
}

void door_signal(bool granted)
{
#if CONFIG_LAB_BUZZER_GPIO >= 0
    int beeps = granted ? 1 : 3, len = granted ? 200 : 80;
    for (int i = 0; i < beeps; i++) {
        gpio_set_level(CONFIG_LAB_BUZZER_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(len));
        gpio_set_level(CONFIG_LAB_BUZZER_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(80));
    }
#endif
    // TODO: 7인치 LCD에 결과 표시 (LVGL, 다음 단계)
}

// 도어락 릴레이 제어: 2x13 헤더의 GPIO로 릴레이 모듈의 IN 핀을 직접 구동
// 릴레이 핀은 최대 3개(기본 GPIO27, 26: UART1 단자의 IO27, IO26 / GPIO33: 2x13 헤더)를 같은 신호로 함께 켜고 끈다. -1인 핀은 쓰지 않는다.
// 문 열림 펄스는 esp_timer로 닫아서 QR 스캔 루프를 막지 않는다.
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "app.h"

static const char *TAG = "door";
static esp_timer_handle_t s_close_timer;
static const int s_relay_pins[] = { CONFIG_LAB_RELAY_GPIO, CONFIG_LAB_RELAY_GPIO2, CONFIG_LAB_RELAY_GPIO3 };
#define RELAY_PIN_COUNT (sizeof(s_relay_pins) / sizeof(s_relay_pins[0]))

#define RELAY_ON  (CONFIG_LAB_RELAY_ACTIVE_HIGH ? 1 : 0)
#define RELAY_OFF (CONFIG_LAB_RELAY_ACTIVE_HIGH ? 0 : 1)

static void relay_set(bool on)
{
    for (size_t i = 0; i < RELAY_PIN_COUNT; i++) {
        if (s_relay_pins[i] >= 0) gpio_set_level(s_relay_pins[i], on ? RELAY_ON : RELAY_OFF);
    }
}

static void relay_init(void)
{
    // 부팅 직후 문이 열리지 않도록 출력 레벨을 먼저 잡고 방향 설정
    uint64_t mask = 0;
    for (size_t i = 0; i < RELAY_PIN_COUNT; i++) {
        if (s_relay_pins[i] < 0) continue;
        gpio_set_level(s_relay_pins[i], RELAY_OFF);
        mask |= 1ULL << s_relay_pins[i];
    }
    if (!mask) return;
    gpio_config_t io = { .pin_bit_mask = mask, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&io);
    relay_set(false);
    ESP_LOGI(TAG, "릴레이 핀 GPIO %d, %d, %d (%s 동작)", CONFIG_LAB_RELAY_GPIO, CONFIG_LAB_RELAY_GPIO2, CONFIG_LAB_RELAY_GPIO3,
             CONFIG_LAB_RELAY_ACTIVE_HIGH ? "High" : "Low");
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
    // 서버의 문 열림 시간이 짧아도 최소 CONFIG_LAB_DOOR_MIN_OPEN_MS(기본 15초)는 신호를 유지한다
    if (ms < CONFIG_LAB_DOOR_MIN_OPEN_MS) ms = CONFIG_LAB_DOOR_MIN_OPEN_MS;
    if (ms > CONFIG_LAB_DOOR_MAX_OPEN_MS) ms = CONFIG_LAB_DOOR_MAX_OPEN_MS;
    ESP_LOGI(TAG, "릴레이 켬 %d ms", ms);
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
}

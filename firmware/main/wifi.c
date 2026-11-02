// Wi-Fi 연결. ESP32-P4는 esp_wifi_remote/esp_hosted를 통해 보드의 ESP32-C6를 쓰지만 API는 동일하다.
#include <string.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "sdkconfig.h"
#include "app.h"

static const char *TAG = "wifi";
static EventGroupHandle_t s_evt;
#define BIT_CONNECTED BIT0

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_evt, BIT_CONNECTED);
        ESP_LOGW(TAG, "연결 끊김, 재시도");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&e->ip_info.ip));
        xEventGroupSetBits(s_evt, BIT_CONNECTED);
    }
}

bool wifi_is_connected(void) { return xEventGroupGetBits(s_evt) & BIT_CONNECTED; }

esp_err_t wifi_start_and_wait(void)
{
    s_evt = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL));
    wifi_config_t wc = { 0 };
    strlcpy((char *)wc.sta.ssid, CONFIG_LAB_WIFI_SSID, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, CONFIG_LAB_WIFI_PASSWORD, sizeof(wc.sta.password));
    // 비밀번호가 비어 있으면 개방형(Open) 네트워크로 접속
    wc.sta.threshold.authmode = (strlen(CONFIG_LAB_WIFI_PASSWORD) == 0) ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    xEventGroupWaitBits(s_evt, BIT_CONNECTED, pdFALSE, pdTRUE, portMAX_DELAY);
    return ESP_OK;
}

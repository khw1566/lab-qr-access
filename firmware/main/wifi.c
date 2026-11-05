// Wi-Fi 연결. ESP32-P4는 esp_wifi_remote/esp_hosted를 통해 보드의 ESP32-C6를 쓰지만 API는 동일하다.
// 설정 화면에서 고른 네트워크는 NVS에 저장하고, 없으면 menuconfig 값을 쓴다.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_netif_sntp.h"
#include <sys/time.h>
#include "esp_system.h"
#include "freertos/task.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "app.h"

static const char *TAG = "wifi";
static EventGroupHandle_t s_evt;
#define BIT_CONNECTED BIT0
#define NVS_SPACE "lab-wifi"

static char s_ssid[33], s_password[65];
static volatile bool s_paused;  // 검색 중에는 자동 재접속을 멈춘다
static esp_timer_handle_t s_dhcp_timer;
static void sntp_start_once(void);
#define DHCP_TIMEOUT_US (8 * 1000 * 1000)   // 정상이면 1~2초 안에 IP가 온다. 못 받으면 Wi-Fi를 다시 켠다

static volatile bool s_dhcp_stuck;
static int s_dhcp_failures;
#define DHCP_FAILURES_BEFORE_REBOOT 3

// esp_timer 문맥에서는 RPC 호출을 하지 않고 표시만 남긴다. 복구는 wifi_recover()가 맡는다.
static void on_dhcp_timeout(void *arg)
{
    s_dhcp_stuck = true;
}

// ping_task가 IP 없는 동안 주기적으로 부른다.
// 같은 접속 안에서 다시 붙어도 IP가 안 오는 경우가 있어(부팅마다 달라짐) Wi-Fi를 껐다 켜고,
// 그래도 안 되면 보드를 다시 시작한다. 재부팅으로는 복구되는 것을 현장에서 확인했다.
void wifi_recover(void)
{
    if (!s_dhcp_stuck || s_paused) return;
    s_dhcp_stuck = false;
    s_dhcp_failures++;
    ESP_LOGW(TAG, "AP 연결 후 8초 동안 IP를 받지 못함 (SSID %s, %d번째)", s_ssid, s_dhcp_failures);
    if (s_dhcp_failures >= DHCP_FAILURES_BEFORE_REBOOT) {
        ESP_LOGE(TAG, "IP를 계속 받지 못해 단말을 다시 시작합니다");
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_restart();
    }
    s_paused = true;
    esp_wifi_stop();
    vTaskDelay(pdMS_TO_TICKS(500));
    s_paused = false;
    esp_wifi_start();   // STA_START 이벤트에서 다시 접속한다
}

static void show_connecting(void)
{
    char text[64];
    snprintf(text, sizeof(text), "Wi-Fi 연결 중: %s", s_ssid);
    ui_status(text);
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "AP 연결됨, IP 기다리는 중");
        esp_timer_stop(s_dhcp_timer);
        esp_timer_start_once(s_dhcp_timer, DHCP_TIMEOUT_US);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_timer_stop(s_dhcp_timer);
        xEventGroupClearBits(s_evt, BIT_CONNECTED);
        if (s_paused) return;
        wifi_event_sta_disconnected_t *e = data;
        ESP_LOGW(TAG, "연결 끊김 (SSID %s, reason %d, RSSI %d), 재시도", s_ssid, e ? e->reason : -1, e ? e->rssi : 0);
        show_connecting();
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        esp_timer_stop(s_dhcp_timer);
        s_dhcp_failures = 0;
        ip_event_got_ip_t *e = data;
        ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&e->ip_info.ip));
        char text[32];
        snprintf(text, sizeof(text), "Wi-Fi: " IPSTR, IP2STR(&e->ip_info.ip));
        ui_status(text);
        xEventGroupSetBits(s_evt, BIT_CONNECTED);
        sntp_start_once();
    }
}

// 시계는 서버 응답과 별개로 NTP로도 맞춘다. 서버에 못 붙어도 상단 시계가 나온다.
static void on_time_sync(struct timeval *tv)
{
    ESP_LOGI(TAG, "NTP 시각 동기화");
    ui_clock_set((int64_t)tv->tv_sec * 1000 + tv->tv_usec / 1000);
}

static void sntp_start_once(void)
{
    static bool started;
    if (started) return;
    started = true;
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    config.sync_cb = on_time_sync;
    if (esp_netif_sntp_init(&config) != ESP_OK) ESP_LOGW(TAG, "NTP 시작 실패");
}

static void credentials_load(void)
{
    strlcpy(s_ssid, CONFIG_LAB_WIFI_SSID, sizeof(s_ssid));
    strlcpy(s_password, CONFIG_LAB_WIFI_PASSWORD, sizeof(s_password));
    nvs_handle_t nvs;
    if (nvs_open(NVS_SPACE, NVS_READONLY, &nvs) != ESP_OK) return;
    size_t ssid_len = sizeof(s_ssid), password_len = sizeof(s_password);
    char ssid[sizeof(s_ssid)], password[sizeof(s_password)];
    if (nvs_get_str(nvs, "ssid", ssid, &ssid_len) == ESP_OK &&
        nvs_get_str(nvs, "pass", password, &password_len) == ESP_OK) {
        strlcpy(s_ssid, ssid, sizeof(s_ssid));
        strlcpy(s_password, password, sizeof(s_password));
    }
    nvs_close(nvs);
}

static void credentials_save(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_SPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "ssid", s_ssid);
        if (err == ESP_OK) err = nvs_set_str(nvs, "pass", s_password);
        if (err == ESP_OK) err = nvs_commit(nvs);
        nvs_close(nvs);
    }
    if (err != ESP_OK) ESP_LOGE(TAG, "Wi-Fi 설정 저장 실패: %s", esp_err_to_name(err));
}

static esp_err_t config_apply(void)
{
    wifi_config_t wc = { 0 };
    strlcpy((char *)wc.sta.ssid, s_ssid, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, s_password, sizeof(wc.sta.password));
    // 비밀번호가 비어 있으면 개방형(Open) 네트워크로 접속
    wc.sta.threshold.authmode = (strlen(s_password) == 0) ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_PSK;
    return esp_wifi_set_config(WIFI_IF_STA, &wc);
}

bool wifi_is_connected(void) { return xEventGroupGetBits(s_evt) & BIT_CONNECTED; }

const char *wifi_current_ssid(void) { return s_ssid; }

void wifi_start(void)
{
    s_evt = xEventGroupCreate();
    const esp_timer_create_args_t timer_args = { .callback = on_dhcp_timeout, .name = "dhcp_wait" };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_dhcp_timer));
    credentials_load();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(config_apply());
    ESP_LOGI(TAG, "접속 시도: SSID %s (%s)", s_ssid, s_password[0] ? "비밀번호 있음" : "개방형");
    show_connecting();
    ESP_ERROR_CHECK(esp_wifi_start());
}

int wifi_scan(wifi_ap_t *out, int max)
{
    wifi_ap_record_t *records = calloc(max, sizeof(*records));
    if (!records) return -1;

    s_paused = true;
    esp_wifi_disconnect();
    wifi_scan_config_t cfg = { 0 };
    uint16_t found = max;
    esp_err_t err = esp_wifi_scan_start(&cfg, true);
    if (err == ESP_OK) err = esp_wifi_scan_get_ap_records(&found, records);
    s_paused = false;
    esp_wifi_connect();

    int count = 0;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "검색 실패: %s", esp_err_to_name(err));
        count = -1;
    } else {
        // 신호 세기 순으로 오므로 같은 이름은 처음 것만 남긴다
        for (int i = 0; i < found; i++) {
            const char *ssid = (const char *)records[i].ssid;
            bool duplicate = ssid[0] == 0;
            for (int j = 0; j < count && !duplicate; j++) duplicate = strcmp(out[j].ssid, ssid) == 0;
            if (duplicate) continue;
            strlcpy(out[count].ssid, ssid, sizeof(out[count].ssid));
            out[count].rssi = records[i].rssi;
            out[count].secured = records[i].authmode != WIFI_AUTH_OPEN;
            count++;
        }
    }
    free(records);
    return count;
}

void wifi_connect_to(const char *ssid, const char *password)
{
    strlcpy(s_ssid, ssid, sizeof(s_ssid));
    strlcpy(s_password, password, sizeof(s_password));
    credentials_save();
    s_paused = true;
    esp_wifi_disconnect();
    if (config_apply() != ESP_OK) ESP_LOGE(TAG, "Wi-Fi 설정 적용 실패");
    s_paused = false;
    show_connecting();
    esp_wifi_connect();
}

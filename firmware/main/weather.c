// 상단 바 날씨: Open-Meteo(무료, API 키 없음)에서 현재 기온과 WMO 날씨 코드를 받아 온다.
// 출입 검증과 별개라서 실패해도 문 제어에는 영향이 없다. 호출은 main.c의 ping_task가 맡는다.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "app.h"

static const char *TAG = "weather";

#if CONFIG_LAB_WEATHER_ENABLE
// "current":{...} 안에서 "key": 뒤의 숫자를 읽는다. current_units에도 같은 키가 있어 범위를 좁힌다.
static bool number_in(const char *object, const char *key, double *out)
{
    char pattern[40];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *end = strchr(object, '}');
    const char *p = strstr(object, pattern);
    if (!p || (end && p > end)) return false;
    p += strlen(pattern);
    char *stop;
    *out = strtod(p, &stop);
    return stop != p;
}

bool weather_fetch(void)
{
    char url[224];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s"
             "&current=temperature_2m,weather_code,is_day&timezone=auto",
             CONFIG_LAB_WEATHER_LAT, CONFIG_LAB_WEATHER_LON);
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return false;
    char resp[1024];
    int status = -1, n = 0;
    if (esp_http_client_open(c, 0) == ESP_OK) {
        esp_http_client_fetch_headers(c);
        status = esp_http_client_get_status_code(c);
        n = esp_http_client_read_response(c, resp, sizeof(resp) - 1);
    }
    esp_http_client_cleanup(c);
    resp[n > 0 ? n : 0] = 0;
    const char *current = status == 200 ? strstr(resp, "\"current\":{") : NULL;
    double temp, code, day = 1;
    if (!current || !number_in(current, "temperature_2m", &temp) || !number_in(current, "weather_code", &code)) {
        ESP_LOGW(TAG, "날씨 받기 실패 (HTTP %d)", status);
        return false;
    }
    number_in(current, "is_day", &day);
    ESP_LOGI(TAG, "날씨 코드 %d, %.1f°C", (int)code, temp);
    ui_weather_set((int)code, day != 0, (float)temp);
    return true;
}
#else
bool weather_fetch(void) { return true; }
#endif

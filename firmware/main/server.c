// Cloudflare Worker와 통신: POST /api/device/verify, /api/device/ping
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "app.h"

static const char *TAG = "server";

static int post_json(const char *path, const char *body, char *out, int out_len)
{
    char url[160];
    snprintf(url, sizeof(url), "%s%s", CONFIG_LAB_SERVER_URL, path);
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 12000,    // 보드의 TLS 연결만 4초 이상 걸려 5초로는 자주 시간 초과된다
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    char auth[96];
    snprintf(auth, sizeof(auth), "Bearer %s", CONFIG_LAB_DEVICE_KEY);
    esp_http_client_set_header(c, "Authorization", auth);
    esp_http_client_set_header(c, "Content-Type", "application/json");

    int status = -1, blen = strlen(body);
    if (esp_http_client_open(c, blen) == ESP_OK && esp_http_client_write(c, body, blen) == blen) {
        esp_http_client_fetch_headers(c);
        status = esp_http_client_get_status_code(c);
        int n = esp_http_client_read_response(c, out, out_len - 1);
        out[n > 0 ? n : 0] = 0;
    }
    esp_http_client_cleanup(c);
    return status;
}

// 간단한 JSON 값 추출 (응답 형식이 고정이라 cJSON 없이 처리)
static bool json_find(const char *json, const char *key, char *val, size_t len)
{
    char pat[40];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = strstr(json, pat);
    if (!p) return false;
    p += strlen(pat);
    if (*p == '"') p++;
    size_t i = 0;
    while (*p && *p != '"' && *p != ',' && *p != '}' && i < len - 1) val[i++] = *p++;
    val[i] = 0;
    return true;
}

static verify_result_t verify_payload(const char *body)
{
    verify_result_t r = { 0 };
    char resp[256], v[32];
    int status = post_json("/api/device/verify", body, resp, sizeof(resp));
    if (status != 200) {
        ESP_LOGE(TAG, "verify HTTP %d", status);
        return r;
    }
    r.reachable = true;
    r.ok = json_find(resp, "ok", v, sizeof(v)) && strcmp(v, "true") == 0;
    r.open_ms = json_find(resp, "open_ms", v, sizeof(v)) ? atoi(v) : 3000;
    if (!r.ok) json_find(resp, "reason", r.reason, sizeof(r.reason));
    return r;
}

verify_result_t server_verify(const char *token)
{
    if (strpbrk(token, "\"\\") || strlen(token) > 80) {
        verify_result_t r = { .reachable = true };
        strlcpy(r.reason, "bad_format", sizeof(r.reason));
        return r;
    }
    char body[128];
    snprintf(body, sizeof(body), "{\"token\":\"%s\"}", token);
    return verify_payload(body);
}

verify_result_t server_verify_code(const char *code)
{
    if (strlen(code) != 8 || strspn(code, "0123456789") != 8) {
        verify_result_t r = { .reachable = true };
        strlcpy(r.reason, "manual_invalid", sizeof(r.reason));
        return r;
    }
    char body[32];
    snprintf(body, sizeof(body), "{\"code\":\"%s\"}", code);
    return verify_payload(body);
}

bool server_ping(void)
{
    char resp[128];
    int s = post_json("/api/device/ping", "{}", resp, sizeof(resp));
    if (s != 200) ESP_LOGW(TAG, "ping 실패 (HTTP %d)", s);
    else ESP_LOGI(TAG, "ping 성공");
    char server_time[24];
    if (s == 200 && json_find(resp, "server_time", server_time, sizeof(server_time))) {
        ui_clock_set(strtoll(server_time, NULL, 10));
    }
    return s == 200;
}

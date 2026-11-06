// 서버와 통신: POST /api/device/verify, /api/device/ping
// 라즈베리파이 서버를 쓰면 CONFIG_LAB_DEVICE_API_URL(학내망 http://<파이IP>:8788)로 직접 붙는다.
// 응답은 X-Lab-Sig(HMAC-SHA256(단말 키, "<nonce>.<본문>")) 서명을 확인한 뒤에만 믿는다.
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/md.h"
#include "sdkconfig.h"
#include "app.h"

static const char *TAG = "server";

#define SIG_HEX_LEN 64

static const char *api_base(void)
{
    return CONFIG_LAB_DEVICE_API_URL[0] ? CONFIG_LAB_DEVICE_API_URL : CONFIG_LAB_SERVER_URL;
}

// 응답 헤더 중 X-Lab-Sig 만 받아 둔다 (esp_http_client_get_header는 요청 헤더만 읽으므로 이벤트로 받음)
static esp_err_t http_event(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_HEADER && evt->user_data && strcasecmp(evt->header_key, "x-lab-sig") == 0) {
        strlcpy((char *)evt->user_data, evt->header_value, SIG_HEX_LEN + 1);
    }
    return ESP_OK;
}

static void to_hex(const uint8_t *in, size_t n, char *out)
{
    static const char d[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[i * 2] = d[in[i] >> 4];
        out[i * 2 + 1] = d[in[i] & 15];
    }
    out[n * 2] = 0;
}

// 상수 시간 비교
static bool sig_ok(const char *nonce, const char *body, const char *got)
{
    if (strlen(got) != SIG_HEX_LEN) return false;
    uint8_t mac[32];
    char expect[SIG_HEX_LEN + 1];
    const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    bool ok = mbedtls_md_setup(&ctx, md, 1) == 0 &&
              mbedtls_md_hmac_starts(&ctx, (const unsigned char *)CONFIG_LAB_DEVICE_KEY, strlen(CONFIG_LAB_DEVICE_KEY)) == 0 &&
              mbedtls_md_hmac_update(&ctx, (const unsigned char *)nonce, strlen(nonce)) == 0 &&
              mbedtls_md_hmac_update(&ctx, (const unsigned char *)".", 1) == 0 &&
              mbedtls_md_hmac_update(&ctx, (const unsigned char *)body, strlen(body)) == 0 &&
              mbedtls_md_hmac_finish(&ctx, mac) == 0;
    mbedtls_md_free(&ctx);
    if (!ok) return false;
    to_hex(mac, sizeof(mac), expect);
    uint8_t diff = 0;
    for (int i = 0; i < SIG_HEX_LEN; i++) diff |= (uint8_t)(expect[i] ^ got[i]);
    return diff == 0;
}

// 상단 바 서버 표시등. 한 번 실패로 깜빡이지 않도록 연속 2번 실패해야 꺼짐으로 바꾼다.
#define OFFLINE_AFTER_FAILURES 2
static int s_failures;

static void note_reachability(int status)
{
    if (status == 200) {
        s_failures = 0;
        ui_server_status(UI_SERVER_ONLINE);
    } else if (++s_failures >= OFFLINE_AFTER_FAILURES) {
        ui_server_status(UI_SERVER_OFFLINE);
    }
}

// 반환: HTTP 상태 코드, 연결 실패는 -1, 응답 서명이 틀리면 -2 (둘 다 '서버 연결 실패'로 처리)
// track_failures가 false면 성공만 표시등에 반영한다 (원격 명령 대기는 실패 판정을 ping에 맡김)
static int post_json_ex(const char *path, const char *body, char *out, int out_len, int timeout_ms, bool track_failures)
{
    char url[192];
    snprintf(url, sizeof(url), "%s%s", api_base(), path);
    char sig[SIG_HEX_LEN + 1] = "";
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = timeout_ms,
        .event_handler = http_event,
        .user_data = sig,
    };
    if (strncmp(url, "https://", 8) == 0) cfg.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    char auth[96];
    snprintf(auth, sizeof(auth), "Bearer %s", CONFIG_LAB_DEVICE_KEY);
    esp_http_client_set_header(c, "Authorization", auth);
    esp_http_client_set_header(c, "Content-Type", "application/json");

    uint8_t rnd[16];
    char nonce[sizeof(rnd) * 2 + 1];
    esp_fill_random(rnd, sizeof(rnd));
    to_hex(rnd, sizeof(rnd), nonce);
    esp_http_client_set_header(c, "X-Lab-Nonce", nonce);

    int status = -1, blen = strlen(body);
    out[0] = 0;
    if (esp_http_client_open(c, blen) == ESP_OK && esp_http_client_write(c, body, blen) == blen) {
        esp_http_client_fetch_headers(c);
        status = esp_http_client_get_status_code(c);
        int n = esp_http_client_read_response(c, out, out_len - 1);
        out[n > 0 ? n : 0] = 0;
    }
    esp_http_client_cleanup(c);
#if CONFIG_LAB_VERIFY_RESPONSE_SIG
    if (status == 200 && !sig_ok(nonce, out, sig)) {
        ESP_LOGE(TAG, "%s 응답 서명이 맞지 않아 무시합니다 (서버 버전 또는 단말 키 확인)", path);
        status = -2;
    }
#endif
    if (track_failures || status == 200) note_reachability(status);
    return status;
}

static int post_json(const char *path, const char *body, char *out, int out_len)
{
    // 인터넷 경유 TLS 연결은 4초 이상 걸려 넉넉히 둔다 (내부망 http는 훨씬 빠름)
    return post_json_ex(path, body, out, out_len, 12000, true);
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
    char resp[384], v[32];   // 서명 확인에는 본문 전체가 필요하다
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
    char resp[256];   // 서명 확인에는 본문 전체가 필요하다 (단말 이름이 한글이면 길어짐)
    int s = post_json("/api/device/ping", "{}", resp, sizeof(resp));
    if (s != 200) ESP_LOGW(TAG, "ping 실패 (HTTP %d)", s);
    else ESP_LOGI(TAG, "ping 성공");
    char server_time[24];
    if (s == 200 && json_find(resp, "server_time", server_time, sizeof(server_time))) {
        ui_clock_set(strtoll(server_time, NULL, 10));
    }
    return s == 200;
}

// 관리자 원격 열기 명령 대기 (롱 폴링). 서버는 명령이 생기면 바로, 없으면 REMOTE_WAIT_MS 뒤에 응답한다.
// 응답도 nonce 서명을 확인하므로 개방형 Wi-Fi에서 가짜 '열어라' 응답으로는 문이 열리지 않는다.
#define REMOTE_WAIT_MS 20000
int server_poll_command(remote_command_t *cmd)
{
    char resp[192], v[16];
    memset(cmd, 0, sizeof(*cmd));
    int s = post_json_ex("/api/device/commands", "{\"wait_ms\":20000}", resp, sizeof(resp), REMOTE_WAIT_MS + 12000, false);
    if (s != 200) return s;
    bool has = json_find(resp, "command", v, sizeof(v));
    cmd->open = has && strcmp(v, "open") == 0;
    cmd->live = has && strcmp(v, "live") == 0;
    cmd->live_mic = cmd->live && json_find(resp, "mic", v, sizeof(v)) && strcmp(v, "true") == 0;
#if !CONFIG_LAB_VERIFY_RESPONSE_SIG
    // 서명 확인을 끄면 응답을 지켜 주는 것은 HTTPS 인증서 확인뿐이다. http 주소에서는 원격 열기를 받지 않는다.
    if (cmd->open && strncmp(api_base(), "https://", 8) != 0) {
        ESP_LOGE(TAG, "서명 확인이 꺼진 상태의 http 서버라 원격 열기를 무시합니다");
        cmd->open = false;
    }
#endif
    cmd->open_ms = json_find(resp, "open_ms", v, sizeof(v)) ? atoi(v) : 3000;
    return s;
}

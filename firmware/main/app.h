#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

// board.c
i2c_master_bus_handle_t board_i2c_bus(void);

// voice.c (순서는 tools/gen_voice.py의 LINES와 같다)
typedef enum { VOICE_SCAN, VOICE_CHECKING, VOICE_GRANTED, VOICE_DENIED, VOICE_OFFLINE } voice_clip_t;
esp_err_t voice_start(void);
void voice_play(voice_clip_t clip);

// display.c
esp_err_t display_start(void);
bool display_lock(uint32_t timeout_ms);
void display_unlock(void);

// ui.c
#define UI_PREVIEW_W 960
#define UI_PREVIEW_H 540
void ui_build(void);
void ui_status(const char *text);
void ui_clock_set(int64_t unix_ms);
#define UI_RESULT_SHOW_MS 3000
void ui_busy(const char *text);
void ui_idle(void);
void ui_result(bool granted, const char *text);
void ui_camera_failed(void);
uint8_t *ui_preview_buffer(void);   // QR 출입 화면이 아니면 NULL
void ui_preview_commit(void);
void ui_settings_closed(void);
bool ui_manual_active(void);
void ui_code_error(const char *reason);
bool access_submit_code(const char *code);

// ui_settings.c. LVGL 9의 객체 태그를 전방 선언해 다른 모듈의 LVGL 의존을 피한다.
struct lv_obj_t;
void ui_settings_build(struct lv_obj_t *screen);
void ui_settings_open(void);
void ui_settings_close(void);

// wifi.c
typedef struct {
    char ssid[33];
    int8_t rssi;
    bool secured;
} wifi_ap_t;
void wifi_start(void);
bool wifi_is_connected(void);
const char *wifi_current_ssid(void);
int wifi_scan(wifi_ap_t *out, int max);     // 찾은 개수, 실패하면 -1
void wifi_connect_to(const char *ssid, const char *password);

// door.c
void door_init(void);
void door_open(int ms);
void door_signal(bool granted);

// server.c
typedef struct {
    bool ok;          // 서버가 출입 허용
    bool reachable;   // 서버 응답 여부
    int open_ms;
    char reason[32];
} verify_result_t;
verify_result_t server_verify(const char *token);
verify_result_t server_verify_code(const char *code);
bool server_ping(void);

// camera_qr.c
typedef void (*qr_callback_t)(const char *payload);
esp_err_t camera_qr_start(qr_callback_t cb);

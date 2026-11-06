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
void voice_chime(void);                                  // 실시간 연결 알림음
bool voice_mic_available(void);                          // 마이크 입력 핀이 설정되어 있음
int voice_mic_read(int16_t *out, int n);                 // 8 kHz mono n개 (막힘), 실패 -1
void voice_talk_write(const uint8_t *pcm8k, size_t bytes);  // 관리자 말하기 8 kHz s16le 재생
void voice_talk_flush(void);
bool voice_talk_active(void);                            // 방금까지 말하기를 재생했음 (마이크 울림 방지)

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
typedef enum { UI_SERVER_CHECKING, UI_SERVER_ONLINE, UI_SERVER_OFFLINE } ui_server_t;
void ui_server_status(ui_server_t state);     // 상단 바의 서버 표시등
void ui_weather_set(int wmo_code, bool is_day, float temp_c);   // wmo_code < 0 이면 날씨를 숨긴다
#define UI_RESULT_SHOW_MS 3000
void ui_busy(const char *text);
void ui_idle(void);
void ui_result(bool granted, const char *text);
void ui_remote_open(void);   // 관리자 원격 열림 안내
void ui_live(bool on, bool mic);   // 관리자 실시간 보기 연결 중 표시
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
void wifi_recover(void);    // IP를 못 받고 멈춘 경우 복구 (ping_task에서 호출)
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
typedef struct {
    bool open;      // 관리자가 원격으로 문 열기를 요청함
    int open_ms;
    bool live;      // 관리자가 실시간 보기를 열었음
    bool live_mic;  // 기능 설정에서 소리 듣기를 허용함
} remote_command_t;
int server_poll_command(remote_command_t *cmd);   // HTTP 상태 코드 (명령이 없어도 200)

// remote.c
void remote_start(void);

// weather.c
bool weather_fetch(void);   // 받아서 화면에 반영했으면 true

// camera_qr.c
typedef void (*qr_callback_t)(const char *payload);
esp_err_t camera_qr_start(qr_callback_t cb);
void camera_frame_size(uint32_t *w, uint32_t *h);   // 축소한 화면 크기 (시작 전이면 0)

// live.c (관리자 실시간 보기)
void live_request(bool mic_allowed);
uint16_t *live_frame_claim(uint32_t *w, uint32_t *h);   // 카메라 태스크: 실시간 보기가 화면을 원하면 채울 버퍼
void live_frame_ready(void);

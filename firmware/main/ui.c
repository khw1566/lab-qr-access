// C안: 파란 상단 바와 큰 출입 안내. QR 검증과 문 제어는 main.c가 맡는다.
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "app.h"
#include "ui_theme.h"
#include "ui_art.h"

static const char *TAG = "ui";
#define BODY_H         (LCD_V_RES - UI_BAR_H)
#define FRAME_SIZE     312
#define CORNER_LEN     48
#define CORNER_WIDTH   5
#define GUIDE_QR_SIZE  288
#define LOCK_WAIT_MS   200
#define IDLE_HOME_MS   60000
#define KST_OFFSET_S   (9 * 3600)

typedef enum { TAB_HOME, TAB_SCAN, TAB_GUIDE, TAB_CODE } tab_t;
static lv_obj_t *s_status, *s_home, *s_scan, *s_guide, *s_hint;
static lv_obj_t *s_code, *s_code_input, *s_code_error;
static volatile bool s_manual_visible;
static lv_obj_t *s_clock, *s_date, *s_weekday;
static lv_obj_t *s_weather, *s_weather_icon, *s_weather_temp, *s_weather_desc;
static lv_obj_t *s_server_led;
static ui_server_t s_server_state;
static lv_obj_t *s_preview, *s_result, *s_result_label, *s_result_hint, *s_spinner;
static lv_obj_t *s_result_mark, *s_result_rail;
static lv_timer_t *s_result_timer;
static lv_image_dsc_t s_frames[2];
static uint8_t *s_buffers[2];
static int s_shown;
static volatile bool s_scan_visible, s_preview_paused, s_overlay_shown;
static tab_t s_tab = TAB_HOME;
static int64_t s_clock_offset_ms;

static void set_hidden(lv_obj_t *obj, bool hidden)
{
    if (hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static void show_tab(tab_t tab)
{
    if (s_tab == TAB_CODE && tab != TAB_CODE && s_code_input) {
        lv_textarea_set_text(s_code_input, "");
        lv_label_set_text(s_code_error, "");
    }
    s_tab = tab;
    s_manual_visible = tab == TAB_CODE;
    s_scan_visible = tab == TAB_SCAN;
    set_hidden(s_home, tab != TAB_HOME);
    set_hidden(s_scan, tab != TAB_SCAN);
    set_hidden(s_guide, tab != TAB_GUIDE);
    set_hidden(s_code, tab != TAB_CODE);
    lv_obj_update_layout(lv_screen_active());
    ESP_LOGI(TAG, "Icon geometry: %u visible images at native size", ui_icons_validate(lv_screen_active()));
}

static void on_tab(lv_event_t *e)
{
    tab_t tab = (tab_t)(intptr_t)lv_event_get_user_data(e);
    if (tab == TAB_SCAN && s_tab != TAB_SCAN) voice_play(VOICE_SCAN);
    show_tab(tab);
}

static void on_settings(lv_event_t *e)
{
    s_preview_paused = true;
    ui_settings_open();
}

void ui_settings_closed(void) { s_preview_paused = false; }
bool ui_manual_active(void) { return s_manual_visible; }

static lv_obj_t *page_create(lv_obj_t *screen)
{
    return ui_panel(screen, 0, UI_BAR_H, LCD_H_RES, BODY_H);
}

static void footer(lv_obj_t *page, bool home)
{
    ui_icon_button(page, home ? UI_ICON_INFO : UI_ICON_HOME, home ? "안내" : "홈",
                   36, 430, 128, 64, false, on_tab, home ? TAB_GUIDE : TAB_HOME);
    ui_icon_button(page, UI_ICON_SETTINGS, "설정", 860, 430, 128, 64, false, on_settings, 0);
}

// 상단 바 오른쪽: [날씨] | [시:분][날짜/요일] | [서버 표시등/Wi-Fi]
// 모든 글자는 흰색이다. 큰 시계 숫자의 윗선(y≈19)과 바닥선(y≈73)에 맞춰
// 오른쪽 세 묶음의 윗줄(28px)과 아랫줄(24px)을 같은 높이로 둔다.
#define BAR_ROW1_Y     18
#define BAR_ROW2_Y     50
#define BAR_CLOCK_Y    16
#define WEATHER_X      384
#define CLOCK_X        556
#define DATE_X         752
#define STATUS_X       916
#define LED_SIZE       14

static lv_obj_t *bar_divider(lv_obj_t *screen, int x)
{
    lv_obj_t *line = ui_panel(screen, x, 22, 1, UI_BAR_H - 44);
    lv_obj_set_style_bg_color(line, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_40, 0);
    return line;
}

static void bar_build(lv_obj_t *screen)
{
    ui_header(screen, "");
    ui_text(screen, "실습실 출입단말", &lab_font_40, 82, 22, 280, lv_color_white());

    // 날씨는 처음 받기 전까지 숨긴다
    s_weather = ui_panel(screen, WEATHER_X, 0, 152, UI_BAR_H);
    s_weather_icon = ui_icon(s_weather, UI_ICON_CLOUD, 0, 20, 52, lv_color_white());
    s_weather_temp = ui_text(s_weather, "", &lab_font_28, 58, BAR_ROW1_Y, 94, lv_color_white());
    s_weather_desc = ui_text(s_weather, "", &lab_font_24, 58, BAR_ROW2_Y, 94, lv_color_white());
    lv_obj_set_style_text_opa(s_weather_desc, LV_OPA_80, 0);
    lv_obj_add_flag(s_weather, LV_OBJ_FLAG_HIDDEN);
    bar_divider(screen, 544);

    s_clock = ui_text(screen, "--:--", &lab_font_64, CLOCK_X, BAR_CLOCK_Y, 184, lv_color_white());
    lv_obj_set_style_text_align(s_clock, LV_TEXT_ALIGN_RIGHT, 0);
    s_date = ui_text(screen, "", &lab_font_28, DATE_X, BAR_ROW1_Y, 136, lv_color_white());
    s_weekday = ui_text(screen, "", &lab_font_24, DATE_X, BAR_ROW2_Y, 136, lv_color_white());
    lv_obj_set_style_text_opa(s_weekday, LV_OPA_80, 0);
    bar_divider(screen, 900);

    // 서버 표시등: 초록 작동 중, 빨강 응답 없음, 회색 확인 중(Wi-Fi 없음 포함)
    s_server_led = ui_panel(screen, STATUS_X + 3, BAR_ROW1_Y + 8, LED_SIZE, LED_SIZE);
    lv_obj_set_style_radius(s_server_led, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_server_led, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_server_led, lv_color_white(), 0);
    lv_obj_set_style_border_width(s_server_led, 2, 0);
    lv_obj_set_style_shadow_spread(s_server_led, 2, 0);
    lv_obj_set_style_shadow_opa(s_server_led, LV_OPA_70, 0);
    ui_text(screen, "서버", &lab_font_24, STATUS_X + 28, BAR_ROW1_Y + 2, 64, lv_color_white());
    s_status = ui_icon(screen, UI_ICON_WIFI_OFF, STATUS_X, BAR_ROW2_Y + 3, 20, lv_color_white());
    ui_text(screen, "Wi-Fi", &lab_font_24, STATUS_X + 28, BAR_ROW2_Y, 64, lv_color_white());
}

static void server_led_paint(ui_server_t state)
{
    lv_color_t color = state == UI_SERVER_ONLINE ? UI_LED_ON :
                       state == UI_SERVER_OFFLINE ? UI_LED_OFF : UI_GRAY;
    lv_obj_set_style_bg_color(s_server_led, color, 0);
    lv_obj_set_style_shadow_color(s_server_led, color, 0);
    lv_obj_set_style_shadow_width(s_server_led, state == UI_SERVER_CHECKING ? 0 : 14, 0);
}

static void home_build(lv_obj_t *screen)
{
    s_home = page_create(screen);
    ui_art_create(s_home, 684, 76, 332);
    ui_text(s_home, "모바일 QR출입증을\n준비해주세요", &lab_font_64, 36, 40, 640, lv_color_black());

    lv_obj_t *entry = ui_button(s_home, "", 36, 226, 640, 148, true, on_tab, TAB_SCAN);
    lv_obj_set_style_radius(entry, 0, 0);
    lv_obj_set_style_pad_all(entry, 0, 0);
    ui_icon(entry, UI_ICON_FINGERPRINT, 32, 34, 80, lv_color_white());
    lv_obj_t *entry_label = ui_text(entry, "QR 출입", &lab_font_64, 146, 0, 464, lv_color_white());
    lv_obj_align(entry_label, LV_ALIGN_LEFT_MID, 146, 0);

    lv_obj_t *guide = ui_button(s_home, "", 36, 396, 480, 88, false, on_tab, TAB_GUIDE);
    lv_obj_set_style_radius(guide, 0, 0);
    lv_obj_set_style_pad_all(guide, 0, 0);
    lv_obj_set_style_bg_color(guide, UI_GRAY, 0);
    lv_obj_set_style_bg_color(guide, UI_MUTED, LV_STATE_PRESSED);
    ui_icon(guide, UI_ICON_INFO, 20, 16, 56, lv_color_white());
    lv_obj_t *guide_label = ui_text(guide, "처음 이용하시나요?", &lab_font_40, 96, 0, 364, lv_color_white());
    lv_obj_align(guide_label, LV_ALIGN_LEFT_MID, 96, 0);

    lv_obj_t *code = ui_button(s_home, "", 544, 396, 340, 88, false, on_tab, TAB_CODE);
    lv_obj_set_style_pad_all(code, 0, 0);
    ui_icon(code, UI_ICON_KEYBOARD, 24, 22, 44, UI_BLUE);
    lv_obj_t *code_label = ui_text(code, "코드 입력", &lab_font_28, 84, 0, 236, UI_INK);
    lv_obj_align(code_label, LV_ALIGN_LEFT_MID, 84, 0);

    lv_obj_t *settings = ui_icon_button(s_home, UI_ICON_SETTINGS, "", 928, 414, 80, 80, false, on_settings, 0);
    lv_obj_t *settings_icon = lv_obj_get_child(settings, 0);
    ui_icon_resize(settings_icon, 64);
    lv_obj_center(settings_icon);
    ui_icon_color(settings_icon, lv_color_black());
}

static void corner(lv_obj_t *frame, lv_align_t align)
{
    lv_obj_t *horizontal = ui_panel(frame, 0, 0, CORNER_LEN, CORNER_WIDTH);
    lv_obj_t *vertical = ui_panel(frame, 0, 0, CORNER_WIDTH, CORNER_LEN);
    lv_obj_t *bars[] = {horizontal, vertical};
    for (int i = 0; i < 2; i++) {
        lv_obj_set_style_bg_color(bars[i], lv_color_white(), 0);
        lv_obj_set_style_bg_opa(bars[i], LV_OPA_COVER, 0);
        lv_obj_align(bars[i], align, 0, 0);
    }
}

static void scan_build(lv_obj_t *screen)
{
    s_scan = page_create(screen);
    ui_icon(s_scan, UI_ICON_PHONE, 48, 76, 128, UI_BLUE);
    ui_text(s_scan, "QR을 맞춰 주세요", &lab_font_40, 48, 264, 520, UI_INK);
    ui_icon_button(s_scan, UI_ICON_KEYBOARD, "코드 입력", 48, 342, 260, 64, false, on_tab, TAB_CODE);
    lv_obj_t *tile = ui_panel(s_scan, 616, 36, 360, 360);
    lv_obj_set_style_bg_color(tile, UI_BLUE, 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(tile, 8, 0);
    // 원래 960×540 버퍼를 세로 312px로 줄여 중앙을 잘라 표시한다.
    // 카메라 입력 해상도와 QR 디코더는 그대로 유지한다.
    lv_obj_t *viewport = ui_panel(tile, 24, 24, FRAME_SIZE, FRAME_SIZE);
    lv_obj_set_style_bg_color(viewport, UI_INK, 0);
    lv_obj_set_style_bg_opa(viewport, LV_OPA_COVER, 0);
    s_preview = lv_image_create(viewport);
    if (s_buffers[0] && s_buffers[1]) lv_image_set_src(s_preview, &s_frames[0]);
    lv_image_set_scale(s_preview, (FRAME_SIZE * 256 + UI_PREVIEW_H - 1) / UI_PREVIEW_H);
    lv_obj_center(s_preview);
    lv_obj_t *frame = ui_panel(viewport, 0, 0, FRAME_SIZE, FRAME_SIZE);
    corner(frame, LV_ALIGN_TOP_LEFT);
    corner(frame, LV_ALIGN_TOP_RIGHT);
    corner(frame, LV_ALIGN_BOTTOM_LEFT);
    corner(frame, LV_ALIGN_BOTTOM_RIGHT);
    s_hint = ui_text(s_scan, "", &lab_font_24, 600, 398, 392, UI_RED);
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);
    footer(s_scan, false);
}

static void guide_build(lv_obj_t *screen)
{
    s_guide = page_create(screen);
    lv_obj_t *qr = lv_qrcode_create(s_guide);
    lv_qrcode_set_size(qr, GUIDE_QR_SIZE);
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());
    lv_qrcode_update(qr, CONFIG_LAB_SERVER_URL, strlen(CONFIG_LAB_SERVER_URL));
    lv_obj_set_pos(qr, 60, 48);
    const char *url_text = CONFIG_LAB_SERVER_URL;
    if (strncmp(url_text, "https://", 8) == 0) url_text += 8;
    else if (strncmp(url_text, "http://", 7) == 0) url_text += 7;
    lv_obj_t *url = ui_text(s_guide, url_text, &lv_font_montserrat_24, 36, 356, 340, UI_MUTED);
    lv_obj_set_style_text_align(url, LV_TEXT_ALIGN_CENTER, 0);
    ui_text(s_guide, "이용안내", &lab_font_40, 412, 48, 570, UI_INK);
    lv_obj_t *steps = ui_text(s_guide,
        "1. 왼쪽 QR을 휴대폰으로 스캔합니다.\n"
        "2. 가입 후 실습실 이용을 신청합니다.\n"
        "3. 승인된 출입증의 QR을 엽니다.\n"
        "4. QR을 보여 주거나 입력 코드로 출입합니다.",
        &lab_font_24, 412, 132, 570, UI_INK);
    lv_obj_set_style_text_line_space(steps, 22, 0);
    lv_obj_t *home = ui_icon_button(s_guide, UI_ICON_HOME, "", 36, 430, 80, 64, false, on_tab, TAB_HOME);
    lv_obj_set_style_bg_opa(home, LV_OPA_COVER, 0);
    ui_icon_resize(lv_obj_get_child(home, 0), 44);
    lv_obj_center(lv_obj_get_child(home, 0));
    lv_obj_t *scan = ui_icon_button(s_guide, UI_ICON_QR, "", 808, 430, 80, 64, true, on_tab, TAB_SCAN);
    lv_obj_t *settings = ui_icon_button(s_guide, UI_ICON_SETTINGS, "", 908, 430, 80, 64, false, on_settings, 0);
    lv_obj_set_style_bg_opa(settings, LV_OPA_COVER, 0);
    ui_icon_resize(lv_obj_get_child(settings, 0), 44);
    lv_obj_center(lv_obj_get_child(settings, 0));
    (void)scan;
}

static void on_code_key(lv_event_t *event)
{
    lv_obj_t *matrix = lv_event_get_target(event);
    const char *key = lv_buttonmatrix_get_button_text(matrix, lv_buttonmatrix_get_selected_button(matrix));
    if (!key) return;
    if (strcmp(key, "지움") == 0) {
        lv_textarea_delete_char(s_code_input);
        lv_label_set_text(s_code_error, "");
    } else if (strcmp(key, "확인") == 0) {
        const char *value = lv_textarea_get_text(s_code_input);
        if (strlen(value) != 8) { lv_label_set_text(s_code_error, "숫자 8자리를 입력하세요"); return; }
        char code[9];
        strlcpy(code, value, sizeof(code));
        lv_textarea_set_text(s_code_input, "");
        lv_label_set_text(s_code_error, "");
        if (!access_submit_code(code)) lv_label_set_text(s_code_error, "잠시 후 다시 입력하세요");
    } else {
        lv_label_set_text(s_code_error, "");
        lv_textarea_add_text(s_code_input, key);
    }
}

static void code_build(lv_obj_t *screen)
{
    static const char *const keys[] = {"1", "2", "3", "\n", "4", "5", "6", "\n",
                                     "7", "8", "9", "\n", "지움", "0", "확인", ""};
    s_code = page_create(screen);
    ui_text(s_code, "코드 직접 입력", &lab_font_40, 40, 40, 540, UI_INK);
    ui_text(s_code, "휴대폰 출입증의 숫자 8자리를 입력하세요", &lab_font_24, 40, 112, 540, UI_MUTED);
    s_code_input = lv_textarea_create(s_code);
    lv_textarea_set_one_line(s_code_input, true);
    lv_textarea_set_accepted_chars(s_code_input, "0123456789");
    lv_textarea_set_max_length(s_code_input, 8);
    lv_textarea_set_cursor_click_pos(s_code_input, false);
    lv_obj_set_pos(s_code_input, 40, 176);
    lv_obj_set_size(s_code_input, 512, 88);
    lv_obj_set_style_text_font(s_code_input, &lab_font_48, 0);
    lv_obj_set_style_text_color(s_code_input, UI_INK, 0);
    lv_obj_set_style_text_align(s_code_input, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_letter_space(s_code_input, 6, 0);
    lv_obj_set_style_border_color(s_code_input, UI_LINE, 0);
    lv_obj_set_style_bg_color(s_code_input, lv_color_white(), 0);
    lv_obj_set_style_radius(s_code_input, 8, 0);
    s_code_error = ui_text(s_code, "", &lab_font_24, 40, 296, 540, UI_RED);
    lv_obj_t *matrix = lv_buttonmatrix_create(s_code);
    lv_buttonmatrix_set_map(matrix, keys);
    lv_obj_set_pos(matrix, 616, 24);
    lv_obj_set_size(matrix, 360, 392);
    lv_obj_set_style_bg_color(matrix, lv_color_white(), 0);
    lv_obj_set_style_border_width(matrix, 0, 0);
    lv_obj_set_style_pad_all(matrix, 8, 0);
    lv_obj_set_style_pad_row(matrix, 10, 0);
    lv_obj_set_style_pad_column(matrix, 10, 0);
    lv_obj_set_style_bg_color(matrix, UI_SOFT, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(matrix, UI_LINE, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(matrix, UI_INK, LV_PART_ITEMS);
    lv_obj_set_style_text_font(matrix, &lab_font_40, LV_PART_ITEMS);
    lv_obj_set_style_border_width(matrix, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(matrix, 8, LV_PART_ITEMS);
    lv_obj_add_event_cb(matrix, on_code_key, LV_EVENT_VALUE_CHANGED, NULL);
    footer(s_code, false);
}

static void hide_result(lv_timer_t *timer)
{
    lv_obj_add_flag(s_result, LV_OBJ_FLAG_HIDDEN);
    s_overlay_shown = false;
    if (timer) show_tab(TAB_HOME);
}

static void overlay_show(lv_color_t color, ui_icon_t mark, const char *text, const char *hint)
{
    s_overlay_shown = true;
    lv_display_trigger_activity(NULL);
    lv_obj_set_style_bg_color(s_result_rail, color, 0);
    lv_obj_set_style_text_color(s_result_label, color, 0);
    ui_icon_set(s_result_mark, mark, color);
    lv_label_set_text(s_result_label, text);
    lv_label_set_text(s_result_hint, hint);
    lv_obj_remove_flag(s_result, LV_OBJ_FLAG_HIDDEN);
}

static void result_build(lv_obj_t *screen)
{
    s_result = page_create(screen);
    lv_obj_set_style_bg_color(s_result, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_result, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_result, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_result, LV_OBJ_FLAG_HIDDEN);
    s_result_rail = ui_panel(s_result, 40, 72, 14, 310);
    lv_obj_set_style_bg_opa(s_result_rail, LV_OPA_COVER, 0);
    s_result_mark = ui_icon(s_result, UI_ICON_CHECK, 96, 158, 128, UI_GREEN);
    s_result_label = ui_text(s_result, "", &lab_font_56, 296, 154, 660, UI_INK);
    s_result_hint = ui_text(s_result, "", &lab_font_24, 296, 258, 660, UI_MUTED);
    s_spinner = lv_spinner_create(s_result);
    lv_spinner_set_anim_params(s_spinner, 1000, 90);
    lv_obj_set_pos(s_spinner, 104, 166);
    lv_obj_set_size(s_spinner, 112, 112);
    lv_obj_set_style_arc_color(s_spinner, UI_SOFT, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_spinner, UI_BLUE, LV_PART_INDICATOR);
    lv_obj_add_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
    s_result_timer = lv_timer_create(hide_result, UI_RESULT_SHOW_MS, NULL);
    lv_timer_set_auto_delete(s_result_timer, false);
    lv_timer_pause(s_result_timer);
}

static void clock_update(void)
{
    if (!s_clock_offset_ms) return;
    static const char *const days[] = {"일", "월", "화", "수", "목", "금", "토"};
    time_t seconds = (s_clock_offset_ms + esp_timer_get_time() / 1000) / 1000 + KST_OFFSET_S;
    struct tm t;
    gmtime_r(&seconds, &t);
    lv_label_set_text_fmt(s_clock, "%02d:%02d", t.tm_hour, t.tm_min);
    lv_label_set_text_fmt(s_date, "%d월 %d일", t.tm_mon + 1, t.tm_mday);
    lv_label_set_text_fmt(s_weekday, "%s요일", days[t.tm_wday]);
}

static void on_tick(lv_timer_t *timer)
{
    clock_update();
    if (s_overlay_shown || lv_display_get_inactive_time(NULL) < IDLE_HOME_MS) return;
    ui_settings_close();
    if (s_tab != TAB_HOME) show_tab(TAB_HOME);
}

static void on_art_tick(lv_timer_t *timer)
{
    if (s_tab == TAB_HOME && !s_preview_paused && !s_overlay_shown) ui_art_step();
}

void ui_build(void)
{
    for (int i = 0; i < 2; i++) {
        s_buffers[i] = heap_caps_calloc(1, UI_PREVIEW_W * UI_PREVIEW_H * 2, MALLOC_CAP_SPIRAM);
        s_frames[i].header.cf = LV_COLOR_FORMAT_RGB565;
        s_frames[i].header.magic = LV_IMAGE_HEADER_MAGIC;
        s_frames[i].header.w = UI_PREVIEW_W;
        s_frames[i].header.h = UI_PREVIEW_H;
        s_frames[i].header.stride = UI_PREVIEW_W * 2;
        s_frames[i].data_size = s_buffers[i] ? UI_PREVIEW_W * UI_PREVIEW_H * 2 : 0;
        s_frames[i].data = s_buffers[i];
    }
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    bar_build(screen);
    server_led_paint(UI_SERVER_CHECKING);
    s_server_state = UI_SERVER_CHECKING;
    home_build(screen);
    scan_build(screen);
    guide_build(screen);
    code_build(screen);
    ui_settings_build(screen);
    result_build(screen);
    show_tab(TAB_HOME);
    lv_timer_create(on_tick, 1000, NULL);
    lv_timer_create(on_art_tick, 50, NULL);
    if (!s_buffers[0] || !s_buffers[1]) {
        ESP_LOGE(TAG, "미리보기 메모리 부족");
        lv_label_set_text(s_hint, "미리보기 오류");
    }
}

void ui_status(const char *text)
{
    if (!s_status || !display_lock(LOCK_WAIT_MS)) return;
    ui_icon_set(s_status, strncmp(text, "Wi-Fi: ", 7) == 0 ? UI_ICON_WIFI : UI_ICON_WIFI_OFF,
                 lv_color_white());
    display_unlock();
}

void ui_server_status(ui_server_t state)
{
    if (!s_server_led || state == s_server_state || !display_lock(LOCK_WAIT_MS)) return;
    s_server_state = state;
    server_led_paint(state);
    display_unlock();
}

// WMO 날씨 코드 → 아이콘과 짧은 한글 설명
static ui_icon_t weather_icon(int code, bool day, const char **text)
{
    if (code == 0) { *text = "맑음"; return day ? UI_ICON_SUN : UI_ICON_MOON; }
    if (code <= 2) { *text = "구름조금"; return day ? UI_ICON_CLOUD_SUN : UI_ICON_CLOUD_MOON; }
    if (code == 45 || code == 48) { *text = "안개"; return UI_ICON_FOG; }
    if (code >= 51 && code <= 57) { *text = "이슬비"; return UI_ICON_DRIZZLE; }
    if (code >= 61 && code <= 67) { *text = "비"; return UI_ICON_RAIN; }
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) { *text = "눈"; return UI_ICON_SNOW; }
    if (code >= 80 && code <= 82) { *text = "소나기"; return UI_ICON_RAIN; }
    if (code >= 95) { *text = "뇌우"; return UI_ICON_THUNDER; }
    *text = "흐림";
    return UI_ICON_CLOUD;
}

void ui_weather_set(int wmo_code, bool is_day, float temp_c)
{
    if (!s_weather || !display_lock(LOCK_WAIT_MS)) return;
    if (wmo_code < 0) {
        lv_obj_add_flag(s_weather, LV_OBJ_FLAG_HIDDEN);
    } else {
        const char *text;
        ui_icon_set(s_weather_icon, weather_icon(wmo_code, is_day, &text), lv_color_white());
        int rounded = (int)(temp_c < 0 ? temp_c - 0.5f : temp_c + 0.5f);
        lv_label_set_text_fmt(s_weather_temp, "%d°C", rounded);
        lv_label_set_text(s_weather_desc, text);
        lv_obj_remove_flag(s_weather, LV_OBJ_FLAG_HIDDEN);
    }
    display_unlock();
}

void ui_clock_set(int64_t unix_ms)
{
    if (!s_clock || !display_lock(LOCK_WAIT_MS)) return;
    s_clock_offset_ms = unix_ms - esp_timer_get_time() / 1000;
    clock_update();
    display_unlock();
}

void ui_busy(const char *text)
{
    if (!s_result || !display_lock(LOCK_WAIT_MS)) return;
    lv_timer_pause(s_result_timer);
    set_hidden(s_result_mark, true);
    lv_obj_remove_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
    (void)text;
    overlay_show(UI_BLUE, UI_ICON_QR, "확인 중", "");
    display_unlock();
}

void ui_idle(void)
{
    if (!s_result || !display_lock(LOCK_WAIT_MS)) return;
    hide_result(NULL);
    display_unlock();
}

void ui_code_error(const char *reason)
{
    if (!s_code || !display_lock(LOCK_WAIT_MS)) return;
    hide_result(NULL);
    show_tab(TAB_CODE);
    const char *text = "출입이 허용되지 않았습니다";
    if (strcmp(reason, "manual_invalid") == 0 || strcmp(reason, "invalid_token") == 0) text = "코드를 확인하고 다시 입력하세요";
    else if (strcmp(reason, "manual_rate_limited") == 0) text = "입력이 많습니다. 잠시 후 다시 시도하세요";
    else if (strcmp(reason, "expired_token") == 0) text = "새 출입증의 코드를 입력하세요";
    else if (strcmp(reason, "duplicate_scan") == 0 || strcmp(reason, "token_reused") == 0) text = "이미 사용한 코드입니다";
    else if (strcmp(reason, "wrong_room") == 0) text = "이 실습실의 코드가 아닙니다";
    else if (strcmp(reason, "offline") == 0) text = "서버 연결을 확인하고 다시 시도하세요";
    lv_label_set_text(s_code_error, text);
    display_unlock();
}

void ui_result(bool granted, const char *text)
{
    if (!s_result || !display_lock(LOCK_WAIT_MS)) return;
    bool offline = !granted && strcmp(text, "서버에 연결할 수 없습니다") == 0;
    set_hidden(s_result_mark, false);
    lv_obj_add_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
    overlay_show(granted ? UI_GREEN : offline ? UI_AMBER : UI_RED,
                 granted ? UI_ICON_CHECK : offline ? UI_ICON_WIFI_OFF : UI_ICON_CLOSE,
                 granted ? "출입 승인" : offline ? "연결 끊김" : "출입 불가",
                 granted ? "입장하세요" : offline ? "잠시 후 다시 시도하세요" : "출입증을 확인하세요");
    lv_timer_set_repeat_count(s_result_timer, 1);
    lv_timer_reset(s_result_timer);
    lv_timer_resume(s_result_timer);
    display_unlock();
}

void ui_camera_failed(void)
{
    if (!s_hint || !display_lock(LOCK_WAIT_MS)) return;
    lv_label_set_text(s_hint, "카메라 오류: 관리자 문의");
    lv_obj_set_style_text_color(s_hint, UI_RED, 0);
    display_unlock();
}

uint8_t *ui_preview_buffer(void)
{
    if (!s_scan_visible || s_preview_paused || s_overlay_shown || !s_buffers[0] || !s_buffers[1]) return NULL;
    return s_buffers[!s_shown];
}

void ui_preview_commit(void)
{
    if (!display_lock(LOCK_WAIT_MS)) return;
    s_shown = !s_shown;
    lv_image_set_src(s_preview, &s_frames[s_shown]);
    display_unlock();
}

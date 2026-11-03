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

static const char *TAG = "ui";
#define BODY_H         (LCD_V_RES - UI_BAR_H)
#define FRAME_SIZE     312
#define CORNER_LEN     48
#define CORNER_WIDTH   5
#define GUIDE_QR_SIZE  288
#define LOCK_WAIT_MS   200
#define IDLE_HOME_MS   60000
#define KST_OFFSET_S   (9 * 3600)

typedef enum { TAB_HOME, TAB_SCAN, TAB_GUIDE } tab_t;
static lv_obj_t *s_status, *s_home, *s_scan, *s_guide, *s_hint;
static lv_obj_t *s_clock, *s_date;
static lv_obj_t *s_preview, *s_result, *s_result_label, *s_result_hint, *s_spinner;
static lv_obj_t *s_result_mark, *s_result_rail, *s_result_footer;
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
    s_tab = tab;
    s_scan_visible = tab == TAB_SCAN;
    set_hidden(s_home, tab != TAB_HOME);
    set_hidden(s_scan, tab != TAB_SCAN);
    set_hidden(s_guide, tab != TAB_GUIDE);
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

static lv_obj_t *page_create(lv_obj_t *screen)
{
    return ui_panel(screen, 0, UI_BAR_H, LCD_H_RES, BODY_H);
}

static void footer(lv_obj_t *page, bool home)
{
    ui_button(page, home ? "처음 이용하시나요? 이용안내" : "메인으로", 36, 432,
              home ? 350 : 150, 56, false, on_tab, home ? TAB_GUIDE : TAB_HOME);
    ui_button(page, "관리자 설정", 824, 432, 164, 56, false, on_settings, 0);
}

static void bar_build(lv_obj_t *screen)
{
    ui_header(screen, "실습실 출입");
    s_status = ui_text(screen, "시작 중", &lab_font_24, 580, 29, 408, lv_color_white());
    lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_DOT);
}

static void home_build(lv_obj_t *screen)
{
    s_home = page_create(screen);
    ui_text(s_home, "출입증을\n준비해 주세요", &lab_font_48, 48, 52, 550, UI_INK);
    ui_text(s_home, "승인된 출입증의 QR코드로 입장하세요.", &lab_font_24, 48, 238, 560, UI_MUTED);
    s_clock = ui_text(s_home, "--:--", &lv_font_montserrat_48, 660, 64, 280, UI_INK);
    lv_obj_set_style_text_align(s_clock, LV_TEXT_ALIGN_CENTER, 0);
    s_date = ui_text(s_home, "시간을 확인하고 있습니다", &lab_font_24, 618, 152, 358, UI_MUTED);
    lv_obj_set_style_text_align(s_date, LV_TEXT_ALIGN_CENTER, 0);
    ui_button(s_home, "QR 출입 시작", 48, 336, 380, 76, true, on_tab, TAB_SCAN);
    ui_text(s_home, "휴대폰에서 출입증을 열어 주세요", &lab_font_24, 452, 359, 536, UI_MUTED);
    footer(s_home, true);
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
    ui_text(s_scan, "QR코드를\n사각형에 맞춰 주세요", &lab_font_48, 48, 60, 520, UI_INK);
    ui_text(s_scan, "휴대폰의 출입증 화면을\n카메라에 보여 주세요.", &lab_font_24, 48, 226, 520, UI_MUTED);
    ui_text(s_scan, "1. 출입증 열기   2. QR코드 보여주기", &lab_font_24, 48, 334, 520, UI_BLUE);
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
    s_hint = ui_text(s_scan, "QR코드를 보여 주세요", &lab_font_24, 600, 406, 392, UI_MUTED);
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
    ui_text(s_guide, "처음 이용하시나요?", &lab_font_40, 412, 48, 570, UI_INK);
    lv_obj_t *steps = ui_text(s_guide,
        "1. 휴대폰으로 왼쪽 QR코드를 스캔합니다.\n"
        "2. 가입 후 실습실 이용을 신청합니다.\n"
        "3. 승인된 출입증의 QR코드를 엽니다.\n"
        "4. 단말 카메라에 QR코드를 보여 주세요.",
        &lab_font_24, 412, 132, 570, UI_MUTED);
    lv_obj_set_style_text_line_space(steps, 20, 0);
    footer(s_guide, false);
}

static void hide_result(lv_timer_t *timer)
{
    lv_obj_add_flag(s_result, LV_OBJ_FLAG_HIDDEN);
    s_overlay_shown = false;
    if (timer) show_tab(TAB_HOME);
}

static void overlay_show(lv_color_t color, const char *mark, const char *text, const char *hint)
{
    s_overlay_shown = true;
    lv_display_trigger_activity(NULL);
    lv_obj_set_style_bg_color(s_result_rail, color, 0);
    lv_obj_set_style_text_color(s_result_label, color, 0);
    lv_obj_set_style_text_color(s_result_mark, color, 0);
    lv_label_set_text(s_result_mark, mark);
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
    s_result_mark = ui_text(s_result, "", &lv_font_montserrat_48, 90, 174, 106, UI_GREEN);
    lv_obj_set_style_text_align(s_result_mark, LV_TEXT_ALIGN_CENTER, 0);
    s_result_label = ui_text(s_result, "", &lab_font_40, 236, 148, 740, UI_INK);
    s_result_hint = ui_text(s_result, "", &lab_font_24, 236, 250, 728, UI_MUTED);
    s_result_footer = ui_text(s_result, "잠시 후 대기 화면으로 돌아갑니다", &lab_font_24, 236, 394, 728, UI_MUTED);
    s_spinner = lv_spinner_create(s_result);
    lv_spinner_set_anim_params(s_spinner, 1000, 90);
    lv_obj_set_pos(s_spinner, 98, 158);
    lv_obj_set_size(s_spinner, 80, 80);
    lv_obj_set_style_arc_color(s_spinner, UI_SOFT, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_spinner, UI_BLUE, LV_PART_INDICATOR);
    lv_obj_add_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
    s_result_timer = lv_timer_create(hide_result, UI_RESULT_SHOW_MS, NULL);
    lv_timer_set_auto_delete(s_result_timer, false);
    lv_timer_pause(s_result_timer);
    set_hidden(s_result_footer, true);
}

static void clock_update(void)
{
    if (!s_clock_offset_ms) return;
    static const char *const days[] = {"일", "월", "화", "수", "목", "금", "토"};
    time_t seconds = (s_clock_offset_ms + esp_timer_get_time() / 1000) / 1000 + KST_OFFSET_S;
    struct tm t;
    gmtime_r(&seconds, &t);
    lv_label_set_text_fmt(s_clock, "%02d:%02d", t.tm_hour, t.tm_min);
    lv_label_set_text_fmt(s_date, "%d년 %d월 %d일 %s요일", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, days[t.tm_wday]);
}

static void on_tick(lv_timer_t *timer)
{
    clock_update();
    if (s_overlay_shown || lv_display_get_inactive_time(NULL) < IDLE_HOME_MS) return;
    ui_settings_close();
    if (s_tab != TAB_HOME) show_tab(TAB_HOME);
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
    home_build(screen);
    scan_build(screen);
    guide_build(screen);
    ui_settings_build(screen);
    result_build(screen);
    show_tab(TAB_HOME);
    lv_timer_create(on_tick, 1000, NULL);
    if (!s_buffers[0] || !s_buffers[1]) {
        ESP_LOGE(TAG, "미리보기 메모리 부족");
        lv_label_set_text(s_hint, "미리보기를 표시할 수 없습니다");
    }
}

void ui_status(const char *text)
{
    if (!s_status || !display_lock(LOCK_WAIT_MS)) return;
    lv_label_set_text(s_status, text);
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
    set_hidden(s_result_footer, true);
    lv_obj_remove_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
    overlay_show(UI_BLUE, "", text, "출입증을 확인하고 있습니다. 잠시만 기다려 주세요.");
    display_unlock();
}

void ui_idle(void)
{
    if (!s_result || !display_lock(LOCK_WAIT_MS)) return;
    hide_result(NULL);
    display_unlock();
}

void ui_result(bool granted, const char *text)
{
    if (!s_result || !display_lock(LOCK_WAIT_MS)) return;
    bool offline = !granted && strcmp(text, "서버에 연결할 수 없습니다") == 0;
    set_hidden(s_result_footer, false);
    lv_obj_add_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
    overlay_show(granted ? UI_GREEN : offline ? UI_AMBER : UI_RED,
                 granted ? LV_SYMBOL_OK : offline ? LV_SYMBOL_WARNING : LV_SYMBOL_CLOSE, text,
                 granted ? "문이 열립니다. 입장해 주세요." : offline ?
                 "잠시 후 QR코드를 다시 보여 주세요.\n연결되기 전에는 문이 열리지 않습니다." :
                 "휴대폰에서 출입증의 승인 상태를 확인해 주세요.\n도움이 필요하면 관리자에게 문의하세요.");
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

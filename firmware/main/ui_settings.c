// 설정 화면: 단말 비밀번호 확인 후 설정 메뉴
//  - Wi-Fi: 저장된 네트워크 열람·연결·삭제, 새 네트워크 검색
//  - 문 열림 시간(릴레이 작동 시간), 단말 비밀번호 변경
//  - 음성 안내 켜기/끄기·음성 종류·볼륨, 화면 밝기·테마 색상
// 설정값은 settings.c가 NVS에 저장한다.
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "lvgl.h"
#include "sdkconfig.h"
#include "app.h"
#include "ui_theme.h"

static const char *TAG = "ui_settings";

#define MAX_APS 20
#define LOCK_WAIT_MS 1000

// 관리자 PIN 화면 왼쪽 아래 문의 안내. 문구를 바꾸면 tools/gen_hangul_font.py를 다시 실행한다.
#define CONTACT_NAME   "취직자리 구하는 근로학생 김형우가 만들었습니다. 혹시 이런거 필요하시면 저 일시켜 주세요 ㅠ"
#define CONTACT_EMAIL  "여기로 연락 주세요!! audreaming3@gmail.com 밑의 github들어가시면 제가 만든 프로젝트들 많아요"
#define CONTACT_URL    "https://github.com/khw1566/lab-qr-access"
#define CONTACT_QR     108
// 문의 칸은 키패드 아래 화면 맨 아래 띠(y 484~600)에 둔다. 글자는 너비 안에서 줄바꿈되고 4줄까지 들어간다.
#define CONTACT_Y      484
#define CONTACT_TEXT_X 164

// 음성 이름은 tools/gen_voice.py의 VOICES 순서와 같다
static const char *const s_voice_names[VOICE_COUNT] = { "여성 1", "여성 2", "남성 1", "남성 2" };
// 테마 이름은 ui.c의 ui_themes 순서와 같다
static const char *const s_theme_names[SETTINGS_THEME_COUNT] = { "파랑", "청록", "초록", "보라", "진회색" };

static const char *const s_pin_map[] = {
    "1", "2", "3", "\n",
    "4", "5", "6", "\n",
    "7", "8", "9", "\n",
    "지움", "0", "확인", "",
};

static const char *const s_voice_map[] = { "여성 1", "여성 2", "남성 1", "남성 2", "" };

typedef enum { PAGE_PIN, PAGE_MENU, PAGE_WIFI, PAGE_SCAN, PAGE_PASS, PAGE_DOOR, PAGE_PINSET, PAGE_VOICE, PAGE_DISPLAY, PAGE_COUNT } page_t;
static lv_obj_t *s_root, *s_pages[PAGE_COUNT];
static lv_obj_t *s_pin_input, *s_pin_error, *s_build_info;
static lv_obj_t *s_menu_values[5], *s_menu_info;
static lv_obj_t *s_wifi_current, *s_saved_list, *s_ap_list, *s_pass_title, *s_pass_input;
static lv_obj_t *s_door_value, *s_door_slider, *s_door_note;
static lv_obj_t *s_pinset_step, *s_pinset_input, *s_pinset_note;
static lv_obj_t *s_voice_switch, *s_voice_buttons, *s_volume_slider, *s_volume_value;
static lv_obj_t *s_bright_slider, *s_bright_value, *s_theme_swatches[SETTINGS_THEME_COUNT], *s_theme_note;
static wifi_ap_t s_aps[MAX_APS];
static char s_saved[WIFI_SAVED_MAX][33];
static int s_selected, s_theme_choice;
static char s_pinset_first[SETTINGS_PIN_MAX + 1];
static volatile bool s_scanning;

static void menu_refresh(void);

static lv_obj_t *page_create(void)
{
    lv_obj_t *page = lv_obj_create(s_root);
    lv_obj_remove_style_all(page);
    lv_obj_set_size(page, LCD_H_RES, LCD_V_RES);
    lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    return page;
}

static lv_obj_t *label_create(lv_obj_t *parent, const char *text, const lv_font_t *font)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, UI_INK, 0);
    lv_label_set_text(label, text);
    return label;
}

static void page_show(page_t page)
{
    for (int i = 0; i < PAGE_COUNT; i++) {
        if (i == page) lv_obj_remove_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void settings_close(void)
{
    lv_textarea_set_text(s_pin_input, "");
    lv_textarea_set_text(s_pass_input, "");
    lv_textarea_set_text(s_pinset_input, "");
    s_pinset_first[0] = 0;
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    ui_settings_closed();
}

static void on_close(lv_event_t *e) { settings_close(); }

void ui_settings_close(void)
{
    if (!lv_obj_has_flag(s_root, LV_OBJ_FLAG_HIDDEN)) settings_close();
}

static void menu_open(void)
{
    menu_refresh();
    page_show(PAGE_MENU);
}

static void on_back(lv_event_t *e)
{
    page_t to = (page_t)(intptr_t)lv_event_get_user_data(e);
    if (to == PAGE_MENU) menu_open();
    else page_show(to);
}

// ---------- 공통 위젯 ----------

static void white_content(lv_obj_t *button)
{
    for (unsigned i = 0; i < lv_obj_get_child_count(button); i++) {
        lv_obj_t *child = lv_obj_get_child(button, i);
        if (lv_obj_check_type(child, &lv_image_class)) ui_icon_color(child, lv_color_white());
        else lv_obj_set_style_text_color(child, lv_color_white(), 0);
    }
}

// 파란 상단 바: 제목, (하위 화면이면) 뒤로, 닫기
static void page_header(lv_obj_t *page, const char *title, int back_to)
{
    ui_header(page, title);
    if (back_to >= 0) {
        lv_obj_t *back = ui_button(page, LV_SYMBOL_LEFT " 뒤로", 740, 18, 150, 56, false, on_back, back_to);
        lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(back, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(back, UI_INK, LV_STATE_PRESSED);
        white_content(back);
    }
    lv_obj_t *close = ui_icon_button(page, UI_ICON_CLOSE, "", 916, 12, 72, 68, false, on_close, 0);
    ui_icon_color(lv_obj_get_child(close, -1), lv_color_white());
}

static lv_obj_t *input_create(lv_obj_t *parent, int width)
{
    lv_obj_t *input = lv_textarea_create(parent);
    lv_obj_set_width(input, width);
    lv_textarea_set_one_line(input, true);
    lv_textarea_set_password_mode(input, true);
    lv_obj_set_style_text_font(input, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(input, UI_INK, 0);
    lv_obj_set_style_bg_color(input, lv_color_white(), 0);
    lv_obj_set_style_border_color(input, UI_LINE, 0);
    lv_obj_set_style_border_color(input, UI_BLUE, LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(input, 2, 0);
    lv_obj_set_style_radius(input, 8, 0);
    lv_obj_set_style_pad_all(input, 14, 0);
    return input;
}

static void keypad_style(lv_obj_t *matrix)
{
    lv_obj_set_style_bg_color(matrix, lv_color_white(), 0);
    lv_obj_set_style_border_width(matrix, 0, 0);
    lv_obj_set_style_pad_all(matrix, 8, 0);
    lv_obj_set_style_pad_row(matrix, 10, 0);
    lv_obj_set_style_pad_column(matrix, 10, 0);
    lv_obj_set_style_bg_color(matrix, UI_SOFT, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(matrix, UI_LINE, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(matrix, UI_INK, LV_PART_ITEMS);
    lv_obj_set_style_border_width(matrix, 0, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(matrix, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(matrix, 8, LV_PART_ITEMS);
}

static lv_obj_t *keypad_create(lv_obj_t *parent, int x, int y, lv_event_cb_t cb)
{
    lv_obj_t *matrix = lv_buttonmatrix_create(parent);
    lv_buttonmatrix_set_map(matrix, s_pin_map);
    lv_obj_set_size(matrix, 360, 320);
    lv_obj_set_pos(matrix, x, y);
    keypad_style(matrix);
    lv_obj_set_style_text_font(matrix, &lab_font_28, 0);
    lv_obj_add_event_cb(matrix, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return matrix;
}

static const char *keypad_key(lv_event_t *e)
{
    lv_obj_t *matrix = lv_event_get_target(e);
    return lv_buttonmatrix_get_button_text(matrix, lv_buttonmatrix_get_selected_button(matrix));
}

static lv_obj_t *slider_create(lv_obj_t *parent, int x, int y, int width, int min, int max, lv_event_cb_t cb)
{
    lv_obj_t *slider = lv_slider_create(parent);
    lv_obj_set_pos(slider, x, y);
    lv_obj_set_size(slider, width, 18);
    lv_slider_set_range(slider, min, max);
    lv_obj_set_ext_click_area(slider, 24);
    lv_obj_set_style_bg_color(slider, UI_LINE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, UI_BLUE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, UI_BLUE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 12, LV_PART_KNOB);
    lv_obj_set_style_border_color(slider, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_border_width(slider, 4, LV_PART_KNOB);
    lv_obj_add_event_cb(slider, cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(slider, cb, LV_EVENT_RELEASED, NULL);
    return slider;
}

static lv_obj_t *muted_label(lv_obj_t *parent, const char *text, int x, int y, int width)
{
    lv_obj_t *label = label_create(parent, text, &lab_font_24);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, width);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(label, UI_MUTED, 0);
    return label;
}

// ---------- 비밀번호 확인 ----------

static void on_pin_key(lv_event_t *e)
{
    const char *key = keypad_key(e);
    if (!key) return;
    if (strcmp(key, "지움") == 0) {
        lv_textarea_delete_char(s_pin_input);
    } else if (strcmp(key, "확인") == 0) {
        const char *pin = settings_admin_pin();
        bool ok = pin[0] && strcmp(lv_textarea_get_text(s_pin_input), pin) == 0;
        lv_textarea_set_text(s_pin_input, "");
        lv_label_set_text(s_pin_error, ok ? "" : "비밀번호가 맞지 않습니다");
        if (ok) menu_open();
    } else {
        lv_label_set_text(s_pin_error, "");
        lv_textarea_add_text(s_pin_input, key);
    }
}

static void build_info_update(void)
{
    // 보드에 어떤 펌웨어가 들어 있는지 바로 보이게 한다 (git 버전, 빌드 시각, 릴레이 핀, 열림 시간)
    const esp_app_desc_t *app = esp_app_get_description();
    lv_label_set_text_fmt(s_build_info, "FW %s  %s %s\nrelay %d,%d,%d  %dms", app->version, app->date, app->time,
                          CONFIG_LAB_RELAY_GPIO, CONFIG_LAB_RELAY_GPIO2, CONFIG_LAB_RELAY_GPIO3, settings_door_ms());
}

static void pin_page_build(void)
{
    lv_obj_t *page = s_pages[PAGE_PIN] = page_create();
    page_header(page, "관리자 설정", -1);
    ui_icon(page, UI_ICON_SETTINGS, 64, 132, 64, UI_BLUE);
    lv_obj_set_pos(label_create(page, "관리자 PIN", &lab_font_40), 64, 228);
    s_pin_input = input_create(page, 360);
    lv_textarea_set_max_length(s_pin_input, SETTINGS_PIN_MAX);
    lv_textarea_set_placeholder_text(s_pin_input, "");
    lv_obj_set_pos(s_pin_input, 64, 288);
    lv_obj_set_style_text_align(s_pin_input, LV_TEXT_ALIGN_CENTER, 0);
    s_pin_error = label_create(page, "", &lab_font_24);
    lv_obj_set_pos(s_pin_error, 64, 376);
    lv_obj_set_width(s_pin_error, 480);
    lv_obj_set_style_text_color(s_pin_error, UI_RED, 0);

    keypad_create(page, 600, 148, on_pin_key);

    s_build_info = label_create(page, "", &lab_font_24);
    lv_obj_set_pos(s_build_info, 64, 412);
    lv_obj_set_width(s_build_info, 520);
    lv_label_set_long_mode(s_build_info, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_build_info, UI_MUTED, 0);

    // 담당자 문의: 프로젝트 GitHub QR과 연락처
    lv_obj_t *qr = lv_qrcode_create(page);
    lv_qrcode_set_size(qr, CONTACT_QR);
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());
    lv_qrcode_update(qr, CONTACT_URL, strlen(CONTACT_URL));
    lv_obj_set_pos(qr, 36, CONTACT_Y);
    lv_obj_t *who = label_create(page, CONTACT_NAME "\n" CONTACT_EMAIL, &lab_font_24);
    lv_obj_set_pos(who, CONTACT_TEXT_X, CONTACT_Y);
    lv_obj_set_size(who, LCD_H_RES - CONTACT_TEXT_X - 36, LCD_V_RES - CONTACT_Y);
    lv_label_set_long_mode(who, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(who, 2, 0);
    lv_obj_set_style_text_color(who, UI_MUTED, 0);
}

// ---------- 설정 메뉴 ----------

static void wifi_page_open(void);
static void door_page_open(void);
static void pinset_page_open(void);
static void voice_page_open(void);
static void display_page_open(void);

static void on_menu(lv_event_t *e)
{
    switch ((intptr_t)lv_event_get_user_data(e)) {
    case 0: wifi_page_open(); break;
    case 1: door_page_open(); break;
    case 2: pinset_page_open(); break;
    case 3: voice_page_open(); break;
    default: display_page_open(); break;
    }
}

#define TILE_W 300
#define TILE_H 196
#define TILE_GAP 26

static lv_obj_t *menu_tile(lv_obj_t *page, int index, const char *symbol, const char *title)
{
    int x = 36 + (index % 3) * (TILE_W + TILE_GAP), y = 116 + (index / 3) * (TILE_H + 24);
    lv_obj_t *tile = ui_button(page, "", x, y, TILE_W, TILE_H, false, on_menu, index);
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_set_style_border_color(tile, UI_LINE, 0);
    lv_obj_set_style_border_width(tile, 2, 0);
    lv_obj_t *icon = label_create(tile, symbol, &lv_font_montserrat_40);
    lv_obj_set_style_text_color(icon, UI_BLUE, 0);
    lv_obj_set_pos(icon, 24, 24);
    lv_obj_set_pos(label_create(tile, title, &lab_font_28), 24, 88);
    lv_obj_t *value = muted_label(tile, "", 24, 132, TILE_W - 48);
    lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
    lv_obj_set_height(value, 56);
    return value;
}

static void menu_refresh(void)
{
    char text[64];
    snprintf(text, sizeof(text), "%s\n%s", wifi_current_ssid(), wifi_is_connected() ? "연결됨" : "연결 안 됨");
    lv_label_set_text(s_menu_values[0], text);
    lv_label_set_text_fmt(s_menu_values[1], "%d초 동안 열림", settings_door_ms() / 1000);
    lv_label_set_text(s_menu_values[2], settings_admin_pin_custom() ? "변경한 비밀번호 사용 중" : "기본 비밀번호 사용 중");
    if (settings_voice_on()) lv_label_set_text_fmt(s_menu_values[3], "%s, 볼륨 %d", s_voice_names[settings_voice_id()], settings_volume());
    else lv_label_set_text(s_menu_values[3], "음성 안내 꺼짐");
    lv_label_set_text_fmt(s_menu_values[4], "밝기 %d%%, %s 테마", settings_brightness(), s_theme_names[settings_theme()]);
    const esp_app_desc_t *app = esp_app_get_description();
    lv_label_set_text_fmt(s_menu_info, "펌웨어 %s\n빌드 %s", app->version, app->date);
}

static void menu_page_build(void)
{
    lv_obj_t *page = s_pages[PAGE_MENU] = page_create();
    page_header(page, "단말 설정", -1);
    s_menu_values[0] = menu_tile(page, 0, LV_SYMBOL_WIFI, "Wi-Fi");
    s_menu_values[1] = menu_tile(page, 1, LV_SYMBOL_CHARGE, "문 열림 시간");
    s_menu_values[2] = menu_tile(page, 2, LV_SYMBOL_KEYBOARD, "단말 비밀번호");
    s_menu_values[3] = menu_tile(page, 3, LV_SYMBOL_VOLUME_MAX, "음성 안내");
    s_menu_values[4] = menu_tile(page, 4, LV_SYMBOL_IMAGE, "화면 밝기·테마");
    // 여섯째 칸: 펌웨어 정보 (누를 수 없음)
    int x = 36 + 2 * (TILE_W + TILE_GAP), y = 116 + TILE_H + 24;
    lv_obj_t *info = ui_panel(page, x, y, TILE_W, TILE_H);
    lv_obj_set_style_bg_color(info, UI_SOFT, 0);
    lv_obj_set_style_bg_opa(info, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(info, 8, 0);
    lv_obj_set_pos(label_create(info, "단말 정보", &lab_font_28), 24, 24);
    s_menu_info = muted_label(info, "", 24, 72, TILE_W - 48);
}

// ---------- Wi-Fi: 저장된 네트워크 ----------

static void saved_list_fill(void);

static void on_saved_connect(lv_event_t *e)
{
    int i = (intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= WIFI_SAVED_MAX) return;
    wifi_saved_connect(s_saved[i]);
    wifi_page_open();
}

static void on_saved_forget(lv_event_t *e)
{
    int i = (intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= WIFI_SAVED_MAX) return;
    wifi_saved_forget(s_saved[i]);
    wifi_page_open();
}

static void saved_list_fill(void)
{
    lv_obj_clean(s_saved_list);
    int count = wifi_saved_list(s_saved, WIFI_SAVED_MAX);
    if (!count) {
        muted_label(s_saved_list, "저장된 네트워크가 없습니다. 오른쪽 위 '새 네트워크'로 찾아 연결하세요.", 20, 20, 880);
        return;
    }
    const char *current = wifi_current_ssid();
    for (int i = 0; i < count; i++) {
        bool active = strcmp(s_saved[i], current) == 0;
        lv_obj_t *row = ui_panel(s_saved_list, 0, i * 84, LCD_H_RES - 76, 76);
        lv_obj_set_style_bg_color(row, active ? UI_SOFT : lv_color_white(), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(row, UI_LINE, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_t *name = label_create(row, s_saved[i], &lab_font_28);
        lv_obj_set_pos(name, 20, 8);
        lv_obj_set_width(name, 600);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        muted_label(row, active ? (wifi_is_connected() ? "지금 연결됨" : "연결 중") : (i == 0 ? "마지막으로 연결" : "저장됨"),
                    20, 44, 600);
        if (!active) ui_button(row, "연결", 632, 10, 132, 56, true, on_saved_connect, i);
        lv_obj_t *forget = ui_button(row, "삭제", 780, 10, 132, 56, false, on_saved_forget, i);
        lv_obj_set_style_text_color(lv_obj_get_child(forget, 0), UI_RED, 0);
    }
}

static void on_scan_open(lv_event_t *e);

static void wifi_page_open(void)
{
    char text[80];
    snprintf(text, sizeof(text), "현재: %s (%s)", wifi_current_ssid(), wifi_is_connected() ? "연결됨" : "연결 안 됨");
    lv_label_set_text(s_wifi_current, text);
    saved_list_fill();
    page_show(PAGE_WIFI);
}

static void wifi_page_build(void)
{
    lv_obj_t *page = s_pages[PAGE_WIFI] = page_create();
    page_header(page, "Wi-Fi 설정", PAGE_MENU);
    s_wifi_current = label_create(page, "", &lab_font_24);
    lv_obj_set_pos(s_wifi_current, 36, 118);
    lv_obj_set_width(s_wifi_current, 680);
    lv_label_set_long_mode(s_wifi_current, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_wifi_current, UI_MUTED, 0);
    ui_button(page, "새 네트워크", 748, 104, 240, 56, true, on_scan_open, 0);
    lv_obj_t *title = label_create(page, "저장된 네트워크 (최근 연결 순)", &lab_font_24);
    lv_obj_set_pos(title, 36, 168);

    s_saved_list = lv_obj_create(page);
    lv_obj_set_size(s_saved_list, LCD_H_RES - 72, 380);
    lv_obj_set_pos(s_saved_list, 36, 204);
    lv_obj_set_style_bg_color(s_saved_list, lv_color_white(), 0);
    lv_obj_set_style_border_color(s_saved_list, UI_LINE, 0);
    lv_obj_set_style_radius(s_saved_list, 8, 0);
    lv_obj_set_style_pad_all(s_saved_list, 2, 0);
    lv_obj_set_scroll_dir(s_saved_list, LV_DIR_VER);
}

// ---------- Wi-Fi: 새 네트워크 검색 ----------

static void on_ap_click(lv_event_t *e);

static void ap_list_fill(int count)
{
    lv_obj_clean(s_ap_list);
    if (count <= 0) {
        lv_list_add_text(s_ap_list, count < 0 ? "검색에 실패했습니다" : "찾은 네트워크가 없습니다");
        return;
    }
    int saved = wifi_saved_list(s_saved, WIFI_SAVED_MAX);
    for (int i = 0; i < count; i++) {
        bool known = false;
        for (int j = 0; j < saved && !known; j++) known = strcmp(s_saved[j], s_aps[i].ssid) == 0;
        char text[80];
        snprintf(text, sizeof(text), "%s  (%d dBm%s%s)", s_aps[i].ssid, s_aps[i].rssi,
                 s_aps[i].secured ? "" : ", 개방형", known ? ", 저장됨" : "");
        lv_obj_t *item = lv_list_add_button(s_ap_list, s_aps[i].secured ? LV_SYMBOL_WIFI : LV_SYMBOL_EYE_OPEN, text);
        lv_obj_set_style_bg_color(item, lv_color_white(), 0);
        lv_obj_set_style_bg_color(item, UI_SOFT, LV_STATE_PRESSED);
        lv_obj_set_style_text_color(item, UI_INK, 0);
        lv_obj_set_style_pad_ver(item, 18, 0);
        lv_obj_set_style_border_color(item, UI_LINE, 0);
        lv_obj_add_event_cb(item, on_ap_click, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

static void scan_task(void *arg)
{
    int count = wifi_scan(s_aps, MAX_APS);
    if (display_lock(LOCK_WAIT_MS)) {
        ap_list_fill(count);
        display_unlock();
    } else {
        ESP_LOGW(TAG, "화면 잠금 시간 초과");
    }
    s_scanning = false;
    vTaskDelete(NULL);
}

static void scan_start(void)
{
    if (s_scanning) return;
    s_scanning = true;
    lv_obj_clean(s_ap_list);
    lv_list_add_text(s_ap_list, "검색 중...");
    if (xTaskCreate(scan_task, "wifi_scan", 6144, NULL, 3, NULL) != pdPASS) s_scanning = false;
}

static void on_rescan(lv_event_t *e) { scan_start(); }

static void on_scan_open(lv_event_t *e)
{
    page_show(PAGE_SCAN);
    scan_start();
}

static void on_ap_click(lv_event_t *e)
{
    s_selected = (intptr_t)lv_event_get_user_data(e);
    // 저장된 네트워크는 저장된 비밀번호로 바로 연결한다
    if (!wifi_saved_connect(s_aps[s_selected].ssid)) {
        if (s_aps[s_selected].secured) {
            char text[64];
            snprintf(text, sizeof(text), "%s 비밀번호", s_aps[s_selected].ssid);
            lv_label_set_text(s_pass_title, text);
            lv_textarea_set_text(s_pass_input, "");
            page_show(PAGE_PASS);
            return;
        }
        wifi_connect_to(s_aps[s_selected].ssid, "");
    }
    wifi_page_open();
}

static void scan_page_build(void)
{
    lv_obj_t *page = s_pages[PAGE_SCAN] = page_create();
    page_header(page, "새 네트워크", PAGE_WIFI);
    lv_obj_t *hint = label_create(page, "연결할 네트워크를 누르세요", &lab_font_24);
    lv_obj_set_pos(hint, 36, 118);
    lv_obj_set_style_text_color(hint, UI_MUTED, 0);
    lv_obj_t *rescan = ui_icon_button(page, UI_ICON_WIFI, "다시 검색", 748, 104, 240, 56, true, on_rescan, 0);
    (void)rescan;

    s_ap_list = lv_list_create(page);
    lv_obj_set_size(s_ap_list, LCD_H_RES - 72, 410);
    lv_obj_set_pos(s_ap_list, 36, 172);
    lv_obj_set_style_bg_color(s_ap_list, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_ap_list, &lab_font_24, 0);
    lv_obj_set_style_text_color(s_ap_list, UI_INK, 0);
    lv_obj_set_style_border_color(s_ap_list, UI_LINE, 0);
    lv_obj_set_style_radius(s_ap_list, 8, 0);
}

static void on_keyboard(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_READY) {
        wifi_connect_to(s_aps[s_selected].ssid, lv_textarea_get_text(s_pass_input));
        lv_textarea_set_text(s_pass_input, "");
        wifi_page_open();
    } else if (lv_event_get_code(e) == LV_EVENT_CANCEL) {
        page_show(PAGE_SCAN);
    }
}

static void pass_page_build(void)
{
    lv_obj_t *page = s_pages[PAGE_PASS] = page_create();
    page_header(page, "Wi-Fi 연결", PAGE_SCAN);
    s_pass_title = label_create(page, "", &lab_font_24);
    lv_obj_set_pos(s_pass_title, 36, 108);
    lv_obj_set_width(s_pass_title, 952);
    lv_label_set_long_mode(s_pass_title, LV_LABEL_LONG_DOT);
    s_pass_input = input_create(page, LCD_H_RES - 72);
    lv_textarea_set_max_length(s_pass_input, 64);
    lv_obj_set_pos(s_pass_input, 36, 152);

    lv_obj_t *keyboard = lv_keyboard_create(page);
    lv_obj_set_size(keyboard, LCD_H_RES, 360);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(keyboard, &lv_font_montserrat_24, 0);
    keypad_style(keyboard);
    lv_keyboard_set_textarea(keyboard, s_pass_input);
    lv_obj_add_event_cb(keyboard, on_keyboard, LV_EVENT_ALL, NULL);
}

// ---------- 문 열림 시간 ----------

static void door_value_show(int seconds)
{
    lv_label_set_text_fmt(s_door_value, "%d초", seconds);
}

static void door_save(int seconds)
{
    settings_set_door_ms(seconds * 1000);
    lv_slider_set_value(s_door_slider, settings_door_ms() / 1000, LV_ANIM_OFF);
    door_value_show(settings_door_ms() / 1000);
    lv_label_set_text(s_door_note, "저장했습니다. 다음 출입부터 적용됩니다.");
}

static void on_door_slider(lv_event_t *e)
{
    int seconds = lv_slider_get_value(s_door_slider);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) door_save(seconds);
    else door_value_show(seconds);
}

static void on_door_step(lv_event_t *e)
{
    door_save(settings_door_ms() / 1000 + (int)(intptr_t)lv_event_get_user_data(e));
}

static void on_door_test(lv_event_t *e)
{
    ESP_LOGW(TAG, "설정 화면에서 문 열기 시험 (%d ms)", settings_door_ms());
    door_open(settings_door_ms());
    lv_label_set_text_fmt(s_door_note, "릴레이를 %d초 동안 켰습니다.", settings_door_ms() / 1000);
}

static void door_page_open(void)
{
    lv_slider_set_value(s_door_slider, settings_door_ms() / 1000, LV_ANIM_OFF);
    door_value_show(settings_door_ms() / 1000);
    lv_label_set_text(s_door_note, "");
    page_show(PAGE_DOOR);
}

static void door_page_build(void)
{
    lv_obj_t *page = s_pages[PAGE_DOOR] = page_create();
    page_header(page, "문 열림 시간", PAGE_MENU);
    muted_label(page, "출입이 승인되면 릴레이를 이 시간 동안 켭니다 (1~60초).", 36, 118, 952);
    s_door_value = label_create(page, "", &lab_font_64);
    lv_obj_set_width(s_door_value, 300);
    lv_obj_set_style_text_align(s_door_value, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_door_value, 362, 180);
    ui_button(page, "-1초", 36, 176, 160, 80, false, on_door_step, -1);
    ui_button(page, "+1초", 828, 176, 160, 80, false, on_door_step, 1);
    s_door_slider = slider_create(page, 60, 316, 904, SETTINGS_DOOR_MIN_MS / 1000, SETTINGS_DOOR_MAX_MS / 1000, on_door_slider);
    muted_label(page, "1초", 36, 350, 100);
    lv_obj_t *max = muted_label(page, "60초", 888, 350, 100);
    lv_obj_set_style_text_align(max, LV_TEXT_ALIGN_RIGHT, 0);
    ui_button(page, "문 열기 시험", 36, 420, 300, 80, true, on_door_test, 0);
    s_door_note = muted_label(page, "", 364, 444, 624);
}

// ---------- 단말 비밀번호 변경 ----------

static void pinset_reset_steps(const char *note)
{
    s_pinset_first[0] = 0;
    lv_textarea_set_text(s_pinset_input, "");
    lv_label_set_text(s_pinset_step, "새 비밀번호 (숫자 4~16자리)");
    lv_label_set_text(s_pinset_note, note);
}

static void on_pinset_key(lv_event_t *e)
{
    const char *key = keypad_key(e);
    if (!key) return;
    if (strcmp(key, "지움") == 0) {
        lv_textarea_delete_char(s_pinset_input);
        return;
    }
    if (strcmp(key, "확인") != 0) {
        lv_textarea_add_text(s_pinset_input, key);
        return;
    }
    const char *text = lv_textarea_get_text(s_pinset_input);
    size_t n = strlen(text);
    if (!s_pinset_first[0]) {
        if (n < SETTINGS_PIN_MIN) {
            lv_label_set_text(s_pinset_note, "4자리 이상 입력하세요");
            return;
        }
        strlcpy(s_pinset_first, text, sizeof(s_pinset_first));
        lv_textarea_set_text(s_pinset_input, "");
        lv_label_set_text(s_pinset_step, "한 번 더 입력하세요");
        lv_label_set_text(s_pinset_note, "");
        return;
    }
    if (strcmp(s_pinset_first, text) != 0) {
        pinset_reset_steps("두 번 입력한 비밀번호가 다릅니다. 처음부터 다시 입력하세요.");
        return;
    }
    bool ok = settings_set_admin_pin(text);
    pinset_reset_steps(ok ? "비밀번호를 바꿨습니다. 설정 화면과 비상 문 열기에 새 비밀번호를 쓰세요."
                          : "저장하지 못했습니다. 다시 시도하세요.");
}

static void on_pinset_default(lv_event_t *e)
{
    bool ok = settings_set_admin_pin("");
    pinset_reset_steps(ok ? "펌웨어 기본 비밀번호로 되돌렸습니다." : "저장하지 못했습니다. 다시 시도하세요.");
}

static void pinset_page_open(void)
{
    pinset_reset_steps(settings_admin_pin_custom() ? "지금은 단말에서 바꾼 비밀번호를 쓰고 있습니다."
                                                   : "지금은 펌웨어 기본 비밀번호를 쓰고 있습니다.");
    page_show(PAGE_PINSET);
}

static void pinset_page_build(void)
{
    lv_obj_t *page = s_pages[PAGE_PINSET] = page_create();
    page_header(page, "단말 비밀번호 변경", PAGE_MENU);
    s_pinset_step = label_create(page, "", &lab_font_28);
    lv_obj_set_pos(s_pinset_step, 64, 140);
    s_pinset_input = input_create(page, 440);
    lv_textarea_set_max_length(s_pinset_input, SETTINGS_PIN_MAX);
    lv_textarea_set_accepted_chars(s_pinset_input, "0123456789");
    lv_obj_set_pos(s_pinset_input, 64, 196);
    lv_obj_set_style_text_align(s_pinset_input, LV_TEXT_ALIGN_CENTER, 0);
    s_pinset_note = muted_label(page, "", 64, 286, 480);
    ui_button(page, "기본값으로 되돌리기", 64, 470, 340, 72, false, on_pinset_default, 0);
    keypad_create(page, 600, 148, on_pinset_key);
}

// ---------- 음성 안내 ----------

static void voice_controls_update(void)
{
    bool on = settings_voice_on();
    if (on) lv_obj_add_state(s_voice_switch, LV_STATE_CHECKED);
    else lv_obj_remove_state(s_voice_switch, LV_STATE_CHECKED);
    lv_buttonmatrix_set_selected_button(s_voice_buttons, settings_voice_id());
    for (int i = 0; i < VOICE_COUNT; i++) {
        if (i == settings_voice_id()) lv_buttonmatrix_set_button_ctrl(s_voice_buttons, i, LV_BUTTONMATRIX_CTRL_CHECKED);
        else lv_buttonmatrix_clear_button_ctrl(s_voice_buttons, i, LV_BUTTONMATRIX_CTRL_CHECKED);
    }
    lv_slider_set_value(s_volume_slider, settings_volume(), LV_ANIM_OFF);
    lv_label_set_text_fmt(s_volume_value, "%d", settings_volume());
}

static void on_voice_switch(lv_event_t *e)
{
    settings_set_voice_on(lv_obj_has_state(s_voice_switch, LV_STATE_CHECKED));
}

static void on_voice_choice(lv_event_t *e)
{
    uint32_t id = lv_buttonmatrix_get_selected_button(s_voice_buttons);
    if (id >= VOICE_COUNT) return;
    settings_set_voice_id((int)id);
    voice_controls_update();
    voice_preview();
}

static void on_volume(lv_event_t *e)
{
    int volume = lv_slider_get_value(s_volume_slider);
    lv_label_set_text_fmt(s_volume_value, "%d", volume);
    voice_set_volume(volume);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        settings_set_volume(volume);
        voice_preview();
    }
}

static void on_voice_preview(lv_event_t *e) { voice_preview(); }

static void voice_page_open(void)
{
    voice_controls_update();
    page_show(PAGE_VOICE);
}

static void voice_page_build(void)
{
    lv_obj_t *page = s_pages[PAGE_VOICE] = page_create();
    page_header(page, "음성 안내", PAGE_MENU);

    lv_obj_set_pos(label_create(page, "음성 안내", &lab_font_28), 36, 124);
    muted_label(page, "끄면 QR 안내·승인·거부 음성을 내지 않습니다. 관리자 실시간 보기 알림음은 그대로 울립니다.", 36, 164, 760);
    s_voice_switch = lv_switch_create(page);
    lv_obj_set_size(s_voice_switch, 120, 60);
    lv_obj_set_pos(s_voice_switch, 868, 124);
    lv_obj_set_style_bg_color(s_voice_switch, UI_BLUE, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_voice_switch, on_voice_switch, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_set_pos(label_create(page, "음성 종류 (누르면 바로 들려줍니다)", &lab_font_28), 36, 238);
    s_voice_buttons = lv_buttonmatrix_create(page);
    lv_buttonmatrix_set_map(s_voice_buttons, s_voice_map);
    lv_buttonmatrix_set_button_ctrl_all(s_voice_buttons, LV_BUTTONMATRIX_CTRL_CHECKABLE);
    lv_buttonmatrix_set_one_checked(s_voice_buttons, true);
    lv_obj_set_size(s_voice_buttons, LCD_H_RES - 72, 92);
    lv_obj_set_pos(s_voice_buttons, 36, 280);
    keypad_style(s_voice_buttons);
    lv_obj_set_style_text_font(s_voice_buttons, &lab_font_28, 0);
    lv_obj_set_style_bg_color(s_voice_buttons, UI_BLUE, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(s_voice_buttons, lv_color_white(), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_voice_buttons, on_voice_choice, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_set_pos(label_create(page, "볼륨", &lab_font_28), 36, 410);
    s_volume_slider = slider_create(page, 160, 424, 620, 0, 100, on_volume);
    s_volume_value = label_create(page, "", &lab_font_28);
    lv_obj_set_pos(s_volume_value, 804, 410);
    ui_button(page, "미리 듣기", 36, 488, 240, 72, true, on_voice_preview, 0);
}

// ---------- 화면 밝기·테마 ----------

static void theme_marks_update(void)
{
    for (int i = 0; i < SETTINGS_THEME_COUNT; i++) {
        lv_obj_set_style_border_width(s_theme_swatches[i], i == s_theme_choice ? 6 : 0, 0);
    }
    if (s_theme_choice == settings_theme()) lv_label_set_text(s_theme_note, "지금 쓰고 있는 테마입니다.");
    else lv_label_set_text_fmt(s_theme_note, "'%s' 테마는 단말을 다시 시작하면 적용됩니다.", s_theme_names[s_theme_choice]);
}

static void on_brightness(lv_event_t *e)
{
    int percent = lv_slider_get_value(s_bright_slider);
    lv_label_set_text_fmt(s_bright_value, "%d%%", percent);
    display_set_brightness(percent);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) settings_set_brightness(percent);
}

static void on_theme_pick(lv_event_t *e)
{
    s_theme_choice = (int)(intptr_t)lv_event_get_user_data(e);
    theme_marks_update();
}

static void restart_cb(lv_timer_t *timer) { esp_restart(); }

static void on_theme_apply(lv_event_t *e)
{
    if (s_theme_choice == settings_theme()) return;
    settings_set_theme(s_theme_choice);
    lv_label_set_text(s_theme_note, "테마를 저장했습니다. 단말을 다시 시작합니다...");
    ESP_LOGW(TAG, "테마 %d 적용을 위해 다시 시작", s_theme_choice);
    lv_timer_t *timer = lv_timer_create(restart_cb, 800, NULL);
    lv_timer_set_repeat_count(timer, 1);
}

static void display_page_open(void)
{
    lv_slider_set_value(s_bright_slider, settings_brightness(), LV_ANIM_OFF);
    lv_label_set_text_fmt(s_bright_value, "%d%%", settings_brightness());
    s_theme_choice = settings_theme();
    theme_marks_update();
    page_show(PAGE_DISPLAY);
}

static void display_page_build(void)
{
    lv_obj_t *page = s_pages[PAGE_DISPLAY] = page_create();
    page_header(page, "화면 밝기·테마", PAGE_MENU);

    lv_obj_set_pos(label_create(page, "밝기", &lab_font_28), 36, 128);
    s_bright_slider = slider_create(page, 160, 142, 620, SETTINGS_BRIGHT_MIN, 100, on_brightness);
    s_bright_value = label_create(page, "", &lab_font_28);
    lv_obj_set_pos(s_bright_value, 804, 128);

    lv_obj_set_pos(label_create(page, "테마 색상", &lab_font_28), 36, 214);
    for (int i = 0; i < SETTINGS_THEME_COUNT; i++) {
        int x = 36 + i * 194;
        lv_obj_t *swatch = ui_button(page, "", x, 260, 170, 110, false, on_theme_pick, i);
        lv_obj_set_style_bg_color(swatch, lv_color_hex(ui_themes[i].accent), 0);
        lv_obj_set_style_bg_color(swatch, lv_color_hex(ui_themes[i].ink), LV_STATE_PRESSED);
        lv_obj_set_style_border_color(swatch, lv_color_hex(0xffc83d), 0);   // 고른 테마 표시 (노란 테두리)
        lv_obj_t *name = ui_text(swatch, s_theme_names[i], &lab_font_28, 0, 0, 150, lv_color_white());
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(name);
        s_theme_swatches[i] = swatch;
    }
    s_theme_note = muted_label(page, "", 36, 396, 952);
    ui_button(page, "테마 적용 (다시 시작)", 36, 460, 400, 80, true, on_theme_apply, 0);
}

// ---------- 시작 ----------

void ui_settings_build(lv_obj_t *screen)
{
    s_root = lv_obj_create(screen);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, LCD_H_RES, LCD_V_RES);
    lv_obj_set_style_bg_color(s_root, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    pin_page_build();
    menu_page_build();
    wifi_page_build();
    scan_page_build();
    pass_page_build();
    door_page_build();
    pinset_page_build();
    voice_page_build();
    display_page_build();
}

void ui_settings_open(void)
{
    lv_label_set_text(s_pin_error, settings_admin_pin()[0] ? "" : "비밀번호가 설정되지 않았습니다");
    build_info_update();
    page_show(PAGE_PIN);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_HIDDEN);
}

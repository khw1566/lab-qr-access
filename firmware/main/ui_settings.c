// 설정 화면: 관리자 비밀번호 확인 후 Wi-Fi 검색과 연결
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lvgl.h"
#include "sdkconfig.h"
#include "app.h"
#include "ui_theme.h"

static const char *TAG = "ui_settings";

#define MAX_APS 20
#define LOCK_WAIT_MS 1000

static const char *const s_pin_map[] = {
    "1", "2", "3", "\n",
    "4", "5", "6", "\n",
    "7", "8", "9", "\n",
    "지움", "0", "확인", "",
};

static lv_obj_t *s_root, *s_pin_page, *s_wifi_page, *s_pass_page;
static lv_obj_t *s_pin_input, *s_pin_error, *s_wifi_current, *s_ap_list, *s_pass_title, *s_pass_input;
static wifi_ap_t s_aps[MAX_APS];
static int s_selected;
static volatile bool s_scanning;

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

static lv_obj_t *button_create(lv_obj_t *parent, const char *text, lv_event_cb_t cb)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, 150, 56);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_bg_color(button, UI_SOFT, 0);
    lv_obj_set_style_bg_color(button, UI_LINE, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = label_create(button, text, &lab_font_24);
    lv_obj_center(label);
    return button;
}

static void page_show(lv_obj_t *page)
{
    lv_obj_t *pages[] = {s_pin_page, s_wifi_page, s_pass_page};
    for (int i = 0; i < 3; i++) {
        if (pages[i] == page) lv_obj_remove_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void settings_close(void)
{
    lv_textarea_set_text(s_pin_input, "");
    lv_textarea_set_text(s_pass_input, "");
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    ui_settings_closed();
}

static void on_close(lv_event_t *e) { settings_close(); }

void ui_settings_close(void)
{
    if (!lv_obj_has_flag(s_root, LV_OBJ_FLAG_HIDDEN)) settings_close();
}

static void on_ap_click(lv_event_t *e);

static void ap_list_fill(int count)
{
    lv_obj_clean(s_ap_list);
    if (count <= 0) {
        lv_list_add_text(s_ap_list, count < 0 ? "검색에 실패했습니다" : "찾은 네트워크가 없습니다");
        return;
    }
    for (int i = 0; i < count; i++) {
        char text[64];
        snprintf(text, sizeof(text), "%s  (%d dBm%s)", s_aps[i].ssid, s_aps[i].rssi, s_aps[i].secured ? "" : ", 개방형");
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

static void wifi_page_open(void)
{
    char text[64];
    snprintf(text, sizeof(text), "현재: %s (%s)", wifi_current_ssid(), wifi_is_connected() ? "연결됨" : "연결 안 됨");
    lv_label_set_text(s_wifi_current, text);
    page_show(s_wifi_page);
    scan_start();
}

static void on_ap_click(lv_event_t *e)
{
    s_selected = (intptr_t)lv_event_get_user_data(e);
    if (!s_aps[s_selected].secured) {
        wifi_connect_to(s_aps[s_selected].ssid, "");
        settings_close();
        return;
    }
    char text[64];
    snprintf(text, sizeof(text), "%s 비밀번호", s_aps[s_selected].ssid);
    lv_label_set_text(s_pass_title, text);
    lv_textarea_set_text(s_pass_input, "");
    page_show(s_pass_page);
}

static void on_keyboard(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_READY) {
        wifi_connect_to(s_aps[s_selected].ssid, lv_textarea_get_text(s_pass_input));
        settings_close();
    } else if (lv_event_get_code(e) == LV_EVENT_CANCEL) {
        page_show(s_wifi_page);
    }
}

static void on_pin_key(lv_event_t *e)
{
    lv_obj_t *matrix = lv_event_get_target(e);
    const char *key = lv_buttonmatrix_get_button_text(matrix, lv_buttonmatrix_get_selected_button(matrix));
    if (!key) return;
    if (strcmp(key, "지움") == 0) {
        lv_textarea_delete_char(s_pin_input);
    } else if (strcmp(key, "확인") == 0) {
        bool ok = CONFIG_LAB_ADMIN_PIN[0] && strcmp(lv_textarea_get_text(s_pin_input), CONFIG_LAB_ADMIN_PIN) == 0;
        lv_textarea_set_text(s_pin_input, "");
        lv_label_set_text(s_pin_error, ok ? "" : "비밀번호가 맞지 않습니다");
        if (ok) wifi_page_open();
    } else {
        lv_label_set_text(s_pin_error, "");
        lv_textarea_add_text(s_pin_input, key);
    }
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

static void page_header(lv_obj_t *page, const char *title)
{
    ui_header(page, title);
    lv_obj_t *close = button_create(page, "닫기", on_close);
    lv_obj_set_pos(close, 838, 18);
}

static void pin_page_build(void)
{
    s_pin_page = page_create();
    page_header(s_pin_page, "관리자 설정");
    lv_obj_set_pos(label_create(s_pin_page, "관리자 비밀번호", &lab_font_40), 64, 156);
    ui_text(s_pin_page, "관리자 PIN을 입력해 주세요.", &lab_font_24, 64, 228, 480, UI_MUTED);
    s_pin_input = input_create(s_pin_page, 360);
    lv_textarea_set_max_length(s_pin_input, 16);
    lv_textarea_set_placeholder_text(s_pin_input, "");
    lv_obj_set_pos(s_pin_input, 64, 288);
    lv_obj_set_style_text_align(s_pin_input, LV_TEXT_ALIGN_CENTER, 0);
    s_pin_error = label_create(s_pin_page, "", &lab_font_24);
    lv_obj_set_pos(s_pin_error, 64, 376);
    lv_obj_set_width(s_pin_error, 480);
    lv_obj_set_style_text_color(s_pin_error, UI_RED, 0);

    lv_obj_t *matrix = lv_buttonmatrix_create(s_pin_page);
    lv_buttonmatrix_set_map(matrix, s_pin_map);
    lv_obj_set_size(matrix, 360, 320);
    lv_obj_set_pos(matrix, 600, 148);
    keypad_style(matrix);
    lv_obj_set_style_text_font(matrix, &lab_font_24, 0);
    lv_obj_add_event_cb(matrix, on_pin_key, LV_EVENT_VALUE_CHANGED, NULL);

    ui_text(s_pin_page, "네트워크 설정은 관리자만 변경할 수 있습니다", &lab_font_24, 64, 536, 900, UI_MUTED);
}

static void wifi_page_build(void)
{
    s_wifi_page = page_create();
    page_header(s_wifi_page, "Wi-Fi 설정");
    s_wifi_current = label_create(s_wifi_page, "", &lab_font_24);
    lv_obj_set_pos(s_wifi_current, 36, 118);
    lv_obj_set_width(s_wifi_current, 760);
    lv_label_set_long_mode(s_wifi_current, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_wifi_current, UI_MUTED, 0);
    lv_obj_set_pos(button_create(s_wifi_page, "다시 검색", on_rescan), 672, 18);

    s_ap_list = lv_list_create(s_wifi_page);
    lv_obj_set_size(s_ap_list, LCD_H_RES - 72, 398);
    lv_obj_set_pos(s_ap_list, 36, 178);
    lv_obj_set_style_bg_color(s_ap_list, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_ap_list, &lab_font_24, 0);
    lv_obj_set_style_text_color(s_ap_list, UI_INK, 0);
    lv_obj_set_style_border_color(s_ap_list, UI_LINE, 0);
    lv_obj_set_style_radius(s_ap_list, 8, 0);
}

static void pass_page_build(void)
{
    s_pass_page = page_create();
    page_header(s_pass_page, "Wi-Fi 연결");
    s_pass_title = label_create(s_pass_page, "", &lab_font_24);
    lv_obj_set_pos(s_pass_title, 36, 108);
    lv_obj_set_width(s_pass_title, 952);
    lv_label_set_long_mode(s_pass_title, LV_LABEL_LONG_DOT);
    s_pass_input = input_create(s_pass_page, LCD_H_RES - 72);
    lv_textarea_set_max_length(s_pass_input, 64);
    lv_obj_set_pos(s_pass_input, 36, 152);

    lv_obj_t *keyboard = lv_keyboard_create(s_pass_page);
    lv_obj_set_size(keyboard, LCD_H_RES, 360);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(keyboard, &lv_font_montserrat_24, 0);
    keypad_style(keyboard);
    lv_keyboard_set_textarea(keyboard, s_pass_input);
    lv_obj_add_event_cb(keyboard, on_keyboard, LV_EVENT_ALL, NULL);
}

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
    wifi_page_build();
    pass_page_build();
}

void ui_settings_open(void)
{
    lv_label_set_text(s_pin_error, CONFIG_LAB_ADMIN_PIN[0] ? "" : "비밀번호가 설정되지 않았습니다");
    page_show(s_pin_page);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_HIDDEN);
}

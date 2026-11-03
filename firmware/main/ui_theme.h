#pragma once
#include "lvgl.h"
#include "panel_init.h"
#include "ui_icons.h"

// C안: 파란 안내 바, 흰 바탕, 남색 글자. 크기는 1024×600 기준이다.
#define UI_BLUE       lv_color_hex(0x2447db)
#define UI_INK        lv_color_hex(0x142979)
#define UI_MUTED      lv_color_hex(0x4e6080)
#define UI_SOFT       lv_color_hex(0xedf1ff)
#define UI_LINE       lv_color_hex(0xcbd6ef)
#define UI_GREEN      lv_color_hex(0x08764d)
#define UI_RED        lv_color_hex(0xb73734)
#define UI_AMBER      lv_color_hex(0x955c13)
#define UI_BAR_H      92

extern const lv_font_t lab_font_24, lab_font_28, lab_font_40, lab_font_48, lab_font_56, lab_font_64;

static inline lv_obj_t *ui_panel(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    return obj;
}

static inline lv_obj_t *ui_text(lv_obj_t *parent, const char *text, const lv_font_t *font,
                                int x, int y, int width, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_width(label, width);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    return label;
}

static inline lv_obj_t *ui_button(lv_obj_t *parent, const char *text, int x, int y,
                                  int width, int height, bool primary,
                                  lv_event_cb_t cb, intptr_t data)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_bg_color(button, primary ? UI_BLUE : UI_SOFT, 0);
    lv_obj_set_style_bg_color(button, primary ? UI_INK : UI_LINE, LV_STATE_PRESSED);
    lv_obj_set_style_outline_color(button, UI_BLUE, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_width(button, 3, LV_STATE_FOCUS_KEY);
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, (void *)data);
    if (!text[0]) return button;
    lv_obj_t *label = ui_text(button, text, &lab_font_28, 0, 0, width - 16,
                              primary ? lv_color_white() : UI_INK);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
    return button;
}

static inline lv_obj_t *ui_icon_button(lv_obj_t *parent, ui_icon_t icon, const char *text,
                                       int x, int y, int width, int height, bool primary,
                                       lv_event_cb_t cb, intptr_t data)
{
    lv_obj_t *button = ui_button(parent, "", x, y, width, height, primary, cb, data);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_style_bg_opa(button, primary ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
    int icon_size = primary ? 44 : 28;
    int icon_x = text[0] ? (primary ? 64 : 12) : (width - icon_size) / 2;
    ui_icon(button, icon, icon_x, (height - icon_size) / 2, icon_size,
             primary ? lv_color_white() : UI_MUTED);
    if (text[0]) {
        lv_obj_t *label = ui_text(button, text, primary ? &lab_font_28 : &lab_font_24,
                                  icon_x + icon_size + 14, 0,
                                  width - icon_x - icon_size - 20,
                                  primary ? lv_color_white() : UI_MUTED);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, icon_x + icon_size + 14, 0);
    }
    return button;
}

static inline void ui_header(lv_obj_t *parent, const char *title)
{
    lv_obj_t *bar = ui_panel(parent, 0, 0, LCD_H_RES, UI_BAR_H);
    lv_obj_set_style_bg_color(bar, UI_BLUE, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    for (int i = 0; i < 4; i++) {
        lv_obj_t *tile = ui_panel(bar, 36 + (i % 2) * 18, 29 + (i / 2) * 18, 14, 14);
        lv_obj_set_style_bg_color(tile, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(tile, i == 3 ? LV_OPA_50 : LV_OPA_COVER, 0);
        lv_obj_set_style_radius(tile, 3, 0);
    }
    ui_text(bar, title, &lab_font_28, 82, 27, 500, lv_color_white());
}

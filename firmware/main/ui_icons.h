#pragma once
#include "lvgl.h"
#include "ui_assets.h"

// PC에서 렌더링한 이미지. 공통 128px 원본으로 부드러운 외곽선을 유지한다.
typedef enum {
    UI_ICON_QR, UI_ICON_HOME, UI_ICON_SETTINGS, UI_ICON_INFO, UI_ICON_PHONE,
    UI_ICON_USER, UI_ICON_CALENDAR, UI_ICON_CHECK, UI_ICON_CLOSE,
    UI_ICON_WIFI, UI_ICON_WIFI_OFF, UI_ICON_RIGHT, UI_ICON_FINGERPRINT
} ui_icon_t;

static inline lv_obj_t *ui_icon(lv_obj_t *parent, ui_icon_t icon, int x, int y, int size, lv_color_t color)
{
    lv_obj_t *obj = lv_image_create(parent);
    lv_image_set_src(obj, ui_icon_images[icon]);
    lv_image_set_inner_align(obj, LV_IMAGE_ALIGN_STRETCH);
    lv_image_set_antialias(obj, true);
    lv_obj_set_size(obj, size, size);
    lv_obj_set_pos(obj, x, y);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_color(obj, color, 0);
    lv_obj_set_style_image_recolor(obj, color, 0);
    lv_obj_set_style_image_recolor_opa(obj, LV_OPA_COVER, 0);
    return obj;
}

static inline void ui_icon_set(lv_obj_t *obj, ui_icon_t icon, lv_color_t color)
{
    lv_image_set_src(obj, ui_icon_images[icon]);
    lv_obj_set_style_image_recolor(obj, color, 0);
    lv_obj_invalidate(obj);
}

static inline void ui_icon_color(lv_obj_t *obj, lv_color_t color)
{
    lv_obj_set_style_image_recolor(obj, color, 0);
}

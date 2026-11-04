#pragma once
#include "lvgl.h"
#include "ui_assets.h"
#include "esp_log.h"

// PC에서 실제 표시 크기로 변환한 A8 이미지. LVGL에서 확대·축소하지 않는다.
typedef enum {
    UI_ICON_QR, UI_ICON_HOME, UI_ICON_SETTINGS, UI_ICON_INFO, UI_ICON_PHONE,
    UI_ICON_USER, UI_ICON_CALENDAR, UI_ICON_CHECK, UI_ICON_CLOSE,
    UI_ICON_WIFI, UI_ICON_WIFI_OFF, UI_ICON_RIGHT, UI_ICON_FINGERPRINT, UI_ICON_KEYBOARD
} ui_icon_t;

static inline lv_obj_t *ui_icon(lv_obj_t *parent, ui_icon_t icon, int x, int y, int size, lv_color_t color)
{
    lv_obj_t *obj = lv_image_create(parent);
    const lv_image_dsc_t *source = ui_icon_asset(icon, size);
    lv_image_set_src(obj, source);
    lv_image_set_inner_align(obj, LV_IMAGE_ALIGN_TOP_LEFT);
    lv_image_set_scale(obj, LV_SCALE_NONE);
    lv_obj_set_size(obj, source->header.w, source->header.h);
    lv_obj_set_user_data(obj, (void *)(intptr_t)icon);
    lv_obj_set_pos(obj, x, y);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_color(obj, color, 0);
    lv_obj_set_style_image_recolor(obj, color, 0);
    lv_obj_set_style_image_recolor_opa(obj, LV_OPA_COVER, 0);
    return obj;
}

static inline void ui_icon_set(lv_obj_t *obj, ui_icon_t icon, lv_color_t color)
{
    const lv_image_dsc_t *current = lv_image_get_src(obj);
    const lv_image_dsc_t *source = ui_icon_asset(icon, current->header.w);
    lv_image_set_src(obj, source);
    lv_obj_set_size(obj, source->header.w, source->header.h);
    lv_obj_set_user_data(obj, (void *)(intptr_t)icon);
    lv_obj_set_style_image_recolor(obj, color, 0);
    lv_obj_invalidate(obj);
}

static inline void ui_icon_color(lv_obj_t *obj, lv_color_t color)
{
    lv_obj_set_style_image_recolor(obj, color, 0);
}

static inline void ui_icon_resize(lv_obj_t *obj, int size)
{
    ui_icon_t icon = (ui_icon_t)(intptr_t)lv_obj_get_user_data(obj);
    const lv_image_dsc_t *source = ui_icon_asset(icon, size);
    lv_image_set_src(obj, source);
    lv_obj_set_size(obj, source->header.w, source->header.h);
}

static inline unsigned ui_icons_validate(lv_obj_t *obj)
{
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return 0;
    unsigned count = 0;
    if (lv_obj_check_type(obj, &lv_image_class)) {
        const lv_image_dsc_t *source = lv_image_get_src(obj);
        if (source && lv_image_src_get_type(source) == LV_IMAGE_SRC_VARIABLE && source->header.cf == LV_COLOR_FORMAT_A8) {
            bool valid = lv_obj_get_width(obj) == source->header.w &&
                         lv_obj_get_height(obj) == source->header.h &&
                         lv_image_get_scale_x(obj) == LV_SCALE_NONE &&
                         lv_image_get_scale_y(obj) == LV_SCALE_NONE;
            if (!valid) ESP_LOGE("ui", "Icon geometry mismatch: source %ux%u, widget %ldx%ld",
                                 (unsigned)source->header.w, (unsigned)source->header.h,
                                 (long)lv_obj_get_width(obj), (long)lv_obj_get_height(obj));
            else count++;
        }
    }
    for (unsigned i = 0; i < lv_obj_get_child_count(obj); i++) count += ui_icons_validate(lv_obj_get_child(obj, i));
    return count;
}

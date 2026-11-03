#pragma once
#include "lvgl.h"

// 32단위 좌표를 실제 크기로 그린다. 폰트 기호나 이미지 확대에 의존하지 않는다.
typedef enum {
    UI_ICON_QR, UI_ICON_HOME, UI_ICON_SETTINGS, UI_ICON_INFO, UI_ICON_PHONE,
    UI_ICON_USER, UI_ICON_CALENDAR, UI_ICON_CHECK, UI_ICON_CLOSE,
    UI_ICON_WIFI, UI_ICON_WIFI_OFF, UI_ICON_RIGHT
} ui_icon_t;

typedef struct { int8_t x, y; } ui_icon_point_t;

static inline void ui_icon_path(lv_layer_t *layer, const lv_area_t *box, int size,
                                 lv_color_t color, const ui_icon_point_t *points, unsigned count)
{
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = color;
    line.width = (size + 15) / 16;
    line.round_start = true;
    line.round_end = true;
    for (unsigned i = 1; i < count; i++) {
        line.p1.x = box->x1 + points[i - 1].x * size / 32;
        line.p1.y = box->y1 + points[i - 1].y * size / 32;
        line.p2.x = box->x1 + points[i].x * size / 32;
        line.p2.y = box->y1 + points[i].y * size / 32;
        lv_draw_line(layer, &line);
    }
}

static inline void ui_icon_ring(lv_layer_t *layer, const lv_area_t *box, int size,
                                 lv_color_t color, int x, int y, int w, int h, bool circle)
{
    lv_area_t area = {box->x1 + x * size / 32, box->y1 + y * size / 32,
                      box->x1 + (x + w) * size / 32, box->y1 + (y + h) * size / 32};
    lv_draw_rect_dsc_t rect;
    lv_draw_rect_dsc_init(&rect);
    rect.bg_opa = LV_OPA_TRANSP;
    rect.border_opa = LV_OPA_COVER;
    rect.border_color = color;
    rect.border_width = (size + 15) / 16;
    rect.radius = circle ? LV_RADIUS_CIRCLE : size / 24;
    lv_draw_rect(layer, &rect, &area);
}

static inline void ui_icon_draw(lv_event_t *event)
{
    lv_obj_t *obj = lv_event_get_target(event);
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t box;
    lv_obj_get_coords(obj, &box);
    int size = lv_obj_get_width(obj);
    lv_color_t color = lv_obj_get_style_text_color(obj, 0);
    ui_icon_t icon = (ui_icon_t)(intptr_t)lv_obj_get_user_data(obj);
#define ICON_PATH(...) do { const ui_icon_point_t points[] = {__VA_ARGS__}; \
    ui_icon_path(layer, &box, size, color, points, sizeof(points) / sizeof(points[0])); } while (0)
#define ICON_RING(x,y,w,h,circle) ui_icon_ring(layer, &box, size, color, x,y,w,h,circle)
    switch (icon) {
    case UI_ICON_QR:
        ICON_RING(4,4,9,9,false); ICON_RING(19,4,9,9,false); ICON_RING(4,19,9,9,false);
        ICON_PATH({20,19},{20,23},{25,23},{25,28},{28,28});
        ICON_PATH({28,18},{28,20}); ICON_PATH({19,28},{21,28});
        break;
    case UI_ICON_HOME:
        ICON_PATH({3,14},{16,4},{29,14});
        ICON_PATH({7,12},{7,28},{13,28},{13,19},{19,19},{19,28},{25,28},{25,12});
        break;
    case UI_ICON_SETTINGS:
        ICON_PATH({13,3},{19,3},{20,7},{24,9},{28,8},{31,13},{28,16},{28,20},
                  {29,24},{24,29},{20,27},{16,28},{13,30},{8,27},{8,23},{5,20},
                  {2,19},{2,13},{6,11},{8,8},{8,4},{13,3});
        ICON_RING(11,11,10,10,true);
        break;
    case UI_ICON_INFO:
        ICON_RING(3,3,26,26,true);
        ICON_PATH({16,15},{16,23}); ICON_PATH({16,9},{16,10});
        break;
    case UI_ICON_PHONE:
        ICON_RING(7,2,18,28,false);
        ICON_PATH({13,6},{19,6}); ICON_PATH({14,26},{18,26});
        ICON_RING(11,11,5,5,false); ICON_RING(19,11,3,3,false);
        ICON_PATH({12,20},{15,20}); ICON_PATH({20,18},{20,21});
        break;
    case UI_ICON_USER:
        ICON_RING(11,3,10,10,true);
        ICON_PATH({4,28},{5,23},{9,19},{16,17},{23,19},{27,23},{28,28});
        break;
    case UI_ICON_CALENDAR:
        ICON_RING(4,7,24,22,false);
        ICON_PATH({10,3},{10,10}); ICON_PATH({22,3},{22,10});
        ICON_PATH({5,14},{27,14}); ICON_PATH({11,21},{15,25},{22,18});
        break;
    case UI_ICON_CHECK:
        ICON_RING(2,2,28,28,true);
        ICON_PATH({9,16},{14,21},{24,11});
        break;
    case UI_ICON_CLOSE:
        ICON_RING(2,2,28,28,true);
        ICON_PATH({11,11},{21,21}); ICON_PATH({21,11},{11,21});
        break;
    case UI_ICON_WIFI:
    case UI_ICON_WIFI_OFF:
        ICON_PATH({3,11},{7,8},{12,6},{20,6},{25,8},{29,11});
        ICON_PATH({8,17},{12,14},{20,14},{24,17});
        ICON_PATH({13,23},{16,21},{19,23}); ICON_PATH({16,27},{16,28});
        if (icon == UI_ICON_WIFI_OFF) { ICON_PATH({4,3},{28,29}); }
        break;
    case UI_ICON_RIGHT:
        ICON_PATH({12,6},{22,16},{12,26});
        break;
    }
#undef ICON_RING
#undef ICON_PATH
}

static inline lv_obj_t *ui_icon(lv_obj_t *parent, ui_icon_t icon, int x, int y, int size, lv_color_t color)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, size, size);
    lv_obj_set_style_text_color(obj, color, 0);
    lv_obj_set_user_data(obj, (void *)(intptr_t)icon);
    lv_obj_add_event_cb(obj, ui_icon_draw, LV_EVENT_DRAW_MAIN, NULL);
    return obj;
}

static inline void ui_icon_set(lv_obj_t *obj, ui_icon_t icon, lv_color_t color)
{
    lv_obj_set_user_data(obj, (void *)(intptr_t)icon);
    lv_obj_set_style_text_color(obj, color, 0);
    lv_obj_invalidate(obj);
}

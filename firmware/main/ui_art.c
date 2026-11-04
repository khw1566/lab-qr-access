// 원본 로고는 고정하고, 윤곽선이 바깥으로 퍼지며 작동 중임을 표시한다.
#include "ui_art.h"
#include "logo_contours.h"

#define ART_SIZE 332
#define RIPPLE_FRAMES 100
#define RIPPLE_COUNT 3

extern const uint8_t logo_art_bytes[] asm("_binary_logo_art_a8_start");
static const lv_image_dsc_t logo_art = {
    .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8,
               .w = ART_SIZE, .h = ART_SIZE, .stride = ART_SIZE},
    .data_size = ART_SIZE * ART_SIZE,
    .data = logo_art_bytes,
};
static lv_obj_t *s_art;
static unsigned s_frame;

static void draw_ripples(lv_event_t *event)
{
    lv_obj_t *obj = lv_event_get_target(event);
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t box;
    lv_obj_get_coords(obj, &box);
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = lv_color_hex(0x005767);
    line.width = 1;
    line.round_start = true;
    line.round_end = true;
    for (unsigned wave = 0; wave < RIPPLE_COUNT; wave++) {
        unsigned age = (s_frame + wave * RIPPLE_FRAMES / RIPPLE_COUNT) % RIPPLE_FRAMES;
        unsigned radius = age * LOGO_RIPPLE_DISTANCE / (RIPPLE_FRAMES - 1);
        // Fade in at the logo edge, then fade completely before restarting there.
        unsigned fade_in = age < 10 ? age * 10 : 100;
        line.opa = (lv_opa_t)(100 * fade_in * (RIPPLE_FRAMES - 1 - age) / (100 * (RIPPLE_FRAMES - 1)));
        if (!line.opa) continue;
        logo_contour_t contour = logo_contours[radius];
        for (unsigned i = 0; i < contour.count; i++) {
            lv_point_t a = logo_points[contour.offset + i];
            lv_point_t b = logo_points[contour.offset + (i + 1) % contour.count];
            line.p1 = (lv_point_precise_t){box.x1 + a.x, box.y1 + a.y};
            line.p2 = (lv_point_precise_t){box.x1 + b.x, box.y1 + b.y};
            lv_draw_line(layer, &line);
        }
    }
}

lv_obj_t *ui_art_create(lv_obj_t *parent, int x, int y, int size)
{
    (void)size;
    s_frame = 0;
    s_art = lv_obj_create(parent);
    lv_obj_remove_style_all(s_art);
    lv_obj_set_size(s_art, ART_SIZE, ART_SIZE);
    lv_obj_set_pos(s_art, x, y);
    lv_obj_remove_flag(s_art, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_art, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_art, draw_ripples, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_t *logo = lv_image_create(s_art);
    lv_image_set_src(logo, &logo_art);
    lv_image_set_inner_align(logo, LV_IMAGE_ALIGN_TOP_LEFT);
    lv_image_set_scale(logo, LV_SCALE_NONE);
    lv_obj_set_size(logo, ART_SIZE, ART_SIZE);
    lv_obj_set_style_image_recolor(logo, lv_color_hex(0x005767), 0);
    lv_obj_set_style_image_recolor_opa(logo, LV_OPA_COVER, 0);
    lv_obj_remove_flag(logo, LV_OBJ_FLAG_CLICKABLE);
    return s_art;
}

void ui_art_step(void)
{
    if (!s_art) return;
    s_frame = (s_frame + 1) % RIPPLE_FRAMES;
    lv_obj_invalidate(s_art);
}

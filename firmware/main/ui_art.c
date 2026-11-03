// Processing 스타일의 생성형 아트: 파란 토러스 매듭이 회전하며 호흡한다.
// 고정 이미지나 프레임 배열 없이 매 프레임 수학 곡선을 계산한다.
#include <math.h>
#include "ui_art.h"

#define ART_STRANDS 7
#define ART_SAMPLES 80
#define TAU 6.28318530718f

static lv_obj_t *s_art;
static float s_phase;

typedef struct { float x, y, z; } art_point_t;

static art_point_t knot(float t, int strand)
{
    float band = (strand - (ART_STRANDS - 1) * 0.5f) * 0.055f;
    float tube = 0.48f + band + 0.05f * sinf(s_phase * 0.8f + t * 4.0f);
    float radius = 1.30f + tube * cosf(3.0f * t + s_phase * 0.35f);
    float x = radius * cosf(2.0f * t);
    float y = radius * sinf(2.0f * t);
    float z = tube * sinf(3.0f * t + s_phase * 0.35f);
    float yaw = s_phase * 0.42f;
    float tilt = 0.65f + 0.22f * sinf(s_phase * 0.31f);
    float xx = x * cosf(yaw) + z * sinf(yaw);
    float zz = -x * sinf(yaw) + z * cosf(yaw);
    art_point_t p = {xx, y * cosf(tilt) - zz * sinf(tilt), y * sinf(tilt) + zz * cosf(tilt)};
    return p;
}

static lv_point_precise_t project(art_point_t p, const lv_area_t *box, int size)
{
    float perspective = 4.8f / (4.8f + p.z);
    float scale = size * 0.205f * perspective;
    lv_point_precise_t out = {box->x1 + size * 0.5f + p.x * scale,
                              box->y1 + size * 0.5f + p.y * scale};
    return out;
}

static void draw_art(lv_event_t *event)
{
    lv_obj_t *obj = lv_event_get_target(event);
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t box;
    lv_obj_get_coords(obj, &box);
    int size = lv_obj_get_width(obj);
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.round_start = true;
    line.round_end = true;
    // 뒤쪽의 옅은 선부터 그려 교차 부분의 깊이가 드러나게 한다.
    for (int pass = 0; pass < 2; pass++) {
        for (int strand = 0; strand < ART_STRANDS; strand++) {
            art_point_t previous = knot(0, strand);
            for (int i = 1; i <= ART_SAMPLES; i++) {
                art_point_t current = knot(TAU * i / ART_SAMPLES, strand);
                float depth = (previous.z + current.z) * 0.5f;
                if ((depth < 0) == (pass == 1)) {
                    float shade = (1.0f - depth / 2.2f) * 0.5f;
                    if (shade < 0.12f) shade = 0.12f;
                    if (shade > 0.92f) shade = 0.92f;
                    line.color = lv_color_mix(lv_color_hex(0x0070c0), lv_color_white(), (uint8_t)(shade * 255));
                    line.opa = LV_OPA_COVER;
                    line.width = depth < 0 ? 2 : 1;
                    line.p1 = project(previous, &box, size);
                    line.p2 = project(current, &box, size);
                    lv_draw_line(layer, &line);
                }
                previous = current;
            }
        }
    }
    // 매듭 위를 천천히 흐르는 점이 방향과 연속적인 움직임을 드러낸다.
    for (int i = 0; i < 7; i++) {
        art_point_t p = knot(s_phase * 0.45f + TAU * i / 7, ART_STRANDS / 2);
        lv_point_precise_t at = project(p, &box, size);
        lv_draw_rect_dsc_t dot;
        lv_draw_rect_dsc_init(&dot);
        dot.bg_color = lv_color_hex(0x0070c0);
        dot.bg_opa = p.z < 0 ? LV_OPA_COVER : LV_OPA_50;
        dot.radius = LV_RADIUS_CIRCLE;
        int radius = p.z < 0 ? 3 : 2;
        lv_area_t area = {(int)at.x - radius, (int)at.y - radius, (int)at.x + radius, (int)at.y + radius};
        lv_draw_rect(layer, &dot, &area);
    }
}

lv_obj_t *ui_art_create(lv_obj_t *parent, int x, int y, int size)
{
    s_art = lv_obj_create(parent);
    lv_obj_remove_style_all(s_art);
    lv_obj_remove_flag(s_art, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_art, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(s_art, x, y);
    lv_obj_set_size(s_art, size, size);
    lv_obj_add_event_cb(s_art, draw_art, LV_EVENT_DRAW_MAIN, NULL);
    return s_art;
}

void ui_art_step(void)
{
    if (!s_art) return;
    s_phase += 0.035f;
    if (s_phase > TAU * 100) s_phase -= TAU * 100;
    lv_obj_invalidate(s_art);
}

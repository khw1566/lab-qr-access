// 헤드리스 LVGL 시뮬레이터: 펌웨어 ui.c / ui_settings.c를 그대로 그려 PPM으로 저장한다.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "app.h"
#include "src/libs/qrcode/qrcodegen.h"

extern int sim_theme;
void sim_tab(int tab);
void sim_code_text(const char *t, const char *err);
void sim_settings_from_home(void);
void sim_settings_page(int p, const char *input);
void ui_art_step(void);

static uint16_t fb[1024 * 600];
static uint8_t draw_buf[1024 * 600 * 2];
static const char *outdir;

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    int w = lv_area_get_width(a);
    for (int y = a->y1; y <= a->y2; y++)
        memcpy(&fb[y * 1024 + a->x1], px + (size_t)(y - a->y1) * w * 2, w * 2);
    lv_display_flush_ready(d);
}

static void run(int ms)
{
    for (int t = 0; t < ms; t += 5) { lv_tick_inc(5); lv_timer_handler(); }
}

static void shot(const char *name)
{
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(NULL);
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.ppm", outdir, name);
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n1024 600\n255\n");
    for (int i = 0; i < 1024 * 600; i++) {
        uint16_t c = fb[i];
        uint8_t rgb[3] = { (uint8_t)(((c >> 11) & 31) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63), (uint8_t)((c & 31) * 255 / 31) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("%s\n", name);
}

static inline uint16_t rgb565(int r, int g, int b) { return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3); }

// 카메라 미리보기 대신: 휴대폰 화면에 띄운 출입증 QR
static void fake_camera(void)
{
    uint8_t *buf = ui_preview_buffer();
    if (!buf) return;
    uint16_t *px = (uint16_t *)buf;
    int W = UI_PREVIEW_W, H = UI_PREVIEW_H;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int v = 70 + (x + y) * 50 / (W + H);
            px[y * W + x] = rgb565(v + 10, v + 6, v);
        }
    int pw = 470, ph = 640, px0 = (W - pw) / 2 + 10, py0 = -50;
    for (int y = py0; y < py0 + ph; y++) for (int x = px0; x < px0 + pw; x++)
        if (y >= 0 && y < H) px[y * W + x] = rgb565(25, 25, 30);
    int sx = px0 + 14, sy = py0 + 30, sw = pw - 28, sh = ph - 60;
    for (int y = sy; y < sy + sh; y++) for (int x = sx; x < sx + sw; x++)
        if (y >= 0 && y < H) px[y * W + x] = rgb565(236, 238, 242);
    uint8_t qr[qrcodegen_BUFFER_LEN_MAX], tmp[qrcodegen_BUFFER_LEN_MAX];
    qrcodegen_encodeText("LA1.1042.5867321.k3J9xQ2mVb7tR0", tmp, qr, qrcodegen_Ecc_MEDIUM, 1, 40, qrcodegen_Mask_AUTO, true);
    int n = qrcodegen_getSize(qr), quiet = 2, mod = 400 / (n + 2 * quiet);
    int qs = mod * (n + 2 * quiet), qx = sx + (sw - qs) / 2, qy = (H - qs) / 2;
    for (int y = 0; y < qs; y++) for (int x = 0; x < qs; x++) {
        int mx = x / mod - quiet, my = y / mod - quiet;
        bool dark = mx >= 0 && my >= 0 && mx < n && my < n && qrcodegen_getModule(qr, mx, my);
        px[(qy + y) * W + qx + x] = dark ? rgb565(20, 20, 24) : rgb565(250, 250, 250);
    }
    ui_preview_commit();
}

int main(int argc, char **argv)
{
    outdir = argc > 1 ? argv[1] : "out";
    sim_theme = argc > 2 ? atoi(argv[2]) : 0;
    bool themes_only = argc > 2;
    lv_init();
    lv_display_t *d = lv_display_create(1024, 600);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(d, draw_buf, NULL, sizeof(draw_buf), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(d, flush_cb);

    ui_build();
    ui_clock_set(1795051800000LL);   // 2026-11-19 10:30 KST
    ui_weather_set(2, true, 12.4f);
    ui_server_status(UI_SERVER_ONLINE);
    ui_status("Wi-Fi: UOU-Lab");
    for (int i = 0; i < 28; i++) ui_art_step();
    run(100);
    char name[64];
    if (themes_only) {
        snprintf(name, sizeof(name), "theme%d-home", sim_theme); shot(name);
        sim_settings_page(1, NULL); run(500);
        snprintf(name, sizeof(name), "theme%d-settings-menu", sim_theme); shot(name);
        return 0;
    }
    shot("01-home");
    sim_tab(1); fake_camera(); run(500); shot("02-qr-scan");
    ui_busy("확인 중"); run(450); shot("03-checking");
    ui_result(true, "출입 승인"); run(500); shot("04-granted");
    ui_result(false, "출입증을 확인하세요"); run(500); shot("05-denied");
    ui_result(false, "서버에 연결할 수 없습니다"); run(500); shot("06-offline");
    ui_remote_open(); run(500); shot("07-remote-open");
    sim_tab(2); run(500); shot("08-guide");
    sim_tab(3); sim_code_text("482193", ""); run(500); shot("09-code-entry");
    sim_code_text("", "코드를 확인하고 다시 입력하세요"); run(500); shot("10-code-error");
    sim_tab(1); fake_camera(); ui_live(true, true); run(500); shot("11-live-view");
    ui_live(false, false);
    ui_camera_failed(); run(500); shot("12-camera-error");
    sim_tab(0);
    ui_server_status(UI_SERVER_OFFLINE); ui_status("disconnected"); ui_weather_set(-1, true, 0); run(500); shot("13-home-offline-header");
    ui_server_status(UI_SERVER_ONLINE); ui_status("Wi-Fi: UOU-Lab"); ui_weather_set(2, true, 12.4f);
    sim_settings_page(0, "5738"); run(500); shot("20-settings-pin");
    sim_settings_page(1, NULL); run(500); shot("21-settings-menu");
    sim_settings_page(2, NULL); run(500); shot("22-settings-wifi");
    sim_settings_page(3, NULL); run(500); shot("23-settings-wifi-scan");
    sim_settings_page(4, "gigawifi"); run(500); shot("24-settings-wifi-password");
    sim_settings_page(5, NULL); run(500); shot("25-settings-door");
    sim_settings_page(6, "1234"); run(500); shot("26-settings-pinset");
    sim_settings_page(7, NULL); run(500); shot("27-settings-voice");
    sim_settings_page(8, NULL); run(500); shot("28-settings-display");
    return 0;
}

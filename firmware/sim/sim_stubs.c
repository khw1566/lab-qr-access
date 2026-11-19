// 펌웨어 하드웨어 함수 대신 화면 캡처에 쓸 고정 값
#include <string.h>
#include <stdio.h>
#include "app.h"
#include "esp_app_desc.h"
int64_t esp_timer_get_time(void) { return 1000000; }
static esp_app_desc_t s_app = { "70ee6a1", "Nov 19 2026", "10:30:00" };
const esp_app_desc_t *esp_app_get_description(void) { return &s_app; }
bool display_lock(uint32_t t) { (void)t; return true; }
void display_unlock(void) {}
void display_set_brightness(int p) { (void)p; }
void voice_play(voice_clip_t c) { (void)c; }
void voice_preview(void) {}
void voice_set_volume(int p) { (void)p; }
void door_open(int ms) { (void)ms; }
bool access_submit_code(const char *c) { (void)c; return true; }
int sim_theme = 0, sim_voice_id = 0;
bool sim_pin_custom = false, sim_voice_on = true;
const char *settings_admin_pin(void) { return "0000"; }
bool settings_admin_pin_custom(void) { return sim_pin_custom; }
bool settings_set_admin_pin(const char *p) { (void)p; return true; }
int settings_door_ms(void) { return 15000; }
void settings_set_door_ms(int ms) { (void)ms; }
bool settings_voice_on(void) { return sim_voice_on; }
void settings_set_voice_on(bool on) { sim_voice_on = on; }
int settings_voice_id(void) { return sim_voice_id; }
void settings_set_voice_id(int id) { sim_voice_id = id; }
int settings_volume(void) { return 70; }
void settings_set_volume(int v) { (void)v; }
int settings_brightness(void) { return 80; }
void settings_set_brightness(int p) { (void)p; }
int settings_theme(void) { return sim_theme; }
void settings_set_theme(int t) { (void)t; }
bool wifi_is_connected(void) { return true; }
const char *wifi_current_ssid(void) { return "UOU-Lab"; }
int wifi_scan(wifi_ap_t *out, int max) { (void)out; (void)max; return 0; }
void wifi_connect_to(const char *s, const char *p) { (void)s; (void)p; }
static const char *s_saved_names[] = { "UOU-Lab", "UOU-WiFi", "Lab-Guest" };
int wifi_saved_list(char ssids[][33], int max) {
    int n = 3 < max ? 3 : max;
    for (int i = 0; i < n; i++) strncpy(ssids[i], s_saved_names[i], 33);
    return n;
}
bool wifi_saved_connect(const char *s) { (void)s; return false; }
void wifi_saved_forget(const char *s) { (void)s; }

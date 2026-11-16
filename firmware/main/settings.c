// 단말 설정값 (NVS "lab-set"에 저장). 펌웨어를 다시 구워도 지워지지 않는다.
// 저장된 값이 없으면 menuconfig(sdkconfig) 값을 기본으로 쓴다.
#include <string.h>
#include "esp_log.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "app.h"

static const char *TAG = "settings";
#define NVS_SPACE "lab-set"

static struct {
    char pin[SETTINGS_PIN_MAX + 1];   // 비어 있으면 CONFIG_LAB_ADMIN_PIN
    int door_ms;
    bool voice_on;
    int voice_id;
    int volume;
    int brightness;
    int theme;
} s = {
    .door_ms = CONFIG_LAB_DOOR_MIN_OPEN_MS,
    .voice_on = true,
    .voice_id = 0,
    .volume = 80,
    .brightness = 80,
    .theme = 0,
};

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void load_int(nvs_handle_t nvs, const char *key, int *value, int lo, int hi)
{
    int32_t v;
    if (nvs_get_i32(nvs, key, &v) == ESP_OK) *value = clamp((int)v, lo, hi);
}

void settings_load(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_SPACE, NVS_READONLY, &nvs) != ESP_OK) return;   // 처음 켠 단말: 기본값
    size_t len = sizeof(s.pin);
    if (nvs_get_str(nvs, "pin", s.pin, &len) != ESP_OK) s.pin[0] = 0;
    int voice_on = s.voice_on;
    load_int(nvs, "door_ms", &s.door_ms, SETTINGS_DOOR_MIN_MS, SETTINGS_DOOR_MAX_MS);
    load_int(nvs, "voice_on", &voice_on, 0, 1);
    load_int(nvs, "voice_id", &s.voice_id, 0, VOICE_COUNT - 1);
    load_int(nvs, "volume", &s.volume, 0, 100);
    load_int(nvs, "bright", &s.brightness, SETTINGS_BRIGHT_MIN, 100);
    load_int(nvs, "theme", &s.theme, 0, SETTINGS_THEME_COUNT - 1);
    s.voice_on = voice_on;
    nvs_close(nvs);
    ESP_LOGI(TAG, "문 %d ms, 음성 %s(%d) 볼륨 %d, 밝기 %d, 테마 %d, 비밀번호 %s", s.door_ms, s.voice_on ? "켬" : "끔",
             s.voice_id, s.volume, s.brightness, s.theme, s.pin[0] ? "변경됨" : "기본값");
}

static void save_int(const char *key, int value)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_SPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_set_i32(nvs, key, value);
        if (err == ESP_OK) err = nvs_commit(nvs);
        nvs_close(nvs);
    }
    if (err != ESP_OK) ESP_LOGE(TAG, "%s 저장 실패: %s", key, esp_err_to_name(err));
}

const char *settings_admin_pin(void) { return s.pin[0] ? s.pin : CONFIG_LAB_ADMIN_PIN; }
bool settings_admin_pin_custom(void) { return s.pin[0] != 0; }

bool settings_set_admin_pin(const char *pin)
{
    size_t n = pin ? strlen(pin) : 0;
    if (n && (n < SETTINGS_PIN_MIN || n > SETTINGS_PIN_MAX || strspn(pin, "0123456789") != n)) return false;
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_SPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = n ? nvs_set_str(nvs, "pin", pin) : nvs_erase_key(nvs, "pin");
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
        if (err == ESP_OK) err = nvs_commit(nvs);
        nvs_close(nvs);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "비밀번호 저장 실패: %s", esp_err_to_name(err));
        return false;
    }
    strlcpy(s.pin, n ? pin : "", sizeof(s.pin));
    ESP_LOGW(TAG, "단말 비밀번호 %s", n ? "변경" : "기본값으로 되돌림");
    return true;
}

int settings_door_ms(void) { return s.door_ms; }
void settings_set_door_ms(int ms)
{
    s.door_ms = clamp(ms, SETTINGS_DOOR_MIN_MS, SETTINGS_DOOR_MAX_MS);
    save_int("door_ms", s.door_ms);
}

bool settings_voice_on(void) { return s.voice_on; }
void settings_set_voice_on(bool on)
{
    s.voice_on = on;
    save_int("voice_on", on);
}

int settings_voice_id(void) { return s.voice_id; }
void settings_set_voice_id(int id)
{
    s.voice_id = clamp(id, 0, VOICE_COUNT - 1);
    save_int("voice_id", s.voice_id);
}

int settings_volume(void) { return s.volume; }
void settings_set_volume(int volume)
{
    s.volume = clamp(volume, 0, 100);
    save_int("volume", s.volume);
}

int settings_brightness(void) { return s.brightness; }
void settings_set_brightness(int percent)
{
    s.brightness = clamp(percent, SETTINGS_BRIGHT_MIN, 100);
    save_int("bright", s.brightness);
}

int settings_theme(void) { return s.theme; }
void settings_set_theme(int theme)
{
    s.theme = clamp(theme, 0, SETTINGS_THEME_COUNT - 1);
    save_int("theme", s.theme);
}

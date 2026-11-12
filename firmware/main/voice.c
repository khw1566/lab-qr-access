// 음성: ES8311 코덱 하나로
//  - 안내 음성: 펌웨어에 넣은 PCM(16 kHz mono, tools/gen_voice.py로 생성)을 스피커로 재생
//  - 실시간 보기 말하기: 관리자가 보낸 8 kHz PCM을 받아 스피커로 재생 (live.c)
//  - 실시간 보기 소리 듣기: 마이크를 8 kHz PCM으로 읽어 live.c가 서버로 보낸다 (저장하지 않음)
// 마이크 입력 핀(CONFIG_LAB_AUDIO_DIN_GPIO)이 -1이면 스피커만 쓴다.
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "app.h"

static const char *TAG = "voice";

#define RATE        16000
#define CHUNK       512     // 한 번에 쓰는 mono 샘플 수
#define VOLUME      80
#define MIC_GAIN_DB 30.0f
#define TALK_BUF    (16000)             // 말하기 수신 버퍼 (8 kHz s16 기준 약 1초)
#define TALK_HOLD_MS 300                // 말하기 재생 직후 이 시간 동안은 마이크를 보내지 않는다 (울림 방지)
#define VOICE_CHIME ((voice_clip_t)100)  // 실시간 연결 알림음 (펌웨어에서 합성)

#ifndef CONFIG_LAB_AUDIO_DIN_GPIO
#define CONFIG_LAB_AUDIO_DIN_GPIO -1
#endif

#define CLIP(name) \
    extern const uint8_t name##_start[] asm("_binary_" #name "_pcm_start"); \
    extern const uint8_t name##_end[] asm("_binary_" #name "_pcm_end");
CLIP(scan)
CLIP(checking)
CLIP(granted)
CLIP(denied)
CLIP(offline)

static const uint8_t *const s_clips[][2] = {
    [VOICE_SCAN] = {scan_start, scan_end},
    [VOICE_CHECKING] = {checking_start, checking_end},
    [VOICE_GRANTED] = {granted_start, granted_end},
    [VOICE_DENIED] = {denied_start, denied_end},
    [VOICE_OFFLINE] = {offline_start, offline_end},
};

static QueueHandle_t s_queue;
static StreamBufferHandle_t s_talk;
static esp_codec_dev_handle_t s_codec;
static bool s_has_mic;
static volatile int64_t s_talk_at;

static esp_err_t codec_start(void)
{
    i2s_chan_handle_t tx, rx = NULL;
    bool want_mic = CONFIG_LAB_AUDIO_DIN_GPIO >= 0;
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel, &tx, want_mic ? &rx : NULL), TAG, "I2S 채널");
    i2s_std_config_t config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = CONFIG_LAB_AUDIO_MCLK_GPIO,
            .bclk = CONFIG_LAB_AUDIO_BCLK_GPIO,
            .ws = CONFIG_LAB_AUDIO_WS_GPIO,
            .dout = CONFIG_LAB_AUDIO_DOUT_GPIO,
            .din = want_mic ? CONFIG_LAB_AUDIO_DIN_GPIO : I2S_GPIO_UNUSED,
        },
    };
    config.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx, &config), TAG, "I2S 설정");
    if (rx) ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(rx, &config), TAG, "I2S 입력 설정");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(tx), TAG, "I2S 시작");
    if (rx) ESP_RETURN_ON_ERROR(i2s_channel_enable(rx), TAG, "I2S 입력 시작");

    audio_codec_i2s_cfg_t data = { .port = I2S_NUM_0, .tx_handle = tx, .rx_handle = rx };
    audio_codec_i2c_cfg_t control = { .port = I2C_NUM_0, .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = board_i2c_bus() };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&data);
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&control);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    ESP_RETURN_ON_FALSE(data_if && ctrl_if && gpio_if, ESP_ERR_NO_MEM, TAG, "코덱 인터페이스");

    es8311_codec_cfg_t chip = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = rx ? ESP_CODEC_DEV_WORK_MODE_BOTH : ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = CONFIG_LAB_AUDIO_AMP_GPIO,
        .use_mclk = true,
        .mclk_div = 256,
        .hw_gain = { .pa_voltage = 5.0f, .codec_dac_voltage = 3.3f },
    };
    const audio_codec_if_t *chip_if = es8311_codec_new(&chip);
    ESP_RETURN_ON_FALSE(chip_if, ESP_FAIL, TAG, "ES8311을 찾지 못함");
    esp_codec_dev_cfg_t device = {
        .dev_type = rx ? ESP_CODEC_DEV_TYPE_IN_OUT : ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = chip_if,
        .data_if = data_if,
    };
    s_codec = esp_codec_dev_new(&device);
    ESP_RETURN_ON_FALSE(s_codec, ESP_ERR_NO_MEM, TAG, "코덱 장치");
    esp_codec_dev_sample_info_t format = { .sample_rate = RATE, .channel = 2, .bits_per_sample = 16 };
    ESP_RETURN_ON_FALSE(esp_codec_dev_open(s_codec, &format) == ESP_CODEC_DEV_OK, ESP_FAIL, TAG, "코덱 열기");
    esp_codec_dev_set_out_vol(s_codec, VOLUME);
    if (rx) {
        esp_codec_dev_set_in_gain(s_codec, MIC_GAIN_DB);
        s_has_mic = true;
    }
    return ESP_OK;
}

static void write_mono(const int16_t *pcm, size_t n)
{
    static int16_t stereo[CHUNK * 2];
    for (size_t at = 0; at < n; at += CHUNK) {
        size_t k = n - at < CHUNK ? n - at : CHUNK;
        for (size_t i = 0; i < k; i++) stereo[i * 2] = stereo[i * 2 + 1] = pcm[at + i];
        esp_codec_dev_write(s_codec, stereo, k * 4);
    }
}

// 실시간 연결 알림음: 딩(880 Hz) 동(660 Hz)
static void play_chime(void)
{
    static int16_t tone[CHUNK];
    const int freqs[2] = { 880, 660 };
    for (int t = 0; t < 2; t++) {
        int total = RATE * 30 / 100;   // 0.3초씩
        for (int at = 0; at < total; at += CHUNK) {
            int k = total - at < CHUNK ? total - at : CHUNK;
            for (int i = 0; i < k; i++) {
                float env = 1.0f - (float)(at + i) / total;
                tone[i] = (int16_t)(sinf(2.0f * (float)M_PI * freqs[t] * (at + i) / RATE) * 9000.0f * env);
            }
            write_mono(tone, k);
        }
    }
}

// 8 kHz → 16 kHz 로 늘려(선형 보간) 재생
static void play_talk(void)
{
    static int16_t in[CHUNK / 2], out[CHUNK];
    static int16_t prev;
    size_t got = xStreamBufferReceive(s_talk, in, sizeof(in), 0);
    size_t n = got / 2;
    if (!n) return;
    for (size_t i = 0; i < n; i++) {
        out[i * 2] = (int16_t)((prev + in[i]) / 2);
        out[i * 2 + 1] = in[i];
        prev = in[i];
    }
    s_talk_at = esp_timer_get_time();
    write_mono(out, n * 2);
}

static void voice_task(void *arg)
{
    voice_clip_t clip;
    for (;;) {
        // 말하기 소리가 들어와 있으면 짧게만 기다려 바로 재생한다
        TickType_t wait = (s_talk && xStreamBufferBytesAvailable(s_talk)) ? 0 : pdMS_TO_TICKS(20);
        if (xQueueReceive(s_queue, &clip, wait) == pdTRUE) {
            if (clip == VOICE_CHIME) { play_chime(); continue; }
            const int16_t *pcm = (const int16_t *)s_clips[clip][0];
            size_t total = (s_clips[clip][1] - s_clips[clip][0]) / 2;
            for (size_t at = 0; at < total; at += CHUNK) {
                // 새 안내가 들어오면 지금 것은 끊고 새 것을 재생한다
                if (uxQueueMessagesWaiting(s_queue)) break;
                write_mono(pcm + at, total - at < CHUNK ? total - at : CHUNK);
            }
            continue;
        }
        if (s_talk) play_talk();
    }
}

esp_err_t voice_start(void)
{
    ESP_RETURN_ON_ERROR(codec_start(), TAG, "스피커");
    s_queue = xQueueCreate(1, sizeof(voice_clip_t));
    s_talk = xStreamBufferCreate(TALK_BUF, 2);
    ESP_RETURN_ON_FALSE(s_queue && s_talk, ESP_ERR_NO_MEM, TAG, "큐");
    ESP_RETURN_ON_FALSE(xTaskCreate(voice_task, "voice", 4096, NULL, 5, NULL) == pdPASS, ESP_ERR_NO_MEM, TAG, "태스크");
    ESP_LOGI(TAG, "ES8311 준비 (스피커%s)", s_has_mic ? " + 마이크" : "");
    return ESP_OK;
}

void voice_play(voice_clip_t clip)
{
    if (s_queue) xQueueOverwrite(s_queue, &clip);
}

void voice_chime(void)
{
    voice_play(VOICE_CHIME);
}

bool voice_mic_available(void)
{
    return s_has_mic;
}

void voice_talk_write(const uint8_t *pcm8k, size_t bytes)
{
    if (!s_talk) return;
    bytes &= ~(size_t)1;
    // 밀려 있으면(네트워크가 몰아서 보냄) 오래된 소리를 버리고 지연이 쌓이지 않게 한다
    if (xStreamBufferSpacesAvailable(s_talk) < bytes) xStreamBufferReset(s_talk);
    xStreamBufferSend(s_talk, pcm8k, bytes, 0);
}

void voice_talk_flush(void)
{
    if (s_talk) xStreamBufferReset(s_talk);
}

bool voice_talk_active(void)
{
    return esp_timer_get_time() - s_talk_at < TALK_HOLD_MS * 1000LL;
}

// 마이크에서 8 kHz mono를 n개 읽는다. ES8311 ADC는 한쪽 채널에만 실릴 수 있어 블록마다 더 큰 쪽을 쓴다.
int voice_mic_read(int16_t *out, int n)
{
    if (!s_has_mic) return -1;
    static int16_t stereo[CHUNK * 2];
    int done = 0;
    while (done < n) {
        int pairs = (n - done) * 2;          // 16 kHz 샘플 수
        if (pairs > CHUNK) pairs = CHUNK;
        if (esp_codec_dev_read(s_codec, stereo, pairs * 4) != ESP_CODEC_DEV_OK) return -1;
        int64_t el = 0, er = 0;
        for (int i = 0; i < pairs; i++) { el += abs(stereo[i * 2]); er += abs(stereo[i * 2 + 1]); }
        int ch = er > el ? 1 : 0;
        for (int i = 0; i + 1 < pairs; i += 2) out[done++] = (int16_t)((stereo[i * 2 + ch] + stereo[(i + 1) * 2 + ch]) / 2);
    }
    return done;
}

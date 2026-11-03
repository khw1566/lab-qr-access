// 음성 안내: 펌웨어에 넣은 PCM(16 kHz mono, tools/gen_voice.py로 생성)을 ES8311 스피커로 재생한다.
#include <string.h>
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "app.h"

static const char *TAG = "voice";

#define RATE        16000
#define CHUNK       512     // 한 번에 쓰는 mono 샘플 수
#define VOLUME      80

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
static esp_codec_dev_handle_t s_codec;

static esp_err_t codec_start(void)
{
    i2s_chan_handle_t tx;
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel, &tx, NULL), TAG, "I2S 채널");
    i2s_std_config_t config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = CONFIG_LAB_AUDIO_MCLK_GPIO,
            .bclk = CONFIG_LAB_AUDIO_BCLK_GPIO,
            .ws = CONFIG_LAB_AUDIO_WS_GPIO,
            .dout = CONFIG_LAB_AUDIO_DOUT_GPIO,
            .din = I2S_GPIO_UNUSED,
        },
    };
    config.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx, &config), TAG, "I2S 설정");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(tx), TAG, "I2S 시작");

    audio_codec_i2s_cfg_t data = { .port = I2S_NUM_0, .tx_handle = tx };
    audio_codec_i2c_cfg_t control = { .port = I2C_NUM_0, .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = board_i2c_bus() };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&data);
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&control);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    ESP_RETURN_ON_FALSE(data_if && ctrl_if && gpio_if, ESP_ERR_NO_MEM, TAG, "코덱 인터페이스");

    es8311_codec_cfg_t chip = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = CONFIG_LAB_AUDIO_AMP_GPIO,
        .use_mclk = true,
        .mclk_div = 256,
        .hw_gain = { .pa_voltage = 5.0f, .codec_dac_voltage = 3.3f },
    };
    const audio_codec_if_t *chip_if = es8311_codec_new(&chip);
    ESP_RETURN_ON_FALSE(chip_if, ESP_FAIL, TAG, "ES8311을 찾지 못함");
    esp_codec_dev_cfg_t device = { .dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = chip_if, .data_if = data_if };
    s_codec = esp_codec_dev_new(&device);
    ESP_RETURN_ON_FALSE(s_codec, ESP_ERR_NO_MEM, TAG, "코덱 장치");
    esp_codec_dev_sample_info_t format = { .sample_rate = RATE, .channel = 2, .bits_per_sample = 16 };
    ESP_RETURN_ON_FALSE(esp_codec_dev_open(s_codec, &format) == ESP_CODEC_DEV_OK, ESP_FAIL, TAG, "코덱 열기");
    esp_codec_dev_set_out_vol(s_codec, VOLUME);
    return ESP_OK;
}

static void voice_task(void *arg)
{
    static int16_t stereo[CHUNK * 2];
    voice_clip_t clip;
    for (;;) {
        if (xQueueReceive(s_queue, &clip, portMAX_DELAY) != pdTRUE) continue;
        const int16_t *pcm = (const int16_t *)s_clips[clip][0];
        size_t total = (s_clips[clip][1] - s_clips[clip][0]) / 2;
        for (size_t at = 0; at < total; at += CHUNK) {
            // 새 안내가 들어오면 지금 것은 끊고 새 것을 재생한다
            if (uxQueueMessagesWaiting(s_queue)) break;
            size_t n = total - at < CHUNK ? total - at : CHUNK;
            for (size_t i = 0; i < n; i++) stereo[i * 2] = stereo[i * 2 + 1] = pcm[at + i];
            esp_codec_dev_write(s_codec, stereo, n * 4);
        }
    }
}

esp_err_t voice_start(void)
{
    ESP_RETURN_ON_ERROR(codec_start(), TAG, "스피커");
    s_queue = xQueueCreate(1, sizeof(voice_clip_t));
    ESP_RETURN_ON_FALSE(s_queue, ESP_ERR_NO_MEM, TAG, "큐");
    ESP_RETURN_ON_FALSE(xTaskCreate(voice_task, "voice", 4096, NULL, 5, NULL) == pdPASS, ESP_ERR_NO_MEM, TAG, "태스크");
    ESP_LOGI(TAG, "ES8311 스피커 준비");
    return ESP_OK;
}

void voice_play(voice_clip_t clip)
{
    if (s_queue) xQueueOverwrite(s_queue, &clip);
}

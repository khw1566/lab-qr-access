// 7인치 MIPI-DSI LCD와 GT911 터치를 켜고 LVGL을 돌린다. 화면 구성은 ui.c가 맡는다.
#include <stdio.h>
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "sdkconfig.h"
#include "app.h"
#include "panel_init.h"

static const char *TAG = "display";

#define DSI_LANES       2
#define DSI_LANE_MBPS   750
#define DSI_PHY_LDO     3
#define DSI_PHY_LDO_MV  2500

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_touch_handle_t s_touch;
static SemaphoreHandle_t s_lock, s_frame_swapped;

// DPI DMA가 새 버퍼로 넘어간 뒤 호출된다. 이후 이전 버퍼에 그려도 화면이 찢어지지 않는다.
static bool IRAM_ATTR on_frame_done(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *e, void *arg)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_frame_swapped, &woken);
    return woken == pdTRUE;
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *pixels)
{
    esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, pixels);
    if (err == ESP_OK) {
        xSemaphoreTake(s_frame_swapped, 0);
        if (xSemaphoreTake(s_frame_swapped, pdMS_TO_TICKS(100)) != pdTRUE) ESP_LOGW(TAG, "프레임 전환 시간 초과");
    } else {
        ESP_LOGE(TAG, "화면 갱신 실패: %s", esp_err_to_name(err));
    }
    lv_display_flush_ready(disp);
}

static void touch_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    data->state = LV_INDEV_STATE_RELEASED;
    if (!s_touch || esp_lcd_touch_read_data(s_touch) != ESP_OK) return;
    esp_lcd_touch_point_data_t point[1];
    uint8_t count = 0;
    if (esp_lcd_touch_get_data(s_touch, point, &count, 1) == ESP_OK && count) {
        data->point.x = point[0].x;
        data->point.y = point[0].y;
        data->state = LV_INDEV_STATE_PRESSED;
    }
}

static esp_err_t touch_start(void)
{
    i2c_master_bus_handle_t bus = board_i2c_bus();
    uint8_t address = 0;
    const uint8_t candidates[] = {ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP};
    for (int i = 0; i < 2 && !address; i++) {
        if (i2c_master_probe(bus, candidates[i], 50) == ESP_OK) address = candidates[i];
    }
    ESP_RETURN_ON_FALSE(address, ESP_ERR_NOT_FOUND, TAG, "GT911을 찾지 못함");

    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_i2c_config_t io_cfg = {
        .dev_addr = address,
        .control_phase_bytes = 1,
        .lcd_cmd_bits = 16,
        .flags.disable_control_phase = 1,
        .scl_speed_hz = 400000,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c_v2(bus, &io_cfg, &io), TAG, "터치 IO");

    static esp_lcd_touch_io_gt911_config_t gt911;
    gt911.dev_addr = address;
    esp_lcd_touch_config_t cfg = {
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = GPIO_NUM_NC,
        .driver_data = &gt911,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_gt911(io, &cfg, &s_touch), TAG, "GT911");
    ESP_LOGI(TAG, "GT911 터치 0x%02X", address);
    return ESP_OK;
}

static uint32_t tick_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void lvgl_task(void *arg)
{
    for (;;) {
        uint32_t wait = 30;
        if (xSemaphoreTakeRecursive(s_lock, portMAX_DELAY) == pdTRUE) {
            wait = lv_timer_handler();
            xSemaphoreGiveRecursive(s_lock);
        }
        if (wait < 5) wait = 5;
        if (wait > 30) wait = 30;
        vTaskDelay(pdMS_TO_TICKS(wait));
    }
}

static esp_err_t backlight_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 20000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "백라이트 타이머");
    ledc_channel_config_t channel = {
        .gpio_num = CONFIG_LAB_LCD_BACKLIGHT_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
    };
    return ledc_channel_config(&channel);
}

static void backlight_set(int percent)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, (1023 * percent) / 100);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static esp_err_t panel_start(void)
{
    esp_ldo_channel_handle_t ldo = NULL;
    esp_ldo_channel_config_t ldo_cfg = { .chan_id = DSI_PHY_LDO, .voltage_mv = DSI_PHY_LDO_MV };
    esp_err_t err = esp_ldo_acquire_channel(&ldo_cfg, &ldo);
    if (err != ESP_OK) ESP_LOGW(TAG, "DSI PHY LDO: %s (계속 진행)", esp_err_to_name(err));

    esp_lcd_dsi_bus_handle_t dsi;
    esp_lcd_dsi_bus_config_t bus_cfg = {
        .bus_id = 0,
        .num_data_lanes = DSI_LANES,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = DSI_LANE_MBPS,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_cfg, &dsi), TAG, "DSI 버스");

    esp_lcd_panel_io_handle_t dbi;
    esp_lcd_dbi_io_config_t dbi_cfg = { .virtual_channel = 0, .lcd_cmd_bits = 8, .lcd_param_bits = 8 };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(dsi, &dbi_cfg, &dbi), TAG, "DBI IO");

    esp_lcd_dpi_panel_config_t dpi = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = panel_pclk_mhz,
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .num_fbs = 2,
        .video_timing = panel_timing,
    };
    jd9165_vendor_config_t vendor = {
        .init_cmds = panel_init_cmds,
        .init_cmds_size = panel_init_cmd_count,
        .mipi_config = { .dsi_bus = dsi, .dpi_config = &dpi },
    };
    esp_lcd_panel_dev_config_t dev = {
        .reset_gpio_num = CONFIG_LAB_LCD_RESET_GPIO,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_jd9165(dbi, &dev, &s_panel), TAG, "JD9165");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "패널 리셋");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "패널 초기화");
    ESP_LOGI(TAG, "JD9165 패널 시작, %.0f MHz", panel_pclk_mhz);
    return ESP_OK;
}

esp_err_t display_start(void)
{
    ESP_RETURN_ON_ERROR(backlight_init(), TAG, "백라이트");
    ESP_RETURN_ON_ERROR(panel_start(), TAG, "패널");

    s_lock = xSemaphoreCreateRecursiveMutex();
    s_frame_swapped = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_lock && s_frame_swapped, ESP_ERR_NO_MEM, TAG, "세마포어");

    lv_init();
    lv_tick_set_cb(tick_ms);
    lv_display_t *display = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    void *fb0 = NULL, *fb1 = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_get_frame_buffer(s_panel, 2, &fb0, &fb1), TAG, "프레임버퍼");
    esp_lcd_dpi_panel_event_callbacks_t callbacks = { .on_frame_buf_complete = on_frame_done };
    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_register_event_callbacks(s_panel, &callbacks, NULL), TAG, "DPI 콜백");
    lv_display_set_buffers(display, fb0, fb1, LCD_H_RES * LCD_V_RES * sizeof(uint16_t), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, flush_cb);

    esp_err_t err = touch_start();
    if (err != ESP_OK) ESP_LOGE(TAG, "터치 사용 불가: %s", esp_err_to_name(err));
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read);
    lv_indev_set_display(indev, display);

    ui_build();

    // QR 스캔이 코어 1을 쓰므로 화면 갱신은 코어 0에 둔다
    if (xTaskCreatePinnedToCore(lvgl_task, "lvgl", 8192, NULL, 4, NULL, 0) != pdPASS) return ESP_ERR_NO_MEM;
    backlight_set(80);
    return ESP_OK;
}

bool display_lock(uint32_t timeout_ms)
{
    return s_lock && xSemaphoreTakeRecursive(s_lock, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void display_unlock(void) { xSemaphoreGiveRecursive(s_lock); }

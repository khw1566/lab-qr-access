#pragma once
// JC1060P470C V2 패널(JD9165) 초기화 표와 타이밍
#include <stddef.h>
#include "esp_lcd_jd9165.h"
#include "esp_lcd_mipi_dsi.h"

#define LCD_H_RES 1024
#define LCD_V_RES 600

extern const esp_lcd_video_timing_t panel_timing;
extern const float panel_pclk_mhz;
extern const jd9165_lcd_init_cmd_t panel_init_cmds[];
extern const size_t panel_init_cmd_count;

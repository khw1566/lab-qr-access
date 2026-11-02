// 보드 공용 I2C 버스 (GPIO7=SDA, GPIO8=SCL)
// 터치(GT911), 내장 카메라 SCCB, SH1.0 4P I2C 단자(릴레이)가 같은 버스를 쓴다.
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "app.h"

static i2c_master_bus_handle_t s_bus;

i2c_master_bus_handle_t board_i2c_bus(void)
{
    if (!s_bus) {
        i2c_master_bus_config_t cfg = {
            .i2c_port = 0,
            .sda_io_num = CONFIG_LAB_I2C_SDA,
            .scl_io_num = CONFIG_LAB_I2C_SCL,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &s_bus));
    }
    return s_bus;
}

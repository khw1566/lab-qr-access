#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

// board.c
i2c_master_bus_handle_t board_i2c_bus(void);

// wifi.c
esp_err_t wifi_start_and_wait(void);
bool wifi_is_connected(void);

// door.c
void door_init(void);
void door_open(int ms);
void door_signal(bool granted);

// server.c
typedef struct {
    bool ok;          // 서버가 출입 허용
    bool reachable;   // 서버 응답 여부
    int open_ms;
    char reason[32];
} verify_result_t;
verify_result_t server_verify(const char *token);
void server_ping(void);

// camera_qr.c
typedef void (*qr_callback_t)(const char *payload);
esp_err_t camera_qr_start(qr_callback_t cb);

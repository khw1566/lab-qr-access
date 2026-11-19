#pragma once
#include <stdio.h>
#define ESP_LOGI(t, ...) do{ (void)(t); }while(0)
#define ESP_LOGW(t, ...) do{ (void)(t); }while(0)
#define ESP_LOGE(t, ...) do{ fprintf(stderr, "E %s: ", t); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr);}while(0)

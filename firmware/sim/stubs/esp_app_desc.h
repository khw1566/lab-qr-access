#pragma once
typedef struct { char version[32]; char date[16]; char time[16]; } esp_app_desc_t;
const esp_app_desc_t *esp_app_get_description(void);

#pragma once
static inline int xTaskCreate(void (*f)(void*), const char *n, int s, void *a, int p, void *h){(void)f;(void)n;(void)s;(void)a;(void)p;(void)h;return 0;}
static inline void vTaskDelete(void *t){(void)t;}

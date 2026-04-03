#pragma once
#include <stdio.h>

/* tag is a const char* variable, not a string literal — pass as %s argument */
#define ESP_LOGI(tag, fmt, ...) printf("[I][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...)
#define ESP_LOGW(tag, fmt, ...) printf("[W][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) fprintf(stderr, "[E][%s] " fmt "\n", tag, ##__VA_ARGS__)

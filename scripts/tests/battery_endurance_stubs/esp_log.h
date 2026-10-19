#pragma once
#include <cstdio>
#define ESP_LOGE(tag, ...) do { (void)(tag); std::fprintf(stderr, __VA_ARGS__); std::fputc('\n', stderr); } while (0)
#define ESP_LOGW(tag, ...) ESP_LOGE(tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) ESP_LOGE(tag, __VA_ARGS__)

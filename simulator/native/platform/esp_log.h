#pragma once
#include <cstdarg>
#include <cstdio>

// tag: Original firmware component name.
// format: Original firmware message format.
// ...: Original arguments, including real operation failure causes.
inline void cody_host_log(const char *tag, const char *format, ...)
{
    std::fprintf(stderr, "%s: ", tag);
    va_list arguments;
    va_start(arguments, format);
    std::vfprintf(stderr, format, arguments);
    va_end(arguments);
    std::fputc('\n', stderr);
}
#define ESP_LOGE(tag, ...) cody_host_log(tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) cody_host_log(tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) cody_host_log(tag, __VA_ARGS__)

#pragma once
#include <cstdarg>
#include <cstdio>

// tag: Firmware component reporting a rejected input.
// format: Original format string from the portable firmware source.
// ...: Original log arguments; values are consumed so warnings remain meaningful.
inline void host_error_log(const char *tag, const char *format, ...)
{
    std::fprintf(stderr, "%s: ", tag);
    va_list arguments;
    va_start(arguments, format);
    std::vfprintf(stderr, format, arguments);
    va_end(arguments);
    std::fputc('\n', stderr);
}
#define ESP_LOGE(tag, ...) host_error_log(tag, __VA_ARGS__)

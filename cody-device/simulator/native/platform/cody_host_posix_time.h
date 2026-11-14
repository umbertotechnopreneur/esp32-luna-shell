#pragma once
#if defined(_WIN32)
#include <time.h>
#include <stddef.h>
#include <string.h>
// POSIX argument order and return convention over the thread-safe Windows CRT.
inline struct tm *gmtime_r(const time_t *value, struct tm *result)
{
    return value && result && gmtime_s(result, value) == 0 ? result : nullptr;
}

// ESP-IDF provides strlcpy; expose the same operation to unmodified code on Windows.
inline size_t strlcpy(char *destination, const char *source, size_t capacity)
{
    const size_t length = strlen(source);
    if (capacity != 0) {
        const size_t count = length < capacity - 1 ? length : capacity - 1;
        memcpy(destination, source, count);
        destination[count] = '\0';
    }
    return length;
}
#endif

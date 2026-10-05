#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include "asset_registry.h"
#include "mem_alloc.h"
#define PACKAGE_VERSION "1.52-dev"
#define PACKAGE_STRING "eSpeak NG 1.52-dev"
#define VERSION "1.52-dev"
#define ESPEAKNG_DEFAULT_VOICE "en"
#define PATH_ESPEAK_DATA "/luna-tts/espeak-ng-data"
#define USE_ASYNC 0
#define USE_LIBPCAUDIO 0
#define USE_MBROLA 0
#define USE_KLATT 0
#define USE_LIBSONIC 0
#define ESPEAK_HEAP_HACK 1
// Use a bounded large worker stack instead of assert-based transient heap hacks.
#define ESPEAK_STACK_HACK 0
#define STACK_T
#define ESPEAK_ARDUINO_POSIX_FS 0
#define HAVE_ENDIAN_H 0
#define HAVE_GETOPT_H 0
#define HAVE_MKSTEMP 0
#define HAVE_INTTYPES_H 1
#define HAVE_WCHAR_H 1
#define HAVE_WCTYPE_H 1
#define le16toh(value) (value)
#define le32toh(value) (value)
#define fopen luna_espeak_fopen
#define fclose luna_espeak_fclose
#define strdup luna_espeak_strdup
#define ESPEAK_LOGGING 1
#define ESPK_LOG(...) printf(__VA_ARGS__)

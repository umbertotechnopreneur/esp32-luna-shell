#pragma once
#include "sdkconfig.h"
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif
#if CONFIG_LUNASHELL_TTS_DATA_SD
typedef struct {
    const char *relative_path;
    uint8_t *destination;
    size_t size;
    const uint8_t *sha256;
} lunashell_tts_storage_file_t;
// files: Exact package files and caller-owned buffers retained until this call returns.
// file_count: Bounded count of manifest-owned files, never arbitrary directory traversal.
// verify_only: Hash the current card without mutating already borrowed engine data.
// io_errno: Receives the original filesystem cause when applicable.
// Exception: Returns absent card, lifecycle, exact read or integrity failures.
esp_err_t LUNASHELL_storage_load_tts(lunashell_tts_storage_file_t *files,
                                    size_t file_count, bool verify_only, int *io_errno);
#endif
#ifdef __cplusplus
}
#endif

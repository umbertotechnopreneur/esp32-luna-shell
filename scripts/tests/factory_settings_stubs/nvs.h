/* VBWR B
 *
 * Project: ESP32 Luna Shell
 * Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
 * Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
 *
 * VibeWare: Human intent, AI execution, and plenty of tokens
 * Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
 *
 * Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
 * Human guidance: Umberto Giacobbi; requested VibeWare branding.
 *
 * Copyright (c) 2026 Umberto Giacobbi
 *
 * VBWR E */

#pragma once
#include <cstddef>
#include <cstdint>
#include "esp_err.h"

// Host doubles model only the dedicated settings handle; partition erasure is absent.
using nvs_handle_t = uint32_t;
using nvs_iterator_t = void *;
enum nvs_type_t { NVS_TYPE_ANY = 0xff };
enum nvs_open_mode_t { NVS_READWRITE = 1 };
constexpr size_t NVS_KEY_NAME_MAX_SIZE = 16;
constexpr esp_err_t ESP_ERR_NVS_NOT_FOUND = 0x1102;
constexpr esp_err_t ESP_ERR_NVS_INVALID_NAME = 0x1106;
constexpr esp_err_t ESP_ERR_NVS_KEY_TOO_LONG = 0x1109;

esp_err_t nvs_open(const char *, nvs_open_mode_t, nvs_handle_t *);
void nvs_close(nvs_handle_t);
esp_err_t nvs_set_u8(nvs_handle_t, const char *, uint8_t);
esp_err_t nvs_get_u8(nvs_handle_t, const char *, uint8_t *);
esp_err_t nvs_set_i32(nvs_handle_t, const char *, int32_t);
esp_err_t nvs_get_i32(nvs_handle_t, const char *, int32_t *);
esp_err_t nvs_set_u32(nvs_handle_t, const char *, uint32_t);
esp_err_t nvs_get_u32(nvs_handle_t, const char *, uint32_t *);
esp_err_t nvs_set_i64(nvs_handle_t, const char *, int64_t);
esp_err_t nvs_get_i64(nvs_handle_t, const char *, int64_t *);
esp_err_t nvs_set_u64(nvs_handle_t, const char *, uint64_t);
esp_err_t nvs_get_u64(nvs_handle_t, const char *, uint64_t *);
esp_err_t nvs_set_str(nvs_handle_t, const char *, const char *);
esp_err_t nvs_get_str(nvs_handle_t, const char *, char *, size_t *);
esp_err_t nvs_set_blob(nvs_handle_t, const char *, const void *, size_t);
esp_err_t nvs_get_blob(nvs_handle_t, const char *, void *, size_t *);
esp_err_t nvs_erase_key(nvs_handle_t, const char *);
esp_err_t nvs_erase_all(nvs_handle_t);
esp_err_t nvs_commit(nvs_handle_t);
esp_err_t nvs_entry_find_in_handle(nvs_handle_t, nvs_type_t, nvs_iterator_t *);
void nvs_release_iterator(nvs_iterator_t);

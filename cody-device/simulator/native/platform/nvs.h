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
// Host-only emulation of the subset consumed by LUNASHELL_settings.cpp.
using nvs_handle_t = uint32_t;
constexpr std::size_t NVS_KEY_NAME_MAX_SIZE = 16;
constexpr esp_err_t ESP_ERR_NVS_NOT_INITIALIZED = 0x1101;
constexpr esp_err_t ESP_ERR_NVS_NOT_FOUND = 0x1102;
constexpr esp_err_t ESP_ERR_NVS_TYPE_MISMATCH = 0x1103;
constexpr esp_err_t ESP_ERR_NVS_READ_ONLY = 0x1104;
constexpr esp_err_t ESP_ERR_NVS_NOT_ENOUGH_SPACE = 0x1105;
constexpr esp_err_t ESP_ERR_NVS_INVALID_NAME = 0x1106;
constexpr esp_err_t ESP_ERR_NVS_INVALID_HANDLE = 0x1107;
constexpr esp_err_t ESP_ERR_NVS_KEY_TOO_LONG = 0x1109;
constexpr esp_err_t ESP_ERR_NVS_INVALID_STATE = 0x110b;
constexpr esp_err_t ESP_ERR_NVS_INVALID_LENGTH = 0x110c;
constexpr esp_err_t ESP_ERR_NVS_VALUE_TOO_LONG = 0x110e;

enum nvs_open_mode_t { NVS_READONLY, NVS_READWRITE, NVS_READWRITE_PURGE };
enum nvs_type_t { NVS_TYPE_U8 = 0x01, NVS_TYPE_U32 = 0x04, NVS_TYPE_U64 = 0x08,
    NVS_TYPE_I32 = 0x14, NVS_TYPE_I64 = 0x18, NVS_TYPE_STR = 0x21,
    NVS_TYPE_BLOB = 0x42, NVS_TYPE_ANY = 0xff };
struct nvs_opaque_iterator_t;
using nvs_iterator_t = nvs_opaque_iterator_t *;

extern "C" {
esp_err_t nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *out_handle);
void nvs_close(nvs_handle_t handle);
esp_err_t nvs_set_u8(nvs_handle_t handle, const char *key, uint8_t value);
esp_err_t nvs_set_i32(nvs_handle_t handle, const char *key, int32_t value);
esp_err_t nvs_set_u32(nvs_handle_t handle, const char *key, uint32_t value);
esp_err_t nvs_set_i64(nvs_handle_t handle, const char *key, int64_t value);
esp_err_t nvs_set_u64(nvs_handle_t handle, const char *key, uint64_t value);
esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *value);
esp_err_t nvs_get_i32(nvs_handle_t handle, const char *key, int32_t *value);
esp_err_t nvs_get_u32(nvs_handle_t handle, const char *key, uint32_t *value);
esp_err_t nvs_get_i64(nvs_handle_t handle, const char *key, int64_t *value);
esp_err_t nvs_get_u64(nvs_handle_t handle, const char *key, uint64_t *value);
esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value);
esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *value, std::size_t *length);
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, std::size_t length);
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *value, std::size_t *length);
esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key);
esp_err_t nvs_erase_all(nvs_handle_t handle);
esp_err_t nvs_commit(nvs_handle_t handle);
esp_err_t nvs_entry_find_in_handle(nvs_handle_t handle, nvs_type_t type, nvs_iterator_t *iterator);
void nvs_release_iterator(nvs_iterator_t iterator);

// Configure the workspace and absolute host file before init; never ESP NVS.
#if defined(_WIN32)
__declspec(dllexport)
#endif
esp_err_t cody_host_nvs_set_root(const char *absolute_workspace_root);
#if defined(_WIN32)
__declspec(dllexport)
#endif
esp_err_t cody_host_nvs_set_path(const char *absolute_path);
}

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

#include "LUNASHELL_settings.hpp"
#include "esp_log.h"
namespace LUNASHELL {
// No parameters: expose the production interface to unchanged renderer sources.
UserSettingsService &UserSettingsService::instance()
{
    static UserSettingsService service;
    return service;
}
// No parameters: no firmware storage handle is acquired on PC.
UserSettingsService::~UserSettingsService() = default;
// No parameters: firmware NVS is unavailable on the host.
// Exception: Returns ESP_ERR_NOT_SUPPORTED without claiming initialization.
esp_err_t UserSettingsService::initialize()
{
    ESP_LOGE("CodyHostSettings", "initialize rejected: firmware NVS unavailable on PC");
    return ESP_ERR_NOT_SUPPORTED;
}
// No parameters: no host operation initializes firmware NVS.
bool UserSettingsService::isInitialized() const { return false; }
// key: Requested firmware setting identity.
// value: Destination remains untouched because NVS is unavailable.
// Exception: Returns invalid arguments or ESP_ERR_NOT_SUPPORTED.
esp_err_t UserSettingsService::getString(const char *key, std::string *value) const
{
    const esp_err_t error = key && value ? ESP_ERR_NOT_SUPPORTED : ESP_ERR_INVALID_ARG;
    ESP_LOGE("CodyHostSettings", "getString key=%s rejected: %s (0x%x)", key ? key : "<null>", esp_err_to_name(error), error);
    return error;
}
// key: Requested firmware setting identity.
// value: Requested string; no operation writes firmware NVS.
// Exception: Returns invalid arguments or ESP_ERR_NOT_SUPPORTED.
esp_err_t UserSettingsService::setString(const char *key, const char *value)
{
    const esp_err_t error = key && value ? ESP_ERR_NOT_SUPPORTED : ESP_ERR_INVALID_ARG;
    ESP_LOGE("CodyHostSettings", "setString key=%s rejected: %s (0x%x)", key ? key : "<null>", esp_err_to_name(error), error);
    return error;
}
} // namespace LUNASHELL

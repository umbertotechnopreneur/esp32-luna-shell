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

#include "host_settings.hpp"
#include "LUNASHELL_settings.hpp"

namespace HostSettings {

// No parameters: storage exists only inside this explicitly requested host check.
State &state()
{
    static State fixture;
    return fixture;
}

// No parameters: reset all injected outcomes without touching any real storage.
void reset()
{
    state() = State{};
}

}  // namespace HostSettings

namespace LUNASHELL {

// No parameters: provide the real service interface with host-owned storage.
UserSettingsService &UserSettingsService::instance()
{
    static UserSettingsService service;
    return service;
}

// No parameters: the host fixture owns no NVS handle or physical resources.
UserSettingsService::~UserSettingsService() = default;

// No parameters: preserve the fixture's original initialization failure.
esp_err_t UserSettingsService::initialize()
{
    auto &fixture = HostSettings::state();
    fixture.initialized = fixture.initialize_error == ESP_OK;
    return fixture.initialize_error;
}

// No parameters: report only the host fixture's initialized state.
bool UserSettingsService::isInitialized() const
{
    return HostSettings::state().initialized;
}

// key: Setting identity supplied by the actual firmware consumer.
// value: Receives the saved host value only after a successful fixture read.
esp_err_t UserSettingsService::getString(const char *key, std::string *value) const
{
    if (key == nullptr || value == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    const auto &fixture = HostSettings::state();
    if (fixture.read_error != ESP_OK) {
        return fixture.read_error;
    }
    *value = fixture.saved_value;
    return ESP_OK;
}

// key: Setting identity supplied by the actual firmware consumer.
// value: Stable authored value to retain only after a successful fixture write.
esp_err_t UserSettingsService::setString(const char *key, const char *value)
{
    if (key == nullptr || value == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    auto &fixture = HostSettings::state();
    ++fixture.write_attempts;
    if (fixture.write_error != ESP_OK) {
        return fixture.write_error;
    }
    fixture.saved_value = value;
    fixture.read_error = ESP_OK;
    return ESP_OK;
}

}  // namespace LUNASHELL

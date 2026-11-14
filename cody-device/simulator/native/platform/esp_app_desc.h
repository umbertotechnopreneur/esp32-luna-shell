#pragma once

// Host identity is explicitly synthetic, never a physical firmware version.
struct esp_app_desc_t { char version[32]; };
inline const esp_app_desc_t *esp_app_get_description()
{
    static const esp_app_desc_t description{"SIMULATED"};
    return &description;
}

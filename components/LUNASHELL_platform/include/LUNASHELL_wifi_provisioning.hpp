#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

namespace LUNASHELL::wifi_provisioning {

// These owner-to-owner calls do not write credentials or issue another connect.
esp_err_t begin();
// ssid: Confirmed credential-event SSID bytes, possibly all 32 bytes occupied.
// ssid_capacity: Size of the SDK event's SSID array.
esp_err_t credentialsReceived(const uint8_t *ssid, size_t ssid_capacity);
// keep_connection: Preserve intent only after confirmed credential success.
esp_err_t end(bool keep_connection);

}  // namespace LUNASHELL::wifi_provisioning

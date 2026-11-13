#pragma once

#include "CODY2D_controls.hpp"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_platform.h"

namespace CODY {

// ssid: Complete retained printable SSID for the selected AP.
// selection: Exact security and BSSID selected by the controller.
// caption: Caller-owned output; copied into the selected-label owner before return.
// Exception: Rejects unsupported security or truncation without hiding AP identity.
esp_err_t format_wifi_selection_caption(const char *ssid, const LUNASHELL_wifi_selection_t &selection,
    char (&caption)[65]);

// network: One canonical scan result; its original identity is never modified.
// control: Prepared row admission and bounds; receives only the visible caption.
// label: Display-owned sanitized and ellipsized SSID storage retained until retirement.
// signal_label: Display-owned signal storage retained until retirement.
// signal: Receives the secondary real RSSI line.
// Exception: Preserves font/layout errors and rejects slots that cannot hold the two lines.
esp_err_t prepare_wifi_network_row(const LUNASHELL::WifiScanNetwork &network,
    CODY2D::CapsuleControl &control, char (&label)[34], char (&signal_label)[16], CODY2D::TextLine &signal);

} // namespace CODY

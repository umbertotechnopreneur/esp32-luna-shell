#include "CODY_wifi_list_presenter.hpp"
#include "CODY_flat_menu_row.hpp"
#include "CODY_theme_generated.hpp"
#include "scene_data.hpp"
#include <cstdio>
#include <cstring>

namespace CODY {

// ssid: Complete printable name retained by the Wi-Fi controller.
// selection: Selected AP's immutable security and BSSID.
// caption: Bounded output; no borrowed mutable snapshot pointer escapes.
// Exception: Preserves unsupported-security and truncation failures.
esp_err_t format_wifi_selection_caption(const char *ssid, const LUNASHELL_wifi_selection_t &selection,
    char (&caption)[65])
{
    if (ssid == nullptr) return ESP_ERR_INVALID_ARG;
    const char *security = nullptr;
    switch (selection.security) {
    case LUNASHELL_WIFI_SELECTED_OPEN: security = "Open"; break;
    case LUNASHELL_WIFI_SELECTED_WPA2: security = "WPA2"; break;
    case LUNASHELL_WIFI_SELECTED_WPA3: security = "WPA3"; break;
    case LUNASHELL_WIFI_SELECTED_WPA2_WPA3: security = "WPA2/WPA3"; break;
    default: return ESP_ERR_INVALID_STATE;
    }
    const auto *mac = selection.bssid;
    const int length = std::snprintf(caption, sizeof(caption),
        "%s %s %02X:%02X:%02X:%02X:%02X:%02X", ssid, security,
        static_cast<unsigned>(mac[0]), static_cast<unsigned>(mac[1]), static_cast<unsigned>(mac[2]),
        static_cast<unsigned>(mac[3]), static_cast<unsigned>(mac[4]), static_cast<unsigned>(mac[5]));
    return length > 0 && static_cast<size_t>(length) < sizeof(caption) ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

// network: Immutable canonical SSID and RSSI; sanitization is presentation-only.
// control: Owner-prepared touch rectangle and availability; only its caption changes.
// label: Retained display buffer, distinct from the complete selected SSID.
// signal_label: Retained display buffer for real signal strength.
// signal: Secondary text line borrowing signal_label.
// Exception: Returns the original text/layout error; invalid geometry is not clipped silently.
esp_err_t prepare_wifi_network_row(const LUNASHELL::WifiScanNetwork &network,
    CODY2D::CapsuleControl &control, char (&label)[34], char (&signal_label)[16], CODY2D::TextLine &signal)
{
    const auto &bounds = control.bounds;
    const auto &font = SceneData::body_font;
    if (bounds.width < 60U || bounds.height < 25U + font.height) return ESP_ERR_INVALID_SIZE;
    char printable[33]{};
    size_t length = 0;
    while (length < sizeof(network.ssid) - 1U && network.ssid[length] != '\0') {
        const auto character = static_cast<unsigned char>(network.ssid[length]);
        printable[length] = character >= 32U && character <= 126U ? static_cast<char>(character) : '?';
        ++length;
    }
    if (length == 0) std::memcpy(printable, "Hidden", sizeof("Hidden"));
    const uint16_t left = static_cast<uint16_t>(bounds.x + 48U);
    auto error = FlatMenuRow::prepare_label(font, printable,
        {left, static_cast<uint16_t>(bounds.y + 1U), static_cast<uint16_t>(bounds.width - 60U), font.height},
        control.enabled ? Theme::text_bright : Theme::text_muted, label, sizeof(label), control.caption);
    if (error != ESP_OK) return error;
    const int signal_length = std::snprintf(signal_label, sizeof(signal_label), "%d dBm", static_cast<int>(network.rssi));
    if (signal_length <= 0 || static_cast<size_t>(signal_length) >= sizeof(signal_label)) return ESP_ERR_INVALID_SIZE;
    return CODY2D::prepare_text_line(font, {signal_label, static_cast<uint16_t>(signal_length)},
        {static_cast<int16_t>(left), static_cast<int16_t>(bounds.y + 25U)}, Theme::secondary_default, signal);
}

} // namespace CODY

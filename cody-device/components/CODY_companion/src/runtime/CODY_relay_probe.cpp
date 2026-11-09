#include "CODY_relay_probe.hpp"

#include <cstddef>
#include <string_view>

#include "LUNASHELL_device_snapshot.hpp"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_tls.h"
#include "esp_transport_ssl.h"
#include "esp_transport_ws.h"
#include "sdkconfig.h"

namespace CODY {
namespace {

constexpr char kTag[] = "CODY_relay_probe";
constexpr char kHost[] = CONFIG_LUNASHELL_CODY_RELAY_HOST;
constexpr char kHealthPath[] = "/health/live";
constexpr char kWebSocketPath[] = "/health/websocket";
constexpr std::string_view kReadyFrame = "kodi.cloud.ready";
constexpr int kRelayPort = 443;
constexpr int kIoTimeoutMs = 4000;
constexpr int kResponseLimit = 96;

// host: Build-time DNS name. The request path and TLS port are fixed separately.
constexpr bool valid_host(std::string_view host)
{
    if (host.size() > 253 || host.find('.') == std::string_view::npos) {
        return false;
    }
    std::size_t label_length = 0;
    char previous = '\0';
    for (const char ch : host) {
        if (ch == '.') {
            if (label_length == 0 || previous == '-') { return false; }
            label_length = 0;
        } else {
            const bool alphanumeric = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
            if (!alphanumeric && (ch != '-' || label_length == 0)) { return false; }
            if (++label_length > 63) { return false; }
        }
        previous = ch;
    }
    return label_length != 0 && previous != '-';
}

static_assert(valid_host(kHost), "Relay host must be a DNS name without a port, path or credentials");

// body: Bounded complete liveness response from the verified HTTPS connection.
bool is_alive_response(std::string_view body)
{
    // This endpoint returns a single JSON string property. Match whole tokens
    // so whitespace outside a string is allowed and lookalike strings fail.
    constexpr std::string_view tokens[] = {"{", "\"status\"", ":", "\"alive\"", "}"};
    constexpr char whitespace[] = " \t\r\n";
    for (const auto token : tokens) {
        const auto start = body.find_first_not_of(whitespace);
        if (start == std::string_view::npos || body.substr(start, token.size()) != token) {
            return false;
        }
        body.remove_prefix(start + token.size());
    }
    return body.find_first_not_of(whitespace) == std::string_view::npos;
}

// client: Open HTTPS handle owned by the one-shot worker.
// Exception: Returns the original HTTP read error or a specific response rejection.
RelayProbeResult read_health_response(esp_http_client_handle_t client)
{
    const auto length = esp_http_client_fetch_headers(client);
    if (length < 0) {
        return {RelayProbeStage::Https, length == -ESP_ERR_HTTP_EAGAIN ?
            ESP_ERR_TIMEOUT : ESP_ERR_HTTP_FETCH_HEADER};
    }
    const int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        return {RelayProbeStage::Http, ESP_ERR_INVALID_RESPONSE, status};
    }
    if (length > kResponseLimit) {
        return {RelayProbeStage::Response, ESP_ERR_INVALID_SIZE, status};
    }

    char body[kResponseLimit + 1];
    const int count = esp_http_client_read(client, body, kResponseLimit + 1);
    if (count < 0) {
        const esp_err_t error = count == -ESP_ERR_HTTP_EAGAIN ? ESP_ERR_TIMEOUT : count;
        return {RelayProbeStage::Https, error, status};
    }
    if (count > kResponseLimit) {
        return {RelayProbeStage::Response, ESP_ERR_INVALID_SIZE, status};
    }
    if (!esp_http_client_is_complete_data_received(client)) {
        return {RelayProbeStage::Response, ESP_ERR_HTTP_INCOMPLETE_DATA, status};
    }
    if (!is_alive_response({body, static_cast<std::size_t>(count)})) {
        return {RelayProbeStage::Response, ESP_ERR_INVALID_RESPONSE, status};
    }
    return {RelayProbeStage::Response, ESP_OK, status};
}

// No parameters: Reads the canonical Wi-Fi and UTC observation before any network work.
// Exception: Preserves the snapshot error or reports the unavailable prerequisite.
RelayProbeResult check_prerequisites()
{
    LUNASHELL::DeviceStatusObservation observation{};
    const auto read_error = LUNASHELL::DeviceSnapshotService::instance()
        .readDeviceStatusObservation(observation);
    if (read_error != ESP_OK) { return {RelayProbeStage::Wifi, read_error}; }
    const auto &wifi = observation.wifi;
    if (!wifi.data_valid || wifi.data_stale || wifi.health != LUNASHELL::DeviceHealth::Ready ||
        wifi.last_error != ESP_OK ||
        !wifi.connected || !wifi.ip_acquired || wifi.ip_address[0] == '\0') {
        return {RelayProbeStage::Wifi, wifi.last_error != ESP_OK ? wifi.last_error : ESP_ERR_INVALID_STATE};
    }
    const auto &clock = observation.clock;
    if (!clock.data_valid || clock.data_stale || clock.last_error != ESP_OK || clock.utc_epoch_seconds <= 0) {
        return {RelayProbeStage::Clock, clock.last_error != ESP_OK ? clock.last_error : ESP_ERR_INVALID_STATE};
    }

    return {};
}

// No parameters: Sends one HTTPS request to the configured relay origin.
// Exception: Preserves the original HTTP or cleanup error when available.
RelayProbeResult check_https()
{
    esp_http_client_config_t config{};
    config.host = kHost;
    config.port = kRelayPort;
    config.path = kHealthPath;
    config.transport_type = HTTP_TRANSPORT_OVER_SSL;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = kIoTimeoutMs;
    config.disable_auto_redirect = true;
    config.skip_cert_common_name_check = false;
    config.buffer_size = 512;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    const auto client = esp_http_client_init(&config);
    if (client == nullptr) { return {RelayProbeStage::Https, ESP_FAIL}; }

    const auto open_error = esp_http_client_open(client, 0);
    auto result = open_error == ESP_OK ?
        read_health_response(client) : RelayProbeResult{RelayProbeStage::Https, open_error};
    const auto cleanup_error = esp_http_client_cleanup(client);
    if (cleanup_error != ESP_OK) {
        ESP_LOGE(kTag, "Liveness client cleanup failed (%s)", esp_err_to_name(cleanup_error));
        if (result.error == ESP_OK) {
            result.stage = RelayProbeStage::Https;
            result.error = cleanup_error;
        }
    }
    return result;
}

// transport: Active WebSocket sharing the TLS transport's error state.
// fallback: Specific error for the failed operation when the SDK supplies no detail.
esp_err_t websocket_error(esp_transport_handle_t transport, esp_err_t fallback)
{
    const auto handle = esp_transport_get_error_handle(transport);
    if (handle == nullptr) { return fallback; }
    const auto error = esp_tls_get_and_clear_last_error(handle, nullptr, nullptr);
    return error != ESP_OK ? error : fallback;
}

// transport: Upgraded TLS socket owned exclusively by the relay worker.
// opcode: Required frame type for this step of the probe.
// payload: Caller-owned buffer for the complete frame.
// size: Exact expected payload size and buffer capacity.
// deadline_us: Shared monotonic deadline for the incoming probe frames.
// Exception: A timeout, interrupted frame, or unexpected frame fails the check.
esp_err_t read_probe_frame(esp_transport_handle_t transport, ws_transport_opcodes_t opcode,
    char *payload, int size, int64_t deadline_us)
{
    int received = 0;
    while (received < size) {
        const int64_t remaining_us = deadline_us - esp_timer_get_time();
        if (remaining_us <= 0) { return ESP_ERR_TIMEOUT; }

        const int timeout_ms = static_cast<int>((remaining_us + 999) / 1000);
        const int count = esp_transport_read(transport, payload + received, size - received, timeout_ms);
        if (count < 0) { return websocket_error(transport, ESP_ERR_INVALID_RESPONSE); }
        const auto actual_opcode = esp_transport_ws_get_read_opcode(transport);
        if (actual_opcode != WS_TRANSPORT_OPCODES_NONE &&
            (actual_opcode != opcode || !esp_transport_ws_get_fin_flag(transport) ||
                esp_transport_ws_get_read_payload_len(transport) != size)) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        if (count == 0) { return ESP_ERR_TIMEOUT; }
        if (actual_opcode == WS_TRANSPORT_OPCODES_NONE) { return ESP_ERR_INVALID_RESPONSE; }
        received += count;
    }
    return ESP_OK;
}

// transport: WebSocket configured with the fixed diagnostic path on the verified TLS parent.
// Exception: Requires an HTTP 101 upgrade and the complete expected text frame.
RelayProbeResult exchange_websocket_probe(esp_transport_handle_t transport)
{
    const int connected = esp_transport_connect(transport, kHost, kRelayPort, kIoTimeoutMs);
    const int status = esp_transport_ws_get_upgrade_request_status(transport);
    if (connected != 0 || status != 101) {
        return {RelayProbeStage::WebSocketUpgrade, websocket_error(transport, ESP_ERR_INVALID_RESPONSE), status};
    }

    const int64_t deadline_us = esp_timer_get_time() + kIoTimeoutMs * 1000LL;
    constexpr int kReadySize = static_cast<int>(kReadyFrame.size());
    char ready[kReadySize];
    const auto ready_error = read_probe_frame(transport, WS_TRANSPORT_OPCODES_TEXT, ready, kReadySize, deadline_us);
    if (ready_error != ESP_OK) {
        return {RelayProbeStage::WebSocketFrame, ready_error, status};
    }
    if (std::string_view(ready, kReadySize) != kReadyFrame) {
        return {RelayProbeStage::WebSocketFrame, ESP_ERR_INVALID_RESPONSE, status};
    }

    constexpr int kCloseSize = 2;
    char close_payload[kCloseSize];
    const auto close_read_error = read_probe_frame(transport, WS_TRANSPORT_OPCODES_CLOSE,
        close_payload, kCloseSize, deadline_us);
    if (close_read_error != ESP_OK) {
        return {RelayProbeStage::WebSocketFrame, close_read_error, status};
    }
    if (close_payload[0] != 0x03 || static_cast<uint8_t>(close_payload[1]) != 0xE8) {
        return {RelayProbeStage::WebSocketFrame, ESP_ERR_INVALID_RESPONSE, status};
    }

    // ESP-IDF 6.0.2 masks outgoing bytes in place, so echo the writable buffer.
    constexpr auto close_opcode =
        static_cast<ws_transport_opcodes_t>(WS_TRANSPORT_OPCODES_FIN | WS_TRANSPORT_OPCODES_CLOSE);
    const int sent = esp_transport_ws_send_raw(transport, close_opcode, close_payload, kCloseSize, kIoTimeoutMs);
    const auto close_error = sent == kCloseSize ? ESP_OK :
        websocket_error(transport, sent == 0 ? ESP_ERR_TIMEOUT : ESP_FAIL);
    return {RelayProbeStage::WebSocketFrame, close_error, status};
}

// No parameters: Opens a one-shot WebSocket to the same DNS name as the HTTPS probe.
// Exception: Reports allocation, configuration, TLS, upgrade, or frame errors.
RelayProbeResult check_websocket()
{
    const auto tls = esp_transport_ssl_init();
    if (tls == nullptr) { return {RelayProbeStage::WebSocketUpgrade, ESP_ERR_NO_MEM}; }
    esp_transport_ssl_crt_bundle_attach(tls, esp_crt_bundle_attach);
    esp_transport_ssl_set_common_name(tls, kHost);

    // The socket borrows its TLS parent. Destroy the WebSocket before its parent.
    const auto websocket = esp_transport_ws_init(tls);
    if (websocket == nullptr) {
        esp_transport_destroy(tls);
        return {RelayProbeStage::WebSocketUpgrade, ESP_ERR_NO_MEM};
    }

    esp_transport_ws_config_t config{};
    config.ws_path = kWebSocketPath;
    config.propagate_control_frames = true;
    const auto config_error = esp_transport_ws_set_config(websocket, &config);
    auto result = config_error == ESP_OK ? exchange_websocket_probe(websocket) :
        RelayProbeResult{RelayProbeStage::WebSocketUpgrade, config_error};

    // Destroy also closes the TLS parent on a failed upgrade. An open upgraded
    // socket can be closed explicitly so its cleanup error reaches the caller.
    if (result.http_status == 101 && esp_transport_close(websocket) != 0) {
        const auto error = websocket_error(tls, ESP_FAIL);
        ESP_LOGE(kTag, "WebSocket transport cleanup failed (%s)", esp_err_to_name(error));
        if (result.error == ESP_OK) { result.error = error; }
    }
    esp_transport_destroy(websocket);
    esp_transport_destroy(tls);
    return result;
}

} // namespace

// No parameters: Runs on an internal-stack worker separate from display and pairing.
// Exception: Reports the first failed prerequisite or transport stage.
RelayProbeResult check_relay_liveness()
{
    const auto prerequisites = check_prerequisites();
    if (prerequisites.error != ESP_OK) { return prerequisites; }

    // Release HTTPS and its TLS buffers before opening the WebSocket.
    const auto https = check_https();
    if (https.error != ESP_OK) { return https; }
    return check_websocket();
}

} // namespace CODY

#include "CODY_relay_probe.hpp"

#include <cstddef>
#include <string_view>

#include "LUNASHELL_device_snapshot.hpp"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "sdkconfig.h"

namespace CODY {
namespace {

constexpr char kTag[] = "CODY_relay_probe";
constexpr char kHost[] = CONFIG_LUNASHELL_CODY_RELAY_HOST;
constexpr char kHealthPath[] = "/health/live";
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

} // namespace

// No parameters: Runs on an internal-stack worker separate from display and pairing.
// Exception: Preserves snapshot, transport and cleanup errors without logging server data.
RelayProbeResult check_relay_liveness()
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

    esp_http_client_config_t config{};
    config.host = kHost;
    config.port = 443;
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

} // namespace CODY

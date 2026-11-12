#include "LUNASHELL_device_snapshot.hpp"

#include <cmath>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {
using namespace LUNASHELL;
constexpr char kTag[] = "openrouter_catalog";
constexpr char kBaseUrl[] = "https://openrouter.ai/api/v1/model/";
constexpr size_t kBodyLimit = 8192;
constexpr unsigned kDepthLimit = 16, kNodeLimit = 512;
constexpr int64_t kRefreshUs = 12LL * 60 * 60 * 1000000;
constexpr int64_t kRetryUs = 5LL * 60 * 1000000;
constexpr TickType_t kPrerequisiteRetry = pdMS_TO_TICKS(10000U);
static_assert(kOpenRouterCodexModels.size() <= UINT16_MAX, "Configured catalog exceeds index capacity");
static_assert(sizeof(OpenRouterModelQuote) <= 208, "Catalog entry exceeds its memory budget");

SemaphoreHandle_t s_mutex;
OpenRouterModelQuote s_buffers[2][kOpenRouterCodexModels.size()];
OpenRouterModelQuote *s_live = s_buffers[0];
OpenRouterModelQuote *s_stage = s_buffers[1];
OpenRouterCatalogStatus s_status;
char *s_body;

// object, name, value: Read an explicitly present nonnegative JSON price.
// Exception: Missing, invalid and non-finite strings remain unavailable.
bool price(const cJSON *object, const char *name, double &value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsString(item) || !item->valuestring || !*item->valuestring) return false;
    char *end = nullptr;
    errno = 0;
    const double parsed = std::strtod(item->valuestring, &end);
    if (errno == ERANGE || !end || *end || !std::isfinite(parsed) || parsed < 0) return false;
    value = parsed;
    return true;
}

// object, name, value: Read one explicitly present representable integral count.
// Exception: Missing or malformed values remain unavailable.
bool count_value(const cJSON *object, const char *name, uint64_t &value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) || item->valuedouble < 0 ||
        item->valuedouble > 9007199254740991.0 || std::floor(item->valuedouble) != item->valuedouble) return false;
    value = static_cast<uint64_t>(item->valuedouble);
    return true;
}

// body, size: Complete bounded model response before cJSON allocation.
// Exception: Rejects excessive nesting/node count; cJSON checks full JSON syntax.
bool bounded_json(const char *body, size_t size)
{
    unsigned depth = 0, nodes = 0;
    bool string = false, escaped = false;
    for (size_t i = 0; i < size; ++i) {
        const char ch = body[i];
        if (string) {
            if (escaped) escaped = false;
            else if (ch == '\\') escaped = true;
            else if (ch == '"') string = false;
        } else if (ch == '"') string = true;
        else if (ch == '{' || ch == '[') { if (++depth > kDepthLimit || ++nodes > kNodeLimit) return false; }
        else if (ch == ',') { if (++nodes > kNodeLimit) return false; }
        else if (ch == '}' || ch == ']') { if (!depth) return false; --depth; }
    }
    return !depth && !string;
}

// body, size: Validated complete response for the exact configured model.
// expected, quote: Exact OpenRouter identity and caller-owned output.
// Exception: Rejects mismatched identity and malformed JSON; missing fields stay invalid.
bool parse_quote(const char *body, size_t size, const char *expected, OpenRouterModelQuote &quote)
{
    if (!bounded_json(body, size)) return false;
    const char *end = nullptr;
    cJSON *root = cJSON_ParseWithLengthOpts(body, size + 1, &end, true);
    if (!root || !cJSON_IsObject(root)) { cJSON_Delete(root); return false; }
    const cJSON *model = cJSON_GetObjectItemCaseSensitive(root, "data");
    const cJSON *id = cJSON_IsObject(model) ? cJSON_GetObjectItemCaseSensitive(model, "id") : nullptr;
    const bool valid = cJSON_IsString(id) && id->valuestring && std::strcmp(id->valuestring, expected) == 0;
    if (valid) {
        quote = {};
        std::snprintf(quote.id, sizeof(quote.id), "%s", expected);
        const cJSON *pricing = cJSON_GetObjectItemCaseSensitive(model, "pricing");
        if (cJSON_IsObject(pricing)) {
            quote.input_valid = price(pricing, "prompt", quote.input_usd_per_token);
            quote.output_valid = price(pricing, "completion", quote.output_usd_per_token);
            quote.cached_input_valid = price(pricing, "input_cache_read", quote.cached_input_usd_per_token);
            const cJSON *overrides = cJSON_GetObjectItemCaseSensitive(pricing, "overrides");
            quote.tiered_valid = cJSON_IsArray(overrides) || cJSON_IsNull(overrides);
            quote.tiered = cJSON_IsArray(overrides) && cJSON_GetArraySize(overrides) > 0;
        }
        quote.context_valid = count_value(model, "context_length", quote.context_tokens);
        const cJSON *provider = cJSON_GetObjectItemCaseSensitive(model, "top_provider");
        if (cJSON_IsObject(provider))
            quote.max_output_valid = count_value(provider, "max_completion_tokens", quote.max_output_tokens);
        quote.quote_available = true;
    }
    cJSON_Delete(root);
    return valid;
}

// stage: Receives the unavailable canonical prerequisite.
// Exception: Preserves the snapshot or prerequisite owner's error.
esp_err_t prerequisites(OpenRouterCatalogStage &stage)
{
    DeviceStatusObservation observation{};
    esp_err_t result = DeviceSnapshotService::instance().readDeviceStatusObservation(observation);
    stage = OpenRouterCatalogStage::Wifi;
    if (result != ESP_OK) return result;
    const auto &wifi = observation.wifi;
    if (!wifi.data_valid || wifi.data_stale || wifi.health != DeviceHealth::Ready ||
        wifi.last_error != ESP_OK || !wifi.connected || !wifi.ip_acquired || !wifi.ip_address[0])
        return wifi.last_error != ESP_OK ? wifi.last_error : ESP_ERR_INVALID_STATE;
    stage = OpenRouterCatalogStage::Clock;
    const auto &clock = observation.clock;
    if (!clock.data_valid || clock.data_stale || clock.last_error != ESP_OK || clock.utc_epoch_seconds <= 0)
        return clock.last_error != ESP_OK ? clock.last_error : ESP_ERR_INVALID_STATE;
    return ESP_OK;
}

// quote: Configured identity and previously published quote for one model.
// transport_reported: Tracks whether this refresh emitted a transport diagnostic.
// Exception: Retains old data on transport errors; 404 explicitly clears unavailable prices.
esp_err_t fetch_model(OpenRouterModelQuote &quote, bool &transport_reported)
{
    const int64_t attempted = esp_timer_get_time();
    char url[sizeof(kBaseUrl) + sizeof(quote.id)]{};
    const int url_length = std::snprintf(url, sizeof(url), "%s%s", kBaseUrl, quote.id);
    if (url_length < 0 || static_cast<size_t>(url_length) >= sizeof(url)) return ESP_ERR_INVALID_SIZE;
    esp_http_client_config_t cfg{};
    cfg.url = url;
    cfg.timeout_ms = 15000;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.disable_auto_redirect = true;
    auto client = esp_http_client_init(&cfg);
    esp_err_t error = client ? esp_http_client_open(client, 0) : ESP_ERR_NO_MEM;
    if (client && error == ESP_ERR_HTTP_CONNECT && !transport_reported) {
        transport_reported = true;
        // ESP_ERR_HTTP_CONNECT also covers DNS, TCP and TLS failures. Retain the
        // lower-level reason before closing the client; never log model data.
        const int socket_error = esp_http_client_get_errno(client);
        int tls_error = 0, tls_flags = 0;
        const esp_err_t tls_status = esp_http_client_get_and_clear_last_tls_error(client, &tls_error, &tls_flags);
        const auto internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
        ESP_LOGW(kTag, "HTTPS connect: errno=%d tls=0x%lx detail=%d flags=0x%x heap=%lu largest=%lu",
            socket_error, static_cast<unsigned long>(tls_status), tls_error, static_cast<unsigned>(tls_flags),
            static_cast<unsigned long>(heap_caps_get_free_size(internal)),
            static_cast<unsigned long>(heap_caps_get_largest_free_block(internal)));
    }
    int status = 0;
    size_t bytes = 0;
    if (error == ESP_OK) {
        (void)esp_http_client_fetch_headers(client);
        status = esp_http_client_get_status_code(client);
        if (status == 404) error = ESP_ERR_NOT_FOUND;
        else if (status != 200) error = ESP_FAIL;
    }
    while (error == ESP_OK) {
        const int n = esp_http_client_read(client, s_body + bytes, static_cast<int>(kBodyLimit + 1 - bytes));
        if (n < 0) { error = ESP_FAIL; break; }
        if (!n) break;
        bytes += static_cast<size_t>(n);
        if (bytes > kBodyLimit) { error = ESP_ERR_INVALID_SIZE; break; }
    }
    if (error == ESP_OK && !esp_http_client_is_complete_data_received(client)) error = ESP_ERR_INVALID_RESPONSE;
    if (client) { esp_http_client_close(client); esp_http_client_cleanup(client); }
    OpenRouterModelQuote parsed{};
    if (error == ESP_OK) {
        s_body[bytes] = '\0';
        if (!parse_quote(s_body, bytes, quote.id, parsed)) error = ESP_ERR_INVALID_RESPONSE;
    }
    ClockSnapshot clock{};
    const bool clock_valid = DeviceSnapshotService::instance().readClockObservation(clock) == ESP_OK &&
        clock.data_valid && !clock.data_stale && clock.last_error == ESP_OK && clock.utc_epoch_seconds > 0 &&
        clock.last_success_at_us > 0 && clock.last_success_at_us <= esp_timer_get_time();
    const int64_t completed = esp_timer_get_time();
    if (error == ESP_OK) {
        parsed.timestamp_valid = clock_valid;
        parsed.clock_observed_utc_seconds = clock_valid ? clock.utc_epoch_seconds : 0;
        parsed.clock_observed_monotonic_us = clock_valid ? clock.last_success_at_us : 0;
        parsed.fetched_monotonic_us = completed;
        quote = parsed;
    } else if (error == ESP_ERR_NOT_FOUND) {
        char id[sizeof(quote.id)];
        std::memcpy(id, quote.id, sizeof(id));
        quote = {};
        std::memcpy(quote.id, id, sizeof(id));
        quote.model_not_found = true;
        // Preserve the time of this authoritative 404 separately from a later failed attempt.
        quote.timestamp_valid = clock_valid;
        quote.clock_observed_utc_seconds = clock_valid ? clock.utc_epoch_seconds : 0;
        quote.clock_observed_monotonic_us = clock_valid ? clock.last_success_at_us : 0;
        quote.fetched_monotonic_us = completed;
    }
    quote.last_attempt_monotonic_us = attempted;
    quote.attempt_clock_valid = clock_valid;
    quote.attempt_clock_utc_seconds = clock_valid ? clock.utc_epoch_seconds : 0;
    quote.attempt_clock_monotonic_us = clock_valid ? clock.last_success_at_us : 0;
    quote.attempt_completed_monotonic_us = completed;
    quote.last_error = error;
    quote.http_status = status;
    return error;
}

// deferred: True when canonical Wi-Fi or clock prerequisites prevent acquisition.
// Exception: Publishes actual prerequisite/model errors and never fabricates prices.
esp_err_t refresh(bool &deferred)
{
    const int64_t attempted = esp_timer_get_time();
    OpenRouterCatalogStage stage;
    esp_err_t error = prerequisites(stage);
    deferred = error != ESP_OK;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    s_status.last_attempt_monotonic_us = attempted;
    if (deferred) {
        s_status.last_error = error; s_status.http_status = 0; s_status.stage = stage;
        xSemaphoreGive(s_mutex); return error;
    }
    for (size_t i = 0; i < kOpenRouterCodexModels.size(); ++i) s_stage[i] = s_live[i];
    xSemaphoreGive(s_mutex);

    esp_err_t aggregate = ESP_OK;
    OpenRouterCatalogStage failed_stage = OpenRouterCatalogStage::Response;
    bool changed = false;
    bool transport_reported = false;
    for (size_t i = 0; i < kOpenRouterCodexModels.size(); ++i) {
        OpenRouterCatalogStage prerequisite_stage;
        const auto ready = prerequisites(prerequisite_stage);
        if (ready != ESP_OK) { deferred = true; aggregate = ready; stage = prerequisite_stage; break; }
        auto &quote = s_stage[i];
        const int64_t now = esp_timer_get_time();
        const int64_t successful_at = quote.last_error == ESP_ERR_NOT_FOUND ?
            quote.last_attempt_monotonic_us : quote.fetched_monotonic_us;
        if ((quote.last_error == ESP_OK || quote.last_error == ESP_ERR_NOT_FOUND) && successful_at > 0 &&
            now >= successful_at && now - successful_at < kRefreshUs) continue;
        const auto result = fetch_model(quote, transport_reported);
        changed = true;
        if (result != ESP_OK && result != ESP_ERR_NOT_FOUND && aggregate == ESP_OK) {
            aggregate = result;
            failed_stage = quote.http_status ? OpenRouterCatalogStage::Response : OpenRouterCatalogStage::Https;
        }
    }
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    if (changed) {
        auto *old = s_live; s_live = s_stage; s_stage = old;
        if (++s_status.generation == 0) ++s_status.generation;
        s_status.fetched_monotonic_us = 0;
        s_status.timestamp_valid = false;
        s_status.clock_observed_utc_seconds = 0;
        s_status.clock_observed_monotonic_us = 0;
        for (size_t i = 0; i < kOpenRouterCodexModels.size(); ++i) {
            const auto &quote = s_live[i];
            if (quote.quote_available && quote.fetched_monotonic_us > s_status.fetched_monotonic_us) {
                s_status.fetched_monotonic_us = quote.fetched_monotonic_us;
                s_status.timestamp_valid = quote.timestamp_valid;
                s_status.clock_observed_utc_seconds = quote.clock_observed_utc_seconds;
                s_status.clock_observed_monotonic_us = quote.clock_observed_monotonic_us;
            }
        }
    }
    s_status.last_error = aggregate;
    s_status.stage = deferred ? stage : aggregate == ESP_OK ? OpenRouterCatalogStage::Ready : failed_stage;
    s_status.http_status = 0; // Individual HTTP results belong to their model.
    xSemaphoreGive(s_mutex);
    return aggregate;
}

// arg: Unused FreeRTOS worker argument.
// Exception: Failed requests are retried; missing prerequisites cause no TLS or log spam.
void worker(void *arg)
{
    (void)arg;
    int64_t next_attempt_us = 0;
    for (;;) {
        OpenRouterCatalogStage stage;
        const esp_err_t available = prerequisites(stage);
        if (available != ESP_OK || esp_timer_get_time() >= next_attempt_us) {
            bool deferred = false;
            const esp_err_t error = refresh(deferred);
            if (error != ESP_OK && !deferred) ESP_LOGW(kTag, "catalog refresh failed: %s", esp_err_to_name(error));
            next_attempt_us = deferred ? 0 : esp_timer_get_time() + (error == ESP_OK ? kRefreshUs : kRetryUs);
        }
        vTaskDelay(kPrerequisiteRetry);
    }
}
} // namespace

namespace LUNASHELL {
// No parameters: publish configured identities and start the optional bounded worker.
// Exception: Returns allocation/task errors while preserving the unavailable model list.
esp_err_t start_openrouter_catalog()
{
    if (s_mutex) return ESP_ERR_INVALID_STATE;
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;
    for (size_t i = 0; i < kOpenRouterCodexModels.size(); ++i)
        std::snprintf(s_live[i].id, sizeof(s_live[i].id), "%s", kOpenRouterCodexModels[i]);
    s_status.valid = true;
    s_status.count = static_cast<uint16_t>(kOpenRouterCodexModels.size());
    s_status.generation = 1;
    s_body = static_cast<char *>(heap_caps_malloc(kBodyLimit + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!s_body || xTaskCreate(worker, "openrouter", 6144, nullptr, 3, nullptr) != pdPASS) {
        heap_caps_free(s_body); s_body = nullptr;
        s_status.last_error = ESP_ERR_NO_MEM;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

// status: Caller-owned bounded catalog metadata.
// Exception: Returns owner initialization or lock failure.
esp_err_t DeviceSnapshotService::readOpenRouterCatalogStatus(OpenRouterCatalogStatus &status) const
{
    if (!s_mutex) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    status = s_status; xSemaphoreGive(s_mutex); return ESP_OK;
}

// generation, offset: Stable catalog version and first configured identity.
// output, capacity, written: Caller-owned bounded page and copied count.
// Exception: Rejects generation races and invalid page bounds.
esp_err_t DeviceSnapshotService::readOpenRouterCatalogPage(uint32_t generation, size_t offset,
    OpenRouterModelQuote *output, size_t capacity, size_t &written) const
{
    written = 0;
    if (!s_mutex) return ESP_ERR_INVALID_STATE;
    if ((!output && capacity) || capacity > kOpenRouterCatalogPageMax) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t error = ESP_OK;
    if (!s_status.valid || (generation && generation != s_status.generation)) error = ESP_ERR_INVALID_STATE;
    else if (offset > s_status.count) error = ESP_ERR_INVALID_ARG;
    else while (written < capacity && offset + written < s_status.count) {
        output[written] = s_live[offset + written]; ++written;
    }
    xSemaphoreGive(s_mutex); return error;
}
} // namespace LUNASHELL

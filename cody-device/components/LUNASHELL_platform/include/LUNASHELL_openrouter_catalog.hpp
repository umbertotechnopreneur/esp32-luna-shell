#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "esp_err.h"

namespace LUNASHELL {

inline constexpr size_t kOpenRouterCatalogPageMax = 8;

// Exact model IDs from Cody's Codex identity mappings. This deliberately short
// editable list is a selector, not a claim that a provider offers every model.
inline constexpr std::array<const char *, 9> kOpenRouterCodexModels{{
    "openai/gpt-6.1-terra", "openai/gpt-6.1-sol", "openai/gpt-6-sol",
    "openai/gpt-6-astra", "openai/gpt-6-luna", "openai/gpt-5.6-terra",
    "openai/gpt-5.6-sol", "openai/gpt-5.6-luna", "openai/gpt-5.5",
}};

// An OpenRouter base quote in USD per token, not an OpenAI/Codex bill.
// Missing fields remain invalid; tiered rates require separate interpretation.
struct OpenRouterModelQuote {
    char id[65]{};
    double input_usd_per_token = 0;
    double output_usd_per_token = 0;
    double cached_input_usd_per_token = 0;
    uint64_t context_tokens = 0;
    uint64_t max_output_tokens = 0;
    bool input_valid = false;
    bool output_valid = false;
    bool cached_input_valid = false;
    bool context_valid = false;
    bool max_output_valid = false;
    bool tiered_valid = false;
    bool tiered = false;
    bool quote_available = false;
    bool model_not_found = false; // Last authoritative model lookup returned HTTP 404.
    bool timestamp_valid = false;
    int64_t clock_observed_utc_seconds = 0;
    int64_t clock_observed_monotonic_us = 0;
    int64_t fetched_monotonic_us = 0;
    int64_t last_attempt_monotonic_us = 0;
    bool attempt_clock_valid = false;
    int64_t attempt_clock_utc_seconds = 0;
    int64_t attempt_clock_monotonic_us = 0;
    int64_t attempt_completed_monotonic_us = 0;
    esp_err_t last_error = ESP_ERR_INVALID_STATE;
    int http_status = 0;
};

enum class OpenRouterCatalogStage : uint8_t { Starting, Wifi, Clock, Https, Response, Ready };

struct OpenRouterCatalogStatus {
    uint32_t generation = 0;
    uint16_t count = 0;
    bool valid = false;
    bool truncated = false;
    // UTC is an actual clock sample taken near completion; its monotonic time
    // is retained separately from the precise fetch-completion monotonic time.
    bool timestamp_valid = false;
    int64_t clock_observed_utc_seconds = 0;
    int64_t clock_observed_monotonic_us = 0;
    int64_t fetched_monotonic_us = 0;
    int64_t last_attempt_monotonic_us = 0;
    // A failed refresh preserves the previous generation, never making it current.
    esp_err_t last_error = ESP_ERR_INVALID_STATE;
    OpenRouterCatalogStage stage = OpenRouterCatalogStage::Starting;
    int http_status = 0;
};

// No parameters: start one bounded network worker after the snapshot owner is ready.
// Exception: Reports startup failure without replacing the snapshot owner.
esp_err_t start_openrouter_catalog();

} // namespace LUNASHELL

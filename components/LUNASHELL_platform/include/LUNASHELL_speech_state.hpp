#pragma once
#include "sdkconfig.h"
#include "esp_err.h"
#include <cstdint>

namespace LUNASHELL {
enum class SpeechLanguage : uint8_t { English = 1, Italian = 2, Mandarin = 3, Cantonese = 4 };
enum class SpeechState : uint8_t {
    Disabled = 0, Idle = 1, Queued = 2, Preparing = 3, Synthesizing = 4,
    Playing = 5, Completed = 6, Cancelled = 7, Suppressed = 8, Failed = 9,
    Unavailable = 10, WaitingForStorage = 11
};
enum class SpeechStorage : uint8_t { None = 0, Flash = 1, Sd = 2 };
struct SpeechSnapshot {
    SpeechState state = SpeechState::Disabled;
    SpeechStorage storage = SpeechStorage::None;
    SpeechLanguage language = SpeechLanguage::English;
    uint32_t request_id = 0;
    uint32_t pcm_samples = 0;
    uint32_t sample_rate_hz = 0;
    int32_t engine_error = 0;
    int32_t storage_errno = 0;
    esp_err_t last_error = ESP_ERR_NOT_SUPPORTED;
    int64_t changed_at_us = 0;
};
} // namespace LUNASHELL

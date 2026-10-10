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

#include "LUNASHELL_tts.hpp"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_platform.h"
#include "LUNASHELL_tts_storage.h"
#include "asset_registry.h"
#include "native_engine.h"
#include "bsp/runtime_cores.h"
#include "mem_alloc.h"
#include <atomic>
#include <cerrno>
#include <cstring>
#include <limits>
#include <mutex>
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"

namespace {
using namespace LUNASHELL;
constexpr char TAG[] = "LUNASHELL_tts";
constexpr size_t kMaximumTextBytes = 1024;
static_assert(LUNASHELL_TTS_CORE_ID != LUNASHELL_RENDER_CORE_ID);
static_assert(configNUMBER_OF_CORES == 2, "TTS requires a dedicated non-renderer core");
enum class RequestPurpose : uint8_t { Explicit, ItalianBootTest };
RequestPurpose s_request_purpose = RequestPurpose::Explicit;
#if CONFIG_LUNASHELL_TTS_BOOT_TEST_ITALIAN
bool s_boot_test_requested;
constexpr int64_t kBootStorageWaitUs = 30000000;
constexpr char kItalianBootText[] = "Ciao questa è una sintesi vocale di test 5 ottobre 2026";
#endif
#if CONFIG_LUNASHELL_TTS_DATA_SD
constexpr SpeechStorage kSpeechStorage = SpeechStorage::Sd;
#else
constexpr SpeechStorage kSpeechStorage = SpeechStorage::Flash;
#endif
std::mutex s_request_mutex;
TaskHandle_t s_worker;
bool s_initialization_attempted;
esp_err_t s_initialization_error = ESP_ERR_INVALID_STATE;
bool s_busy;
uint32_t s_last_id;
uint32_t s_active_id;
std::atomic<uint32_t> s_cancel_id;
SpeechRequest s_request;
// Text is accessed only by cache-enabled admission and the PSRAM speech worker.
EXT_RAM_BSS_ATTR char s_text[kMaximumTextBytes + 1];
SpeechSnapshot s_status;
esp_err_t s_callback_error;
bool s_cancelled;
bool s_suppressed;
bool s_engine_ready;
int s_terminal_engine_error;
std::atomic<esp_err_t> s_publication_error{ESP_OK};
#if CONFIG_LUNASHELL_TTS_DATA_SD
bool s_package_ready;
EXT_RAM_BSS_ATTR uint8_t *s_package_buffers[12]{};
#endif

// language: Stable requested identity; the flash package admits only English.
const char *voice_path(SpeechLanguage language)
{
    switch (language) {
    case SpeechLanguage::English: return "/luna-tts/espeak-ng-data/lang/gmw/en";
#if CONFIG_LUNASHELL_TTS_DATA_SD
    case SpeechLanguage::Italian: return "/luna-tts/espeak-ng-data/lang/roa/it";
    case SpeechLanguage::Mandarin: return "/luna-tts/espeak-ng-data/lang/sit/cmn";
    case SpeechLanguage::Cantonese: return "/luna-tts/espeak-ng-data/lang/sit/yue";
#else
    case SpeechLanguage::Italian:
    case SpeechLanguage::Mandarin:
    case SpeechLanguage::Cantonese: return nullptr;
#endif
    }
    return nullptr;
}

// text: Borrowed caller input with an explicit size, never unbounded strlen.
// bytes: Exact UTF-8 length; NUL, controls, malformed and overlong sequences fail.
bool valid_text(const char *text, size_t bytes)
{
    if (text == nullptr || bytes == 0 || bytes > kMaximumTextBytes) return false;
    size_t offset = 0;
    while (offset < bytes) {
        const uint8_t first = static_cast<uint8_t>(text[offset++]);
        if (first < 0x80) {
            if (first < 0x20 || first == 0x7f) return false;
            continue;
        }
        unsigned continuation;
        uint32_t value;
        uint32_t minimum;
        if (first >= 0xc2 && first <= 0xdf) {
            continuation = 1;
            value = first & 0x1f;
            minimum = 0x80;
        } else if (first >= 0xe0 && first <= 0xef) {
            continuation = 2;
            value = first & 0x0f;
            minimum = 0x800;
        } else if (first >= 0xf0 && first <= 0xf4) {
            continuation = 3;
            value = first & 7;
            minimum = 0x10000;
        } else {
            return false;
        }
        if (continuation > bytes - offset) return false;
        for (unsigned index = 0; index < continuation; ++index) {
            const uint8_t next = static_cast<uint8_t>(text[offset++]);
            if ((next & 0xc0) != 0x80) return false;
            value = (value << 6) | (next & 0x3f);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff) ||
            (value >= 0x80 && value <= 0x9f)) return false;
    }
    return true;
}

// state: Actual lifecycle transition, never merely an inferred playback outcome.
// error: Original operation result; engine/filesystem causes travel separately.
// Exception: Returns and latches a canonical publication failure.
esp_err_t publish(SpeechState state, esp_err_t error)
{
    s_status.state = state;
    s_status.last_error = error;
    s_status.changed_at_us = esp_timer_get_time();
    const esp_err_t result = DeviceSnapshotService::instance().reportSpeech(s_status);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Speech state publication failed (%s)", esp_err_to_name(result));
        s_publication_error.store(result);
    }
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Speech request %lu state=%u failed (%s, %ld), engine=%ld errno=%ld",
            static_cast<unsigned long>(s_status.request_id), static_cast<unsigned>(state),
            esp_err_to_name(error), static_cast<long>(error), static_cast<long>(s_status.engine_error),
            static_cast<long>(s_status.storage_errno));
    }
    return result;
}

// observation: Receives a coherent aged canonical view, never a sensor-side read.
// Exception: Refuses standby and unavailable SD; normal requests also require allowed clock policy.
esp_err_t read_policy(SpeechObservation &observation)
{
    esp_err_t error = DeviceSnapshotService::instance().readSpeechObservation(observation);
    if (error != ESP_OK) return error;
    if (observation.operating_state != DeviceOperatingState::Active) return ESP_ERR_INVALID_STATE;
    bool boot_test = false;
#if CONFIG_LUNASHELL_TTS_BOOT_TEST_ITALIAN
    boot_test = s_request_purpose == RequestPurpose::ItalianBootTest;
#endif
    // Only the explicitly enabled diagnostic phrase may run before clock sync
    // or in quiet hours. Normal API requests retain their existing policy.
    if (!boot_test && clock_cue_policy(observation.clock) != ClockCuePolicy::Allowed) {
        return ESP_ERR_INVALID_STATE;
    }
#if CONFIG_LUNASHELL_TTS_DATA_SD
    const auto &storage = observation.storage;
    if (!storage.mounted || !storage.card_response_verified || storage.poisoned ||
        !storage.data_valid || storage.data_stale || storage.last_error != ESP_OK) {
        return storage.last_error == ESP_OK ? ESP_ERR_INVALID_STATE : storage.last_error;
    }
#endif
    return ESP_OK;
}

// No parameters: data ownership transfers only after every manifest hash succeeds.
// Exception: SD/package/PSRAM failures preserve their cause and free only unmapped buffers.
esp_err_t prepare_package()
{
#if CONFIG_LUNASHELL_TTS_DATA_SD
    if (luna_tts_asset_count != 12) return ESP_ERR_INVALID_SIZE;
    lunashell_tts_storage_file_t files[12]{};
    esp_err_t error = ESP_OK;
    for (size_t index = 0; index < luna_tts_asset_count; ++index) {
        auto &asset = luna_tts_assets[index];
        if (!s_package_ready) {
            s_package_buffers[index] = static_cast<uint8_t *>(heap_caps_malloc(asset.size,
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            if (s_package_buffers[index] == nullptr) {
                error = ESP_ERR_NO_MEM;
                break;
            }
        }
        files[index] = {asset.path, s_package_buffers[index], asset.size, asset.sha256};
    }
    int io_errno = 0;
    if (error == ESP_OK) error = LUNASHELL_storage_load_tts(files, luna_tts_asset_count, s_package_ready, &io_errno);
    s_status.storage_errno = io_errno;
    if (error != ESP_OK) {
        if (!s_package_ready) {
            for (auto &buffer : s_package_buffers) { heap_caps_free(buffer); buffer = nullptr; }
        }
        return error;
    }
    for (size_t index = 0; index < luna_tts_asset_count; ++index) {
        luna_tts_assets[index].data = s_package_buffers[index];
    }
    // The engine borrows mapped data until restart; no free or hot-swap is admitted.
    s_package_ready = true;
#endif
    return ESP_OK;
}

// samples: Borrowed synchronous engine PCM; no pointer survives this callback.
// count: Actual sample count; zero/null denotes an engine event, never speaker completion.
// events: Opaque upstream event list, unused by this service.
// Exception: Stops synthesis on cancellation, policy, transport or publication errors.
int consume_pcm(short *samples, int count, void *events)
{
    (void)events;
    if (s_cancel_id.load() == s_status.request_id) {
        s_cancelled = true;
        s_callback_error = ESP_ERR_INVALID_STATE;
        return 1;
    }
    SpeechObservation observation;
    s_callback_error = read_policy(observation);
    if (s_callback_error != ESP_OK) {
        s_suppressed = true;
        return 1;
    }
    if (samples == nullptr || count == 0) return 0;
    if (count < 0 || static_cast<uint32_t>(count) > UINT32_MAX - s_status.pcm_samples) {
        s_callback_error = ESP_ERR_INVALID_SIZE;
        return 1;
    }
    s_callback_error = LUNASHELL_audio_write_speech(samples, static_cast<size_t>(count), s_status.request_id);
    if (s_callback_error != ESP_OK) return 1;
    s_status.pcm_samples += static_cast<uint32_t>(count);
    s_callback_error = publish(SpeechState::Playing, ESP_OK);
    // Let other owners run between bounded engine blocks without queueing PCM.
    vTaskDelay(1);
    return s_callback_error == ESP_OK ? 0 : 1;
}

// state: Terminal result from the sole worker, published before accepting another request.
// error: Original operation cause; a late cancellation never overwrites a real failure.
// Exception: Canonical publication failure remains latched for subsequent admissions.
void finish_request(SpeechState state, esp_err_t error)
{
    std::lock_guard<std::mutex> lock(s_request_mutex);
    if (s_cancel_id.load() == s_status.request_id && error == ESP_OK) {
        state = SpeechState::Cancelled;
        error = ESP_ERR_INVALID_STATE;
    }
    publish(state, error);
    s_busy = false;
    s_active_id = 0;
}

#if CONFIG_LUNASHELL_TTS_BOOT_TEST_ITALIAN
// No parameters: waits only for the canonical storage owner's initial mount report.
// Exception: Returns cancellation, power, publication, actual mount errors or a bounded timeout.
esp_err_t wait_for_boot_storage()
{
    esp_err_t error = publish(SpeechState::WaitingForStorage, ESP_OK);
    if (error != ESP_OK) return error;
    const int64_t deadline = esp_timer_get_time() + kBootStorageWaitUs;
    for (;;) {
        if (s_cancel_id.load() == s_status.request_id) {
            s_cancelled = true;
            return ESP_ERR_INVALID_STATE;
        }
        SpeechObservation observation;
        error = DeviceSnapshotService::instance().readSpeechObservation(observation);
        if (error != ESP_OK) return error;
        if (observation.operating_state != DeviceOperatingState::Active) {
            s_suppressed = true;
            return ESP_ERR_INVALID_STATE;
        }
        const auto &storage = observation.storage;
        if (storage.mount_outcome != StorageMountOutcome::NotAttempted || storage.poisoned) {
            // A completed failed mount is terminal for this one-shot request.
            // No card insertion retry, format request or alternate medium is admitted.
            return read_policy(observation);
        }
        if (storage.last_attempt_at_us > 0 && storage.last_error != ESP_OK &&
            storage.last_error != ESP_ERR_INVALID_STATE) {
            return storage.last_error;
        }
        if (esp_timer_get_time() >= deadline) return ESP_ERR_TIMEOUT;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
#endif

// No parameters: sole worker serializes the non-reentrant engine and its C allocator guard.
// Exception: Failed engine initialization is retained until restart; absent SD may be retried explicitly.
void execute_request()
{
    s_callback_error = ESP_OK;
    s_cancelled = false;
    s_suppressed = false;
    esp_err_t error = ESP_OK;
#if CONFIG_LUNASHELL_TTS_BOOT_TEST_ITALIAN
    if (s_request_purpose == RequestPurpose::ItalianBootTest) {
        error = wait_for_boot_storage();
        if (error != ESP_OK) {
            const SpeechState state = s_cancelled ? SpeechState::Cancelled :
                s_suppressed ? SpeechState::Suppressed : SpeechState::Failed;
            finish_request(state, error);
            return;
        }
    }
#endif
    SpeechObservation observation;
    error = read_policy(observation);
    if (error == ESP_OK && s_cancel_id.load() == s_status.request_id) {
        s_cancelled = true;
        error = ESP_ERR_INVALID_STATE;
    }
    if (error != ESP_OK) {
        finish_request(s_cancelled ? SpeechState::Cancelled : SpeechState::Suppressed, error);
        return;
    }
    if (s_terminal_engine_error != 0) {
        s_status.engine_error = s_terminal_engine_error;
        error = s_terminal_engine_error == ENOMEM ? ESP_ERR_NO_MEM :
            static_cast<esp_err_t>(s_terminal_engine_error);
        finish_request(SpeechState::Failed, error);
        return;
    }
    error = publish(SpeechState::Preparing, ESP_OK);
    if (error == ESP_OK) error = prepare_package();
    if (error == ESP_OK && !s_engine_ready) {
        const int result = s_terminal_engine_error != 0 ? s_terminal_engine_error : luna_tts_engine_initialize();
        s_status.engine_error = result;
        if (result != 0) {
            s_terminal_engine_error = result;
            error = result == ENOMEM ? ESP_ERR_NO_MEM : static_cast<esp_err_t>(result);
        } else {
            s_engine_ready = true;
        }
    }
    if (error != ESP_OK) {
        finish_request(SpeechState::Failed, error);
        return;
    }
    s_status.sample_rate_hz = static_cast<uint32_t>(luna_tts_engine_sample_rate());
    error = read_policy(observation);
    if (s_cancel_id.load() == s_status.request_id) {
        s_cancelled = true;
        error = ESP_ERR_INVALID_STATE;
    }
    if (error != ESP_OK) {
        finish_request(s_cancelled ? SpeechState::Cancelled : SpeechState::Suppressed, error);
        return;
    }
    error = LUNASHELL_audio_begin_speech(s_status.sample_rate_hz, s_status.request_id);
    if (error != ESP_OK) {
        finish_request(SpeechState::Failed, error);
        return;
    }
    error = publish(SpeechState::Synthesizing, ESP_OK);
    if (error == ESP_OK) {
        const int result = luna_tts_engine_synthesize(voice_path(s_request.language), s_text,
            s_request.text_bytes + 1, s_request.words_per_minute, s_request.pitch,
            s_request.pitch_range, consume_pcm);
        s_status.engine_error = result;
        error = s_callback_error != ESP_OK ? s_callback_error :
            result == 0 ? ESP_OK : result == ENOMEM ? ESP_ERR_NO_MEM : static_cast<esp_err_t>(result);
        if (luna_espeak_poisoned()) s_terminal_engine_error = result;
    }
    if (s_cancel_id.load() == s_status.request_id && error == ESP_OK) {
        s_cancelled = true;
        error = ESP_ERR_INVALID_STATE;
    }
    error = LUNASHELL_audio_end_speech(s_status.request_id, error);
    const SpeechState state = s_cancelled ? SpeechState::Cancelled : s_suppressed ? SpeechState::Suppressed :
        error == ESP_OK ? SpeechState::Completed : SpeechState::Failed;
    finish_request(state, error);
}

// context: Unused; all engine calls and PCM ownership belong to this single worker.
void speech_worker(void *context)
{
    (void)context;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        {
            std::lock_guard<std::mutex> lock(s_request_mutex);
            if (!s_busy) continue;
            s_status = {};
            s_status.storage = kSpeechStorage;
            s_status.request_id = s_active_id;
            s_status.language = s_request.language;
        }
        execute_request();
    }
}
// request: Explicit validated UTF-8 request; a private bounded copy is made on admission.
// request_id: Receives stable identity only after successful queue publication/notification.
// purpose: Private identity; only the fixed boot diagnostic may bypass clock policy.
// Exception: Returns rejected input, unsupported language, busy, lifecycle or publication failure.
esp_err_t admit_speech(const SpeechRequest &request, uint32_t &request_id, RequestPurpose purpose)
{
    request_id = 0;
    esp_err_t error = ESP_OK;
    if (!valid_text(request.text, request.text_bytes) || request.words_per_minute < 80 ||
        request.words_per_minute > 450 || request.pitch > 100 || request.pitch_range > 100) {
        error = ESP_ERR_INVALID_ARG;
    } else if (voice_path(request.language) == nullptr) {
        error = ESP_ERR_NOT_SUPPORTED;
    }
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Speech input rejected: language=%u bytes=%u rate=%u pitch=%u range=%u (%s)",
            static_cast<unsigned>(request.language), static_cast<unsigned>(request.text_bytes),
            static_cast<unsigned>(request.words_per_minute), static_cast<unsigned>(request.pitch),
            static_cast<unsigned>(request.pitch_range), esp_err_to_name(error));
        return error;
    }
    std::lock_guard<std::mutex> lock(s_request_mutex);
#if CONFIG_LUNASHELL_TTS_BOOT_TEST_ITALIAN
    if (purpose == RequestPurpose::ItalianBootTest) {
        if (s_boot_test_requested) {
            ESP_LOGE(TAG, "Italian boot speech rejected: already requested this boot");
            return ESP_ERR_INVALID_STATE;
        }
        s_boot_test_requested = true;
    }
#endif
    if (s_initialization_error != ESP_OK) error = s_initialization_error;
    else if (s_worker == nullptr || s_busy || s_last_id == UINT32_MAX) error = ESP_ERR_INVALID_STATE;
    else if (s_publication_error.load() != ESP_OK) error = s_publication_error.load();
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Speech admission rejected (%s)", esp_err_to_name(error));
        return error;
    }
    s_request_purpose = purpose;
    s_request = request;
    memcpy(s_text, request.text, request.text_bytes);
    s_text[request.text_bytes] = 0;
    s_request.text = s_text;
    s_active_id = ++s_last_id;
    s_cancel_id.store(0);
    s_busy = true;
    SpeechSnapshot queued;
    queued.storage = kSpeechStorage;
    queued.state = SpeechState::Queued;
    queued.language = request.language;
    queued.request_id = s_active_id;
    queued.last_error = ESP_OK;
    queued.changed_at_us = esp_timer_get_time();
    error = DeviceSnapshotService::instance().reportSpeech(queued);
    if (error == ESP_OK && xTaskNotifyGive(s_worker) != pdPASS) error = ESP_FAIL;
    if (error != ESP_OK) {
        s_busy = false;
        s_active_id = 0;
        s_publication_error.store(error);
        ESP_LOGE(TAG, "Speech queue publication/notification failed (%s)", esp_err_to_name(error));
        // The worker was never admitted. Publish the retained failure locally.
        s_status = queued;
        publish(SpeechState::Failed, error);
        return error;
    }
    request_id = s_active_id;
    return ESP_OK;
}

}

namespace LUNASHELL {
// No parameters: starts silently and leaves engine/data allocations lazy.
// Exception: Publishes and returns optional startup worker/allocation errors.
esp_err_t initialize_speech()
{
    std::lock_guard<std::mutex> lock(s_request_mutex);
    if (s_initialization_attempted) return s_initialization_error;
    s_initialization_attempted = true;
    s_status.storage = kSpeechStorage;
    s_initialization_error = publish(SpeechState::Idle, ESP_OK);
    if (s_initialization_error == ESP_OK && xTaskCreatePinnedToCoreWithCaps(speech_worker, "LunaSpeech",
        CONFIG_LUNASHELL_TTS_STACK_BYTES, nullptr, 3, &s_worker, LUNASHELL_TTS_CORE_ID,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        s_worker = nullptr;
        s_initialization_error = ESP_ERR_NO_MEM;
    }
    if (s_initialization_error != ESP_OK) {
        publish(SpeechState::Unavailable, s_initialization_error);
    } else {
        ESP_LOGI(TAG, "Speech worker pinned to core %d; renderer core %d",
                 LUNASHELL_TTS_CORE_ID, LUNASHELL_RENDER_CORE_ID);
    }
    return s_initialization_error;
}

// request: Explicit validated UTF-8 input; normal quiet-time policy always applies.
// request_id: Receives admitted request identity, not a playback completion claim.
// Exception: Returns original validation, lifecycle, busy or publication error.
esp_err_t request_speech(const SpeechRequest &request, uint32_t &request_id)
{
    return admit_speech(request, request_id, RequestPurpose::Explicit);
}

#if CONFIG_LUNASHELL_TTS_BOOT_TEST_ITALIAN
// No parameters: submits the fixed Italian diagnostic once after mandatory startup.
// Exception: Returns original admission error; startup may continue after this optional failure.
esp_err_t request_boot_speech_test()
{
    SpeechRequest request;
    request.language = SpeechLanguage::Italian;
    request.text = kItalianBootText;
    request.text_bytes = sizeof(kItalianBootText) - 1;
    uint32_t request_id = 0;
    const esp_err_t error = admit_speech(request, request_id, RequestPurpose::ItalianBootTest);
    if (error == ESP_OK) {
        ESP_LOGI(TAG, "Italian boot speech queued: request=%lu; execution core=%d",
                 static_cast<unsigned long>(request_id), LUNASHELL_TTS_CORE_ID);
    }
    return error;
}
#endif

// request_id: Exact active request identity; a later request is never cancelled accidentally.
// Exception: Returns unknown or completed identities; cancellation admission is not completion.
esp_err_t cancel_speech(uint32_t request_id)
{
    std::lock_guard<std::mutex> lock(s_request_mutex);
    if (request_id == 0 || !s_busy || request_id != s_active_id) {
        ESP_LOGE(TAG, "Speech cancellation rejected: request=%lu", static_cast<unsigned long>(request_id));
        return ESP_ERR_INVALID_STATE;
    }
    s_cancel_id.store(request_id);
    return ESP_OK;
}
} // namespace LUNASHELL

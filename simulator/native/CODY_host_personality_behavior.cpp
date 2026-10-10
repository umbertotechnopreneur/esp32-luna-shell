// Desktop transport for the unchanged firmware mood and caress models.
#include "CODY_happiness.hpp"
#include "CODY_petting.hpp"
#include "personality_rules.hpp"
#include "esp_log.h"
#include <new>

#if defined(_WIN32)
#define CODY_BEHAVIOR_EXPORT extern "C" __declspec(dllexport)
#else
#define CODY_BEHAVIOR_EXPORT extern "C" __attribute__((visibility("default")))
#endif

// handle: Borrowed native frame owned by the graphics transport.
// scene: Receives its actual validated RigScene; ownership stays with that frame.
// Exception: Returns the graphics owner's invalid-frame error.
esp_err_t cody_host_scene_model(const void *handle, const CODY::RigScene *&scene);

namespace {
struct HostBehavior {
    CODY::HappinessState happiness{};
    CODY::PettingState petting{};
};

// operation: Actual rejected host boundary or firmware operation.
// error: Original esp_err_t retained for the Python caller.
esp_err_t behavior_result(const char *operation, esp_err_t error)
{
    if (error != ESP_OK) {
        ESP_LOGE("CodyHostBehavior", "%s failed: %s (0x%x)", operation, esp_err_to_name(error), error);
    }
    return error;
}
}  // namespace

// output: Receives a new independent owner only after authored mood initialization succeeds.
// Exception: Returns invalid arguments, allocation failure or the real configuration error.
CODY_BEHAVIOR_EXPORT int cody_host_behavior_create(void **output)
{
    if (output == nullptr) {
        return behavior_result("create", ESP_ERR_INVALID_ARG);
    }
    *output = nullptr;
    auto *state = new (std::nothrow) HostBehavior;
    if (state == nullptr) {
        return behavior_result("create", ESP_ERR_NO_MEM);
    }
    const esp_err_t error = CODY::initialize_happiness(CODY::PersonalityData::happiness, state->happiness);
    if (error != ESP_OK) {
        delete state;
        return behavior_result("initialize_happiness", error);
    }
    *output = state;
    return ESP_OK;
}

// handle: Independent behavior owner allocated by this DLL, or nullptr.
CODY_BEHAVIOR_EXPORT void cody_host_behavior_destroy(void *handle)
{
    delete static_cast<HostBehavior *>(handle);
}

// handle: Native mood owner; no Python-owned mood state is accepted.
// values: Two output words containing happiness and its authored complement.
// remainder: Receives the exact partial decay interval from the firmware model.
// Exception: Returns invalid arguments or the original validation error without state mutation.
CODY_BEHAVIOR_EXPORT int cody_host_happiness_read(void *handle, uint16_t *values, int64_t *remainder)
{
    if (handle == nullptr || values == nullptr || remainder == nullptr) {
        return behavior_result("happiness_read", ESP_ERR_INVALID_ARG);
    }
    const auto &state = static_cast<HostBehavior *>(handle)->happiness;
    auto verified = state;
    const esp_err_t error = CODY::advance_happiness(CODY::PersonalityData::happiness, 0, verified);
    if (error != ESP_OK) {
        return behavior_result("happiness_read", error);
    }
    values[0] = state.happiness;
    values[1] = static_cast<uint16_t>(CODY::PersonalityData::happiness.maximum - state.happiness);
    *remainder = state.decay_remainder_us;
    return ESP_OK;
}

// handle: Native mood owner; ordinary Studio does not duplicate its arithmetic.
// elapsed_us: Original nonnegative awake desktop interval, including animation pause.
// Exception: Returns invalid arguments or the original firmware arithmetic error.
CODY_BEHAVIOR_EXPORT int cody_host_happiness_advance(void *handle, int64_t elapsed_us)
{
    if (handle == nullptr) {
        return behavior_result("happiness_advance", ESP_ERR_INVALID_ARG);
    }
    return behavior_result("advance_happiness", CODY::advance_happiness(
        CODY::PersonalityData::happiness, elapsed_us, static_cast<HostBehavior *>(handle)->happiness));
}

// handle: Native artistic mood owner.
// event: Catalog identity validated by the unchanged firmware event handler.
// Exception: Returns invalid arguments or an unbound event without changing mood.
CODY_BEHAVIOR_EXPORT int cody_host_happiness_event(void *handle, uint8_t event)
{
    if (handle == nullptr) {
        return behavior_result("happiness_event", ESP_ERR_INVALID_ARG);
    }
    return behavior_result("apply_happiness_event", CODY::apply_happiness_event(
        CODY::PersonalityData::happiness, static_cast<CODY::PersonalityEvent>(event),
        static_cast<HostBehavior *>(handle)->happiness));
}

// handle: Native gesture owner; cancellation preserves its accepted cooldown.
// Exception: Returns invalid arguments for an absent owner.
CODY_BEHAVIOR_EXPORT int cody_host_petting_cancel(void *handle)
{
    if (handle == nullptr) {
        return behavior_result("petting_cancel", ESP_ERR_INVALID_ARG);
    }
    CODY::cancel_petting(static_cast<HostBehavior *>(handle)->petting);
    return ESP_OK;
}

// handle: Independent native gesture owner.
// frame: Last presented frame whose actual C++ geometry defines caress regions.
// x: Logical desktop pointer x after the shared rotation mapping.
// y: Logical desktop pointer y after the shared rotation mapping.
// pressed: Explicitly synthetic desktop button state.
// pressed_edge: True only for this contact's first acquired desktop sample.
// sampled_us: Original monotonic desktop acquisition time.
// zone: Receives the exact typed recognized zone only on successful observation.
// Exception: Returns original frame, geometry, time or recognizer errors without fallback.
CODY_BEHAVIOR_EXPORT int cody_host_petting_observe(void *handle, const void *frame, float x, float y,
    bool pressed, bool pressed_edge, int64_t sampled_us, uint8_t *zone)
{
    if (handle == nullptr || zone == nullptr) {
        return behavior_result("petting_observe", ESP_ERR_INVALID_ARG);
    }
    const CODY::RigScene *scene = nullptr;
    const esp_err_t frame_error = cody_host_scene_model(frame, scene);
    if (frame_error != ESP_OK) {
        return behavior_result("petting_frame", frame_error);
    }
    CODY::PettingZone triggered = CODY::PettingZone::None;
    const esp_err_t error = CODY::observe_petting(*scene, {x, y}, pressed, pressed_edge, sampled_us,
        static_cast<HostBehavior *>(handle)->petting, triggered);
    if (error == ESP_OK) {
        *zone = static_cast<uint8_t>(triggered);
    }
    return behavior_result("observe_petting", error);
}

// No parameters: returns the compiled shared rule, without replicating a desktop default.
CODY_BEHAVIOR_EXPORT int64_t cody_host_petting_affection_us()
{
    return CODY::PettingConfig::affection_us;
}

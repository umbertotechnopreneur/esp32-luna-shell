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

// Synthetic resources execute the production power plan; no ESP sensor/snapshot owner is instantiated.
#include "LUNASHELL_standby_policy.hpp"
#include "LUNASHELL_standby_transition.hpp"
#include "LUNASHELL_standby_resources.hpp"
#include "esp_log.h"
#include <array>
#include <new>

#if defined(_WIN32)
#define CODY_HOST_EXPORT extern "C" __declspec(dllexport)
#else
#define CODY_HOST_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {
using namespace LUNASHELL::power_policy;
constexpr size_t kResourceCount = static_cast<size_t>(StandbyStep::Count);
constexpr const char *kNames[] = {"full-snapshots", "ui-sampling", "gps", "distance",
    "storage", "imu", "motion-wake", "voice-afe", "base-audio", "wireless",
    "standalone-display", "display", "lvgl", "prepare-dark-frame", "prepare-wake-frame"};
static_assert(sizeof(kNames) / sizeof(*kNames) == kResourceCount);

struct Fault {
    esp_err_t error = ESP_OK;
    uint32_t remaining = 0;
    uint32_t skip = 0;
};
struct HostStandby {
    std::array<StandbyResourceStatus, kResourceCount> resources{};
    std::array<std::array<Fault, 2>, kResourceCount> faults{};
    std::array<uint32_t, kResourceCount> calls{};
    StandbySequenceResult sequence{};
    StandbySequenceResult attempted{};
    bool standby = false;
    bool incomplete = false;
    bool voice_listening = false;
    bool retained_connection = false;
    bool deep_sleep_enabled = false;
    bool touch_wake_available = true;
    esp_err_t error = ESP_OK;
    esp_err_t rollback_error = ESP_OK;
    esp_err_t recovery_error = ESP_OK;
    esp_err_t display_rollback_error = ESP_OK;
};
struct OperationContext {
    HostStandby *owner;
    StandbyStep operation;
};

// operation: Rejected native ABI operation.
// error: Original admission or allocation failure.
// Exception: Returns the same real failure after logging it.
esp_err_t reject_operation(const char *operation, esp_err_t error)
{
    ESP_LOGE("CodyHostStandby", "SYNTHETIC %s rejected: %s (0x%x)",
        operation, esp_err_to_name(error), error);
    return error;
}

// context: Borrowed synthetic resource operation; it never accesses real hardware.
// target: Requested resource policy state, selecting its independently injected fault.
// Exception: Returns the exact requested fault; successful calls remain explicit synthetic observations.
esp_err_t synthetic_operation(void *context, bool target)
{
    auto &operation = *static_cast<OperationContext *>(context);
    const size_t index = static_cast<size_t>(operation.operation);
    auto &owner = *operation.owner;
    ++owner.calls[index];
    auto &fault = owner.faults[index][target ? 1 : 0];
    if (fault.skip != 0) {
        --fault.skip;
        return ESP_OK;
    }
    if (fault.remaining != 0) {
        --fault.remaining;
        ESP_LOGE("CodyHostStandby", "SYNTHETIC %s to %s injected failure: %s (0x%x)",
            kNames[index], target ? "standby" : "active", esp_err_to_name(fault.error), fault.error);
        return fault.error;
    }
    return ESP_OK;
}

// context: Borrowed standalone synthetic owner serialized by its Python transport.
// operation: Production plan's typed resource operation.
// target: Requested logical standby state.
// Exception: Keeps optional restore error and mandatory cleanup error independently.
StandbyStepResult execute_synthetic_step(void *context, StandbyStep operation, bool target)
{
    auto &owner = *static_cast<HostStandby *>(context);
    const size_t index = static_cast<size_t>(operation);
    auto &status = owner.resources[index];
    OperationContext transport{&owner, operation};
    StandbyStepResult result;
    if (operation == StandbyStep::Lvgl) {
        // Cody uses its standalone native renderer, so there is no LVGL owner.
        status.unsupported = true;
        return result;
    }
    if (operation == StandbyStep::PrepareDarkFrame) {
        owner.resources[static_cast<size_t>(StandbyStep::Display)].consistent = false;
        result.mandatory = synthetic_operation(&transport, target);
        return result;
    }
    if (operation == StandbyStep::PrepareWakeFrame) {
        result.mandatory = synthetic_operation(&transport, target);
        return result;
    }
    if (operation == StandbyStep::Display) {
        esp_err_t rollback_error = ESP_OK;
        result.mandatory = transition_standby_display(target, status.applied, status.consistent,
            true, &transport, synthetic_operation, rollback_error);
        if (owner.display_rollback_error == ESP_OK && rollback_error != ESP_OK) {
            owner.display_rollback_error = rollback_error;
        }
        return result;
    }
    const bool requested = (operation == StandbyStep::VoiceAfe || operation == StandbyStep::BaseAudio)
        ? select_standby_audio(target, owner.voice_listening) : target;
    result.mandatory = transition_standby_resource(kNames[index], requested, status,
        &transport, synthetic_operation, target && operation == StandbyStep::Wireless);
    if (!target && operation == StandbyStep::Storage && result.mandatory != ESP_OK) {
        result.optional = result.mandatory;
        result.mandatory = ESP_OK;
    }
    if (!target && operation == StandbyStep::Wireless && result.mandatory != ESP_OK) {
        result.optional = result.mandatory;
        result.mandatory = transition_standby_resource(kNames[index], true, status,
            &transport, synthetic_operation);
    }
    return result;
}

// context: Borrowed synthetic owner; no readings escape into DeviceSnapshotService.
// target: Requested confirmed terminal state.
// result: Optional resource errors admitted only by production wake policy.
// Exception: Rejects every uncertain mandatory resource and partial radio cleanup.
esp_err_t finalize_synthetic_sequence(void *context, bool target,
    const StandbySequenceResult &result)
{
    const auto &owner = *static_cast<HostStandby *>(context);
    for (size_t index = 0; index < kResourceCount; ++index) {
        const auto operation = static_cast<StandbyStep>(index);
        if (operation == StandbyStep::PrepareDarkFrame || operation == StandbyStep::PrepareWakeFrame) {
            continue;
        }
        const auto &status = owner.resources[index];
        if (status.unsupported || (!target && operation == StandbyStep::Storage && result.storage != ESP_OK)) {
            continue;
        }
        const bool expected = (operation == StandbyStep::VoiceAfe || operation == StandbyStep::BaseAudio)
            ? select_standby_audio(target, owner.voice_listening) : target;
        const bool matches = operation == StandbyStep::Wireless
            ? standby_wireless_matches(status, target, !target && result.wireless != ESP_OK)
            : standby_resource_matches(status, expected);
        if (!matches) {
            ESP_LOGE("CodyHostStandby", "SYNTHETIC %s final state uncertain", kNames[index]);
            return ESP_ERR_INVALID_STATE;
        }
    }
    return ESP_OK;
}

// context: Serialized synthetic owner retaining resource confidence through recovery.
// target: Production plan target.
// best_effort: Continue recovery while preserving its first mandatory error.
// Exception: Returns actual injected mandatory errors and retains independent optional errors.
esp_err_t run_synthetic_sequence(void *context, bool target, bool best_effort)
{
    auto &owner = *static_cast<HostStandby *>(context);
    owner.sequence = execute_standby_sequence(target, best_effort, context,
        execute_synthetic_step, finalize_synthetic_sequence);
    return owner.sequence.error;
}
}

// output: Receives a separately owned synthetic-resource context.
// standby: Initial scenario lifecycle; boot does not count as a runtime transition.
// Exception: Returns invalid argument or real allocation failure.
CODY_HOST_EXPORT esp_err_t cody_host_standby_create(void **output, bool standby)
{
    if (output == nullptr) {
        return reject_operation("create output", ESP_ERR_INVALID_ARG);
    }
    auto *owner = new (std::nothrow) HostStandby{};
    if (owner == nullptr) {
        return reject_operation("create allocation", ESP_ERR_NO_MEM);
    }
    owner->standby = standby;
    for (auto &status : owner->resources) {
        status.applied = standby;
    }
    owner->resources[static_cast<size_t>(StandbyStep::Lvgl)].unsupported = true;
    *output = owner;
    return ESP_OK;
}

// context: Synthetic owner released only after its serialized calls finish.
CODY_HOST_EXPORT void cody_host_standby_destroy(void *context)
{
    delete static_cast<HostStandby *>(context);
}

// context: Borrowed serialized host-only owner, unchanged during candidate construction.
// output: Receives a separately owned heap candidate including resource confidence and fault counters.
// Exception: Returns real allocation/admission errors; no hardware recovery is inferred or executed.
CODY_HOST_EXPORT esp_err_t cody_host_standby_clone(void *context, void **output)
{
    const auto *owner = static_cast<const HostStandby *>(context);
    if (owner == nullptr || output == nullptr) {
        return reject_operation("clone owner/output", ESP_ERR_INVALID_ARG);
    }
    auto *candidate = new (std::nothrow) HostStandby(*owner);
    if (candidate == nullptr) {
        return reject_operation("clone allocation", ESP_ERR_NO_MEM);
    }
    *output = candidate;
    return ESP_OK;
}

// context: Existing synthetic owner; logical profile changes require active state.
// deep: Whether the modeled build enables deep sleep.
// retained: Whether incoming notifications require a retained connection.
// voice: Whether audio must keep acquiring wake speech.
// touch: Whether the modeled profile exposes the actual deep-sleep wake path.
// Exception: Rejects service changes while standby or an incomplete transaction owns resources.
CODY_HOST_EXPORT esp_err_t cody_host_standby_profile(void *context, bool deep,
    bool retained, bool voice, bool touch)
{
    auto *owner = static_cast<HostStandby *>(context);
    if (owner == nullptr) {
        return reject_operation("profile owner", ESP_ERR_INVALID_ARG);
    }
    if (owner->standby || owner->incomplete) {
        return reject_operation("profile lifecycle", ESP_ERR_INVALID_STATE);
    }
    owner->deep_sleep_enabled = deep;
    owner->retained_connection = retained;
    owner->voice_listening = voice;
    owner->touch_wake_available = touch;
    return ESP_OK;
}

// context: Existing serialized synthetic resource owner.
// resource: Stable typed production resource identity.
// target: Resource state whose operation should fail.
// error: Original ESP error to inject; success cannot masquerade as a fault.
// count: Number of failed calls to inject after the skip interval.
// skip: Number of matching operation calls permitted before the fault.
// Exception: Rejects invalid identities and fault parameters without replacing prior configuration.
CODY_HOST_EXPORT esp_err_t cody_host_standby_fault(void *context, uint8_t resource,
    bool target, esp_err_t error, uint32_t count, uint32_t skip)
{
    auto *owner = static_cast<HostStandby *>(context);
    if (owner == nullptr || resource >= kResourceCount || error == ESP_OK ||
        count == 0 || count > 100 || skip > 100 || resource == static_cast<uint8_t>(StandbyStep::Lvgl)) {
        ESP_LOGE("CodyHostStandby", "SYNTHETIC fault rejected: resource=%u error=%d count=%u skip=%u",
            resource, error, count, skip);
        return ESP_ERR_INVALID_ARG;
    }
    owner->faults[resource][target ? 1 : 0] = {error, count, skip};
    ESP_LOGI("CodyHostStandby", "SYNTHETIC fault configured: %s target=%s error=%s count=%u skip=%u",
        kNames[resource], target ? "standby" : "active", esp_err_to_name(error), count, skip);
    return ESP_OK;
}

// context: Existing serialized synthetic resource owner.
// target: Requested logical standby state; physical sleep is never executed on the PC.
// Exception: Returns original transaction error even after successful rollback or fallback.
CODY_HOST_EXPORT esp_err_t cody_host_standby_transition(void *context, bool target)
{
    auto *owner = static_cast<HostStandby *>(context);
    if (owner == nullptr) {
        return reject_operation("transition owner", ESP_ERR_INVALID_ARG);
    }
    if (owner->incomplete) {
        ESP_LOGE("CodyHostStandby", "SYNTHETIC standby request rejected: previous recovery incomplete");
        return ESP_ERR_INVALID_STATE;
    }
    owner->error = ESP_OK;
    owner->rollback_error = ESP_OK;
    owner->recovery_error = ESP_OK;
    owner->display_rollback_error = ESP_OK;
    if (target == owner->standby) {
        return ESP_OK;
    }
    owner->error = run_synthetic_sequence(owner, target, false);
    owner->attempted = owner->sequence;
    if (owner->error == ESP_OK) {
        owner->standby = target;
        owner->error = owner->sequence.storage != ESP_OK ? owner->sequence.storage : owner->sequence.wireless;
        return owner->error;
    }
    const auto recovery = recover_standby_sequence(target, owner, run_synthetic_sequence);
    owner->rollback_error = recovery.rollback_error;
    owner->recovery_error = recovery.recovery_error;
    if (recovery.state == StandbyRecoveryState::Incomplete) {
        owner->incomplete = true;
    } else {
        owner->standby = recovery.state == StandbyRecoveryState::Requested ? target : !target;
    }
    return owner->error;
}

// context: Existing serialized synthetic owner.
// values: Receives 12 explicitly bounded policy/result fields, never a firmware snapshot.
// count: Exact result-field count, checked before touching the destination.
// applied: Receives each resource's last confirmed policy state.
// consistent: Receives each resource's confidence separately from its last state.
// unsupported: Receives genuine build-off owner classification separately from failures.
// calls: Receives actual synthetic owner operation-call counts.
// resource_count: Exact compiled resource count.
// Exception: Returns invalid argument/size for an invalid ABI buffer.
CODY_HOST_EXPORT esp_err_t cody_host_standby_read(void *context, int32_t *values,
    size_t count, uint8_t *applied, uint8_t *consistent, uint8_t *unsupported,
    uint32_t *calls, size_t resource_count)
{
    const auto *owner = static_cast<const HostStandby *>(context);
    if (owner == nullptr || values == nullptr || applied == nullptr || consistent == nullptr ||
        unsupported == nullptr || calls == nullptr) {
        return reject_operation("read buffers", ESP_ERR_INVALID_ARG);
    }
    if (count != 12 || resource_count != kResourceCount) {
        return reject_operation("read buffer sizes", ESP_ERR_INVALID_SIZE);
    }
    values[0] = owner->standby;
    values[1] = owner->incomplete;
    values[2] = owner->error;
    values[3] = owner->rollback_error;
    values[4] = owner->recovery_error;
    values[5] = owner->attempted.storage != ESP_OK ? owner->attempted.storage : owner->sequence.storage;
    values[6] = owner->attempted.wireless != ESP_OK ? owner->attempted.wireless : owner->sequence.wireless;
    values[7] = select_standby_sleep(owner->deep_sleep_enabled, owner->retained_connection,
        owner->voice_listening, owner->touch_wake_available) == StandbySleep::Deep;
    const auto &base_audio = owner->resources[static_cast<size_t>(StandbyStep::BaseAudio)];
    const auto &voice_afe = owner->resources[static_cast<size_t>(StandbyStep::VoiceAfe)];
    values[8] = (base_audio.consistent && base_audio.applied) ||
        (voice_afe.consistent && voice_afe.applied);
    values[9] = owner->resources[static_cast<size_t>(StandbyStep::Display)].applied;
    values[10] = owner->resources[static_cast<size_t>(StandbyStep::Display)].consistent;
    values[11] = owner->display_rollback_error;
    for (size_t index = 0; index < kResourceCount; ++index) {
        applied[index] = owner->resources[index].applied;
        consistent[index] = owner->resources[index].consistent;
        unsupported[index] = owner->resources[index].unsupported;
        calls[index] = owner->calls[index];
    }
    return ESP_OK;
}

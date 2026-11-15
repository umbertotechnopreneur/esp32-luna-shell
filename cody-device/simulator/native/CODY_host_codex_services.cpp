// Explicit host-side CCP boundary. No API here transmits a message or connects
// to a physical device. Absent simulated owners fail closed.
#include "LUNASHELL_codex_name.hpp"
#include "LUNASHELL_platform.h"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_snapshot_internal.hpp"
#include "CODY_host_onboarding_snapshot.hpp"
#include "esp_timer.h"

#include <atomic>
#include <cstring>
#include <mutex>

namespace {
std::mutex s_name_mutex;
LUNASHELL::CodexNameExecutor s_name_executor = nullptr;
std::mutex s_simulation_mutex;
int s_simulation_mode = 0;
std::atomic_bool s_studio_pairing_code{false};

// link: Synthetic commissioning state, synchronously copied into the canonical host snapshot.
// Exception: Propagates the canonical snapshot publication error.
esp_err_t publish_simulated_link(const LUNASHELL::CodexLinkSnapshot &link)
{
    return LUNASHELL::snapshot_detail::apply_snapshot_mutation(
        [](LUNASHELL::DeviceSnapshot &snapshot, void *context) {
            snapshot.radios.codex_link = *static_cast<const LUNASHELL::CodexLinkSnapshot *>(context);
        }, const_cast<LUNASHELL::CodexLinkSnapshot *>(&link),
        LUNASHELL::snapshot_detail::SnapshotPublicationPolicy::Required);
}
}

namespace CODY::host_onboarding {
// available: Explicit static Studio pairing-code source, independent of any link action.
// Exception: No exceptions; atomic admission never takes the canonical snapshot owner's lock.
void configure_studio_codex_pairing_code(bool available)
{
    s_studio_pairing_code.store(available, std::memory_order_release);
}
}

#if defined(_WIN32)
#define CODY_HOST_CODEX_API extern "C" __declspec(dllexport)
#else
#define CODY_HOST_CODEX_API extern "C" __attribute__((visibility("default")))
#endif

// mode: 1 enables an isolated pairing-code scenario; 2 makes pairing fail.
// Exception: Rejects unknown modes and preserves the native snapshot publication error.
CODY_HOST_CODEX_API esp_err_t cody_host_codex_simulation_configure(int mode)
{
    if (mode != 1 && mode != 2) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(s_simulation_mutex);
    LUNASHELL::CodexLinkSnapshot link{};
    const auto now = esp_timer_get_time();
    link.health = LUNASHELL::DeviceHealth::Ready;
    link.data_valid = true;
    link.last_error = ESP_OK;
    link.last_attempt_at_us = link.last_success_at_us = link.changed_at_us = now;
    const auto error = publish_simulated_link(link);
    if (error == ESP_OK) s_simulation_mode = mode;
    return error;
}

// executor: Actual settings-owner callback retained for a future host CCP owner.
// Exception: Rejects a null callback or any attempt to replace a different owner.
esp_err_t LUNASHELL::registerCodexNameExecutor(CodexNameExecutor executor)
{
    std::lock_guard<std::mutex> lock(s_name_mutex);
    if (!executor || (s_name_executor && s_name_executor != executor)) return ESP_ERR_INVALID_ARG;
    s_name_executor = executor;
    return ESP_OK;
}

// action: Exact requested link action, never sent to a network device.
// expected_attempt_id: Attempt identity supplied by the original controller.
// Exception: Without an explicitly selected scenario, no simulated owner is registered.
extern "C" esp_err_t LUNASHELL_codex_link_request(LUNASHELL_codex_link_action_t action,
    uint32_t expected_attempt_id)
{
    std::lock_guard<std::mutex> lock(s_simulation_mutex);
    if (!s_simulation_mode) return ESP_ERR_NOT_SUPPORTED;
    LUNASHELL::CodexTechnicalObservation observation{};
    const auto error = LUNASHELL::DeviceSnapshotService::instance().readCodexTechnicalObservation(observation);
    if (error != ESP_OK) return error;
    auto link = observation.link;
    if (link.attempt_id != expected_attempt_id) return ESP_ERR_INVALID_STATE;
    if (action == LUNASHELL_CODEX_LINK_ACTION_START_PAIRING ||
        action == LUNASHELL_CODEX_LINK_ACTION_REPLACE_PAIRING) {
        if (s_simulation_mode == 2) return ESP_FAIL;
        ++link.attempt_id;
        link.state = LUNASHELL::CodexLinkState::Pairing;
        link.pairing_window_active = true;
        link.pending_pairing_present = false;
        link.deadline_at_us = esp_timer_get_time() + 180000000LL;
    } else if (action == LUNASHELL_CODEX_LINK_ACTION_START_DISCOVERY) {
        link.state = LUNASHELL::CodexLinkState::Discovering;
        link.pairing_window_active = false;
    } else if (action == LUNASHELL_CODEX_LINK_ACTION_CANCEL ||
               action == LUNASHELL_CODEX_LINK_ACTION_FORGET_PAIRING) {
        link.state = LUNASHELL::CodexLinkState::Idle;
        link.pairing_window_active = false;
        link.pending_pairing_present = false;
        link.retained_link_present = false;
    } else {
        return ESP_ERR_INVALID_ARG;
    }
    link.command_pending = false;
    link.changed_at_us = link.last_attempt_at_us = link.last_success_at_us = esp_timer_get_time();
    return publish_simulated_link(link);
}

// buffer: Caller-owned code buffer, cleared before reporting unavailable.
// buffer_size: Exact writable buffer capacity.
// expected_attempt_id: Attempt identity, never replaced with a synthetic value.
// Exception: Rejects unavailable, stale, expired or undersized simulated requests.
extern "C" esp_err_t LUNASHELL_codex_link_copy_pairing_code(char *buffer, size_t buffer_size,
    uint32_t expected_attempt_id)
{
    if (!buffer || buffer_size == 0) return ESP_ERR_INVALID_ARG;
    buffer[0] = '\0';
    std::lock_guard<std::mutex> lock(s_simulation_mutex);
    if (s_simulation_mode != 1 && !s_studio_pairing_code.load(std::memory_order_acquire))
        return ESP_ERR_NOT_SUPPORTED;
    LUNASHELL::CodexTechnicalObservation observation{};
    const auto error = LUNASHELL::DeviceSnapshotService::instance().readCodexTechnicalObservation(observation);
    if (error != ESP_OK) return error;
    if (observation.link.last_error != ESP_OK) return observation.link.last_error;
    if (!observation.link.data_valid || observation.link.data_stale ||
        observation.link.attempt_id != expected_attempt_id ||
        observation.link.state != LUNASHELL::CodexLinkState::Pairing ||
        !observation.link.pairing_window_active || observation.link.deadline_at_us <= 0 ||
        esp_timer_get_time() >= observation.link.deadline_at_us) return ESP_ERR_INVALID_STATE;
    static constexpr char code[] = "SIM123";
    if (buffer_size < sizeof(code)) return ESP_ERR_INVALID_SIZE;
    std::memcpy(buffer, code, sizeof(code));
    return ESP_OK;
}

// request_id: Explicit event identity, never sent outside the simulation.
// Exception: Returns unavailable until a host event owner is connected.
extern "C" esp_err_t LUNASHELL_codex_event_select(const char *request_id)
{ return request_id ? ESP_ERR_NOT_SUPPORTED : ESP_ERR_INVALID_ARG; }

// request_id: Explicit event identity, never sent outside the simulation.
// choice_id: Explicit selection, never transmitted to another process.
// Exception: Returns unavailable until a host event owner is connected.
extern "C" esp_err_t LUNASHELL_codex_event_reply(const char *request_id, const char *choice_id)
{ return request_id && choice_id ? ESP_ERR_NOT_SUPPORTED : ESP_ERR_INVALID_ARG; }

// request_id: Explicit event identity, never sent outside the simulation.
// Exception: Returns unavailable until a host event owner is connected.
extern "C" esp_err_t LUNASHELL_codex_event_dismiss(const char *request_id)
{ return request_id ? ESP_ERR_NOT_SUPPORTED : ESP_ERR_INVALID_ARG; }

// offset: Requested simulated inbox cursor; no network page is requested.
// Exception: Returns unavailable until a host inbox owner is connected.
extern "C" esp_err_t LUNASHELL_codex_inbox_page(uint8_t offset)
{ (void)offset; return ESP_ERR_NOT_SUPPORTED; }

// request_id: Exact simulated notice identity, never sent to another process.
// Exception: Returns unavailable until a host inbox owner is connected.
extern "C" esp_err_t LUNASHELL_codex_inbox_dismiss(const char *request_id)
{ return request_id ? ESP_ERR_NOT_SUPPORTED : ESP_ERR_INVALID_ARG; }

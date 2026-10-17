#pragma once
#include <cstdint>
#include "esp_err.h"

namespace LUNASHELL {
enum class EndurancePhase : uint8_t {
    Empty, Running, ExternalPowerObserved, InterruptedUnknown, ShutdownRequested,
};
enum EnduranceFlag : uint8_t {
    StartUtcValid = 1, AliveUtcValid = 2, FirstWakeRecorded = 4,
    FirstWakeUtcValid = 8, SourceUnverified = 16,
};
struct BatteryEnduranceRecord {
    uint32_t session_id = 0;
    EndurancePhase phase = EndurancePhase::Empty;
    uint8_t flags = 0;
    // Last committed elapsed time is a lower bound, never an inferred death time.
    uint64_t elapsed_ms = 0;
    int64_t started_utc_s = 0;
    int64_t last_alive_utc_s = 0;
    uint64_t first_wake_elapsed_ms = 0;
    int64_t first_wake_utc_s = 0;
};
struct BatteryEnduranceSnapshot {
    BatteryEnduranceRecord current;
    BatteryEnduranceRecord previous;
    esp_err_t error = ESP_ERR_INVALID_STATE;
    bool initialized = false;
    bool source_current = false;
    int64_t observed_at_us = 0;
    int64_t persisted_at_us = 0;
};
static_assert(sizeof(BatteryEnduranceSnapshot) <= 128,
              "Endurance projection must remain bounded independently of full snapshots");

// standby: Existing power-loop state; no new task, timer or wake lease is created.
// Exception: Returns real optional recorder failures without changing power policy.
esp_err_t battery_endurance_tick(bool standby);
// No parameters: called only after a completed physical-display wake transition.
// Exception: Returns original sampling, persistence or publication errors.
esp_err_t battery_endurance_woke();
// requested: Mark an orderly off request before settings seal, or reopen after abort.
// Exception: Returns original persistence errors; a marker is not power-off evidence.
esp_err_t battery_endurance_power_off(bool requested);
} // namespace LUNASHELL

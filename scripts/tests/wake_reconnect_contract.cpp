// Real production helpers with fake RTOS boundaries and bounded snapshot projections.
#include "esp_err.h"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <limits>
#include <initializer_list>

// condition: Required result from the real helper under test.
// message: Actionable contract failure.
void require(bool condition, const char *message)
{
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
// tag: Actual component identity.
// format: Actual diagnostic format.
// arguments: Production diagnostic arguments consumed without suppressing warnings.
template<typename... Args>
void host_log(const char *tag, const char *format, Args&&... arguments)
{
    (void)tag; (void)format; ((void)arguments, ...);
}
#define ESP_LOGE(...) host_log(__VA_ARGS__)
#define ESP_LOGW(...) host_log(__VA_ARGS__)
#define ESP_LOGI(...) host_log(__VA_ARGS__)
#define pdMS_TO_TICKS(value) static_cast<TickType_t>(value)
using TickType_t = uint32_t;
constexpr int pdPASS = 1;

namespace wifi {
constexpr const char *TAG = "wifi-host";
constexpr uint32_t WIFI_RECONNECT_INITIAL_MS = 1000;
constexpr uint32_t WIFI_RECONNECT_MAX_MS = 30000;
using TimerHandle_t = int*;
int timer_storage;
TimerHandle_t s_reconnect_timer = &timer_storage;
TickType_t fake_now = 0, s_reconnect_scheduled_tick = 0;
bool s_initialized = true, s_driver_started = true, s_connect_requested = true;
bool s_provisioning_owns_connection = false;
std::atomic<bool> s_factory_reset_sealed{false};
struct Status {
    bool started = true, connected = false, scanning = false, reconnect_pending = true;
    uint32_t reconnect_delay_ms = 30000, event_sequence = 0;
    uint32_t connect_attempt_count = 0, reconnect_attempt_count = 0;
    esp_err_t last_error = ESP_OK;
} s_status;
esp_err_t fake_lock_error = ESP_OK;
unsigned timer_calls = 0, publications = 0, locks = 0, lifecycle_releases = 0;
bool accept_timer = true;
unsigned driver_calls = 0;
bool advance_tick_in_timer_command = false;
esp_err_t try_lock_error = ESP_OK;
constexpr esp_err_t ESP_ERR_WIFI_STATE = 0x3006;
constexpr int LUNASHELL_LIVENESS_WIFI = 1;
// operation: Production callback identity.
esp_err_t wifi_lifecycle_try_lock(const char *operation) { (void)operation; return try_lock_error; }
// owner: Production watchdog identity.
esp_err_t LUNASHELL_liveness_watch_begin(int owner) { (void)owner; return ESP_OK; }
// owner: Production watchdog identity.
// error: Real driver result propagated by the callback.
esp_err_t LUNASHELL_liveness_watch_end(int owner, esp_err_t error) { (void)owner; return error; }
// No parameters: observe actual reconnect admission without a radio driver.
esp_err_t esp_wifi_connect() { ++driver_calls; return ESP_OK; }
// No parameters: deterministic fake RTOS time, including wraparound.
TickType_t xTaskGetTickCount() { return fake_now; }
// operation: Production operation identity.
esp_err_t wifi_lifecycle_lock(const char *operation) { (void)operation; return fake_lock_error; }
struct WifiLifecycleGuard {
    // No parameters: record every successful lifecycle lock release.
    ~WifiLifecycleGuard() { ++lifecycle_releases; }
};
// No parameters: expose unbalanced status locking.
void wifi_lock() { ++locks; }
// No parameters: expose unbalanced status locking.
void wifi_unlock() { require(locks == 1, "status unlock without lock"); --locks; }
// No parameters: canonical publication boundary.
void publish_snapshot() { ++publications; }
// error: Original failure from the owner lock.
void record_error(esp_err_t error) { s_status.last_error = error; ++publications; }
// timer: Existing one-shot timer identity.
// period: Requested bounded retry period.
// wait: Must remain nonblocking; timer queue rejection stays observable.
int xTimerChangePeriod(TimerHandle_t timer, TickType_t period, TickType_t wait)
{
    require(timer == &timer_storage && period >= 1000 && period <= 30000 && wait == 0,
            "bounded timer command");
    ++timer_calls;
    if (advance_tick_in_timer_command) { ++fake_now; }
    return accept_timer ? pdPASS : 0;
}
#include "wake_wifi_under_test.inc"
// No parameters: isolated owner state for each admission scenario.
void reset()
{
    s_status = {}; fake_now = 5000; s_reconnect_scheduled_tick = 0;
    s_initialized = s_driver_started = s_connect_requested = true;
    s_provisioning_owns_connection = false; s_factory_reset_sealed = false;
    s_reconnect_timer = &timer_storage; fake_lock_error = ESP_OK; accept_timer = true;
    timer_calls = publications = locks = lifecycle_releases = 0;
    driver_calls = 0; try_lock_error = ESP_OK; advance_tick_in_timer_command = false;
}
// No parameters: executes production admission and wrap-safe deadline logic.
void test()
{
    reset();
    require(LUNASHELL_wifi_expedite_reconnect() == ESP_OK && timer_calls == 1,
            "long backoff expedited exactly once");
    require(s_reconnect_scheduled_tick == 5000 && s_status.reconnect_delay_ms == 1000,
            "accepted retry owns new deadline");
    require(reconnect_deadline_pending_locked(), "stale callback must retain new pending deadline");
    reconnect_timer_callback(s_reconnect_timer);
    require(s_status.reconnect_pending && driver_calls == 0 && timer_calls == 1,
            "stale callback with owner lock preserves newly accepted retry");
    try_lock_error = ESP_ERR_TIMEOUT;
    reconnect_timer_callback(s_reconnect_timer);
    require(s_status.reconnect_pending && driver_calls == 0 && timer_calls == 1,
            "stale callback with busy owner preserves newly accepted retry");
    try_lock_error = ESP_OK;
    require(LUNASHELL_wifi_expedite_reconnect() == ESP_OK && timer_calls == 1,
            "repeated wake does not postpone imminent timer");
    fake_now = 6000;
    require(!reconnect_deadline_pending_locked(), "genuinely due callback admitted");
    reconnect_timer_callback(s_reconnect_timer);
    require(driver_calls == 1 && !s_status.reconnect_pending && s_status.connect_attempt_count == 1 &&
            s_status.reconnect_attempt_count == 1, "due callback executes exactly one driver attempt");
    reconnect_timer_callback(s_reconnect_timer);
    require(driver_calls == 1, "duplicate expired callback cannot reconnect twice");
    reset(); advance_tick_in_timer_command = true;
    require(schedule_reconnect(10000) == ESP_OK && s_reconnect_scheduled_tick == 5000 && fake_now == 5001 &&
            s_status.reconnect_delay_ms == 10000, "accepted scheduling owns pre-command timestamp");
    accept_timer = false;
    require(schedule_reconnect(20000) == ESP_ERR_TIMEOUT && s_reconnect_scheduled_tick == 5000 &&
            s_status.reconnect_delay_ms == 10000 && s_status.reconnect_pending,
            "rejected normal scheduling retains accepted deadline");
    reset(); fake_now = 30000; try_lock_error = ESP_ERR_TIMEOUT;
    reconnect_timer_callback(s_reconnect_timer);
    require(timer_calls == 1 && driver_calls == 0 && s_status.reconnect_pending &&
            s_status.reconnect_delay_ms == 1000 && s_reconnect_scheduled_tick == 30000,
            "due callback with busy lifecycle records real deferred deadline");
    for (unsigned scenario = 0; scenario != 10; ++scenario) {
        reset();
        switch (scenario) {
        case 0: s_initialized = false; break;
        case 1: s_driver_started = false; break;
        case 2: s_status.started = false; break;
        case 3: s_connect_requested = false; break;
        case 4: s_status.connected = true; break;
        case 5: s_status.scanning = true; break;
        case 6: s_provisioning_owns_connection = true; break;
        case 7: s_factory_reset_sealed = true; break;
        case 8: s_status.reconnect_pending = false; break;
        case 9: fake_now = 30000; break;
        }
        require(LUNASHELL_wifi_expedite_reconnect() == ESP_OK && timer_calls == 0 &&
                publications == 0 && locks == 0 && lifecycle_releases == 1,
                "ineligible wake is owner-balanced no-op");
    }
    reset(); fake_now = 29200;
    require(LUNASHELL_wifi_expedite_reconnect() == ESP_OK && timer_calls == 0,
            "800ms retry must not be delayed to 1000ms");
    reset(); accept_timer = false;
    require(LUNASHELL_wifi_expedite_reconnect() == ESP_ERR_TIMEOUT &&
            s_status.last_error == ESP_ERR_TIMEOUT && publications == 1 &&
            s_reconnect_scheduled_tick == 0 && s_status.reconnect_delay_ms == 30000 &&
            s_status.reconnect_pending && s_status.connect_attempt_count == 0 &&
            s_status.reconnect_attempt_count == 0, "queue rejection retains previous retry and real error");
    reset(); s_reconnect_timer = nullptr;
    require(LUNASHELL_wifi_expedite_reconnect() == ESP_ERR_INVALID_STATE && timer_calls == 0,
            "missing timer retains invalid-state error");
    reset(); fake_lock_error = ESP_ERR_NO_MEM;
    require(LUNASHELL_wifi_expedite_reconnect() == ESP_ERR_NO_MEM && lifecycle_releases == 0 &&
            s_status.last_error == ESP_ERR_NO_MEM, "original lifecycle error survives");
    reset(); s_reconnect_scheduled_tick = std::numeric_limits<TickType_t>::max() - 499;
    fake_now = 100; s_status.reconnect_delay_ms = 1000;
    require(reconnect_deadline_pending_locked(), "pending deadline crosses tick wrap");
    fake_now = 500;
    require(!reconnect_deadline_pending_locked(), "deadline expires exactly across wrap");
}
}

namespace ccp {
namespace LUNASHELL {
enum class DeviceHealth { Unknown, Ready, Degraded, Missing };
enum class DeviceOperatingState { Active, EnteringStandby, Standby, Waking };
struct Wifi {
    bool data_valid = true, data_stale = false, started = true;
    DeviceHealth health = DeviceHealth::Ready;
    esp_err_t last_error = ESP_OK;
    bool connected = true, ip_acquired = true;
    char ip_address[16] = "192.0.2.1", mac_address[18] = "00:11:22:33:44:55";
};
struct ConnectivityObservation {
    struct { Wifi wifi; } radios;
    DeviceOperatingState operating_state = DeviceOperatingState::Waking;
};
}
enum class State { Idle, StartingPairing, Pairing, Paired, Discovering, Connected,
                   Failed, TimedOut, AwaitingConfirmation };
enum class Command { None, StartPairing, StartDiscovery, Cancel, ForgetPairing };
constexpr const char *kTag = "ccp-host";
std::mutex s_mutex;
int task_storage;
int *s_worker = &task_storage;
Command s_pending = Command::None;
bool s_link_present = true, s_candidate_present = false, s_network_ready_pending = false;
uint32_t s_network_ready_attempt_id = 0;
struct { State state = State::Discovering; uint32_t attempt_id = 1; } s_link_snapshot;
int64_t s_next_find_at_us = 9999;
unsigned notifications = 0, publications = 0;
bool accept_notification = true;
esp_err_t published_error = ESP_OK;
// task: Existing worker; no new task or periodic polling is created.
int xTaskNotifyGive(int *task)
{
    require(task == &task_storage, "notify existing CCP worker");
    ++notifications;
    return accept_notification ? pdPASS : 0;
}
// state: Existing protocol state, preserved when a scheduling hint fails.
// error: Canonical scheduling error.
void publishLocked(State state, esp_err_t error)
{
    require(state == s_link_snapshot.state, "hint cannot replace protocol state");
    ++publications; published_error = error;
}
#include "wake_ccp_under_test.inc"
// No parameters: isolated retained attempt and scheduler state.
void reset()
{
    s_worker = &task_storage; s_pending = Command::None;
    s_link_present = true; s_candidate_present = false; s_network_ready_pending = false;
    s_network_ready_attempt_id = 0; s_link_snapshot = {State::Discovering, 1};
    s_next_find_at_us = 9999; notifications = publications = 0;
    accept_notification = true; published_error = ESP_OK;
}
// No parameters: production canonical readiness and attempt-scoped admission cases.
void test()
{
    LUNASHELL::ConnectivityObservation observation;
    require(wifiOnline(observation), "valid retained Wi-Fi is online while Waking");
    for (unsigned scenario = 0; scenario != 9; ++scenario) {
        observation = {};
        auto &radio = observation.radios.wifi;
        switch (scenario) {
        case 0: radio.data_valid = false; break;
        case 1: radio.data_stale = true; break;
        case 2: radio.started = false; break;
        case 3: radio.health = LUNASHELL::DeviceHealth::Degraded; break;
        case 4: radio.last_error = ESP_FAIL; break;
        case 5: radio.connected = false; break;
        case 6: radio.ip_acquired = false; break;
        case 7: radio.ip_address[0] = '\0'; break;
        case 8: radio.mac_address[0] = '\0'; break;
        }
        require(!wifiOnline(observation), "power transition cannot fabricate radio readiness");
    }
    for (Command command : {Command::StartPairing, Command::StartDiscovery,
                            Command::Cancel, Command::ForgetPairing}) {
        reset(); s_pending = command;
        require(LUNASHELL_codex_link_notify_network_ready() == ESP_OK && notifications == 0 &&
                s_pending == command, "explicit command has priority");
    }
    for (State state : {State::Idle, State::StartingPairing, State::Pairing, State::Paired,
                        State::Failed, State::TimedOut, State::AwaitingConfirmation}) {
        reset(); s_link_snapshot.state = state;
        require(LUNASHELL_codex_link_notify_network_ready() == ESP_OK && notifications == 0,
                "hint does not enter dormant or pairing state");
    }
    reset(); s_link_present = false;
    require(LUNASHELL_codex_link_notify_network_ready() == ESP_OK && notifications == 0,
            "no retained identity means no discovery hint");
    s_candidate_present = true;
    require(LUNASHELL_codex_link_notify_network_ready() == ESP_OK && notifications == 1,
            "existing candidate is eligible");
    for (State state : {State::Discovering, State::Connected}) {
        reset(); s_link_snapshot.state = state;
        require(LUNASHELL_codex_link_notify_network_ready() == ESP_OK &&
                LUNASHELL_codex_link_notify_network_ready() == ESP_OK && notifications == 1,
                "same-attempt notification coalesces");
        require(s_next_find_at_us == 9999, "caller only queues scheduling hint");
        consumeNetworkReadyLocked();
        require(s_next_find_at_us == 0 && !s_network_ready_pending &&
                s_link_snapshot.state == state && publications == 0,
                "worker expedites scheduling without claiming authenticated connection");
    }
    reset(); LUNASHELL_codex_link_notify_network_ready(); ++s_link_snapshot.attempt_id;
    consumeNetworkReadyLocked();
    require(s_next_find_at_us == 9999 && !s_network_ready_pending, "stale attempt hint discarded");
    reset(); LUNASHELL_codex_link_notify_network_ready(); ++s_link_snapshot.attempt_id;
    require(LUNASHELL_codex_link_notify_network_ready() == ESP_OK && notifications == 2 &&
            s_network_ready_attempt_id == s_link_snapshot.attempt_id,
            "fresh attempt can replace a queued stale hint");
    consumeNetworkReadyLocked();
    require(s_next_find_at_us == 0, "replacement hint expedites its own attempt");
    reset(); consumeNetworkReadyLocked();
    require(s_next_find_at_us == 9999, "ordinary worker tick does not force discovery without hint");
    reset(); LUNASHELL_codex_link_notify_network_ready(); s_pending = Command::Cancel;
    consumeNetworkReadyLocked();
    require(s_next_find_at_us == 9999 && s_pending == Command::Cancel,
            "new explicit command wins over previously queued hint");
    reset(); accept_notification = false;
    require(LUNASHELL_codex_link_notify_network_ready() == ESP_FAIL && published_error == ESP_FAIL &&
            publications == 1 && !s_network_ready_pending && s_next_find_at_us == 9999,
            "failed worker notify returns and publishes original error");
    reset(); s_worker = nullptr;
    require(LUNASHELL_codex_link_notify_network_ready() == ESP_ERR_INVALID_STATE,
            "missing worker remains unavailable");
}
}
// No parameters: all checks execute real extracted helper bodies, not policy replicas.
int main()
{
    wifi::test(); ccp::test();
    std::puts("Wake reconnect admission/deadline contracts passed");
    return 0;
}

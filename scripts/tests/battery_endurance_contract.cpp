#include "LUNASHELL_battery_endurance.hpp"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_settings.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
using namespace LUNASHELL;
int64_t now_us = 1000000;
BatterySnapshot battery;
ClockSnapshot clock_sample;
BatteryEnduranceSnapshot published;
std::vector<unsigned char> stored;
esp_err_t read_error = ESP_OK;
esp_err_t write_error = ESP_OK;
esp_err_t observation_error = ESP_OK;
unsigned writes = 0;

// condition: Actual real-service outcome required by the scenario.
// message: Failure evidence, emitted before returning a nonzero fixture status.
void require(bool condition, const char *message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

// milliseconds: Explicit advance of the fixture monotonic clock.
void advance(uint64_t milliseconds)
{
    now_us += static_cast<int64_t>(milliseconds * 1000);
    battery.last_success_at_us = now_us;
    clock_sample.last_success_at_us = now_us;
    clock_sample.utc_epoch_seconds += static_cast<int64_t>(milliseconds / 1000);
}

// No parameters: inject a valid measured battery-only canonical observation.
void battery_only()
{
    battery = {};
    battery.health = DeviceHealth::Ready;
    battery.last_error = ESP_OK;
    battery.data_valid = true;
    battery.battery_present = true;
    battery.batfet_enabled = true;
    battery.battery_active_mode = true;
    battery.battery_mv_valid = true;
    battery.battery_mv = 3900;
    battery.last_success_at_us = now_us;
}

// No parameters: begin the real recorder from a valid battery-only observation.
void start()
{
    battery_only();
    require(battery_endurance_tick(false) == ESP_OK, "valid battery start failed");
    require(published.current.phase == EndurancePhase::Running, "battery did not start session");
    require(published.current.session_id != 0, "session has no identity");
    require(stored.size() == 112, "journal is not the fixed 112-byte format");
}

// path: Artifact-owned persistence image supplied by the runner.
void load(const char *path)
{
    std::ifstream stream(path, std::ios::binary);
    require(stream.good(), "seed journal missing");
    stored.assign(std::istreambuf_iterator<char>(stream), {});
}
}

// No parameters: deterministic monotonic time transport.
int64_t esp_timer_get_time() { return now_us; }

namespace LUNASHELL {
// No parameters: returns the fixture settings transport.
UserSettingsService &UserSettingsService::instance() { static UserSettingsService service; return service; }
// No parameters: settings initialization is available in these scenarios.
esp_err_t UserSettingsService::initialize() { return ESP_OK; }
// key: Production persistence identity, required to be nonempty.
// data: Fixed destination, unchanged on failure.
// size: Real service's exact format length.
esp_err_t UserSettingsService::getBlobExact(const char *key, void *data, size_t size) const
{
    require(key != nullptr && key[0] != '\0', "empty journal key");
    if (read_error != ESP_OK) return read_error;
    if (stored.empty()) return ESP_ERR_NVS_NOT_FOUND;
    if (stored.size() != size) return ESP_ERR_INVALID_SIZE;
    std::memcpy(data, stored.data(), size);
    return ESP_OK;
}
// key: Production persistence identity.
// data: Complete real-service serialized bytes; the fixture does not encode records.
// size: Fixed transaction length.
esp_err_t UserSettingsService::setBlob(const char *key, const void *data, size_t size)
{
    require(key != nullptr && key[0] != '\0', "empty write key");
    ++writes;
    if (write_error != ESP_OK) return write_error;
    const auto *bytes = static_cast<const unsigned char *>(data);
    stored.assign(bytes, bytes + size);
    return ESP_OK;
}
// No parameters: canonical transport fake, with no sensor or recorder policy.
DeviceSnapshotService &DeviceSnapshotService::instance() { static DeviceSnapshotService service; return service; }
// destination: Borrowed fixture measurement copied only after successful transport.
esp_err_t DeviceSnapshotService::readBatteryObservation(BatterySnapshot &destination) const
{
    if (observation_error != ESP_OK) return observation_error;
    destination = battery;
    return ESP_OK;
}
// No parameters: the fixture supplies the next canonical owner acquisition result.
esp_err_t DeviceSnapshotService::refreshBatteryForEndurance() { return observation_error; }
// report: Real recorder result retained for assertions.
esp_err_t DeviceSnapshotService::reportBatteryEndurance(const BatteryEnduranceSnapshot &report)
{
    published = report;
    return ESP_OK;
}
// clock: Fixture clock transport; validity remains a real-service policy decision.
esp_err_t DeviceSnapshotService::readClockObservation(ClockSnapshot &clock) const
{
    clock = clock_sample;
    return ESP_OK;
}
}

// argc: Scenario name and artifact-owned journal path are required.
// argv: Literal arguments passed by the public check runner.
int main(int argc, char **argv)
{
    using namespace LUNASHELL;
    require(argc == 3, "expected scenario and persistence path");
    const std::string scenario = argv[1];
    clock_sample.data_valid = true;
    clock_sample.health = DeviceHealth::Ready;
    clock_sample.last_error = ESP_OK;
    clock_sample.last_success_at_us = now_us;
    clock_sample.utc_epoch_seconds = 1800000000;
    if (scenario == "usb") {
        battery_only();
        battery.usb_present = true;
        battery.vbus_good = true;
        require(battery_endurance_tick(false) == ESP_OK, "USB observation failed");
        require(published.current.phase == EndurancePhase::Empty, "USB fabricated battery session");
        advance(60000);
        battery.usb_present = false;
        battery.vbus_good = false;
        require(battery_endurance_tick(false) == ESP_OK, "unplug start failed");
        require(published.current.phase == EndurancePhase::Running, "later unplug not recorded");
        const auto battery_session = published.current.session_id;
        advance(1000);
        battery.usb_present = true;
        battery.vbus_good = false;
        require(battery_endurance_tick(false) == ESP_OK, "poor-voltage USB observation failed");
        require(published.current.phase == EndurancePhase::ExternalPowerObserved,
                "USB connection with poor VBUS did not end battery-only session");
        advance(1000);
        require(battery_endurance_tick(false) == ESP_OK, "continued USB observation failed");
        require(published.current.session_id == battery_session &&
                published.current.phase == EndurancePhase::ExternalPowerObserved,
                "connected USB restarted battery-only session");
        advance(1000);
        battery.usb_present = false;
        require(battery_endurance_tick(false) == ESP_OK, "second unplug start failed");
        require(published.current.phase == EndurancePhase::Running &&
                published.current.session_id > battery_session &&
                published.previous.session_id == battery_session,
                "second unplug did not create a distinct session with preserved history");
    } else if (scenario == "unknown") {
        (void)battery_endurance_tick(false);
        require(published.current.session_id == 0, "unknown input fabricated session");
        advance(60000);
        battery_only();
        battery.data_stale = true;
        (void)battery_endurance_tick(false);
        require(published.current.session_id == 0, "stale input fabricated session");
        advance(60000);
        battery.data_stale = false;
        battery.battery_present = false;
        (void)battery_endurance_tick(false);
        require(published.current.session_id == 0, "absent cell fabricated session");
        advance(60000);
        start();
        advance(10000);
        battery.data_stale = true;
        (void)battery_endurance_tick(false);
        advance(1000);
        battery.data_stale = false;
        require(battery_endurance_tick(false) == ESP_OK, "valid observation after gap failed");
        advance(49000);
        require(battery_endurance_tick(true) == ESP_OK, "heartbeat after source gap failed");
        require((published.current.flags & SourceUnverified) != 0,
                "intervening stale source was erased by a later valid heartbeat");
        advance(1000);
        require(battery_endurance_woke() == ESP_OK, "first wake after source gap failed");
        require((published.current.flags & SourceUnverified) != 0 &&
                (published.current.flags & FirstWakeRecorded) != 0,
                "first wake discarded the uncertain-source history");
    } else if (scenario == "crc" || scenario == "type" || scenario == "oversize") {
        load(argv[2]);
        if (scenario == "crc") stored[20] ^= 0x80;
        if (scenario == "type") read_error = ESP_ERR_NVS_TYPE_MISMATCH;
        if (scenario == "oversize") stored.resize(113);
        const auto before = stored;
        battery_only();
        require(battery_endurance_tick(false) != ESP_OK, "corrupt journal silently admitted");
        require(writes == 0 && stored == before, "invalid journal overwritten");
    } else if (scenario == "reboot") {
        load(argv[2]);
        battery_only();
        require(battery_endurance_tick(false) == ESP_OK, "reboot recovery failed");
        require(published.previous.phase == EndurancePhase::InterruptedUnknown, "reboot guessed death cause");
        require(published.previous.elapsed_ms == 60000, "reboot changed last-alive lower bound");
        require(published.current.session_id != published.previous.session_id, "reboot reused session identity");
    } else {
        if (scenario == "clock-unavailable") clock_sample = {};
        start();
        const unsigned initial_writes = writes;
        if (scenario == "heartbeat" || scenario == "seed") {
            advance(59000);
            require(battery_endurance_tick(true) == ESP_OK, "standby tick failed");
            require(writes == initial_writes, "heartbeat wrote before 60 seconds");
            advance(1000);
            require(battery_endurance_tick(true) == ESP_OK, "60-second heartbeat failed");
            require(published.current.elapsed_ms == 60000, "standby excluded from elapsed time");
            require(writes == initial_writes + 1, "heartbeat not durably committed");
            if (scenario == "seed") {
                std::ofstream stream(argv[2], std::ios::binary | std::ios::trunc);
                stream.write(reinterpret_cast<const char *>(stored.data()), static_cast<std::streamsize>(stored.size()));
                require(stream.good(), "seed persistence write failed");
            }
        } else if (scenario == "wake") {
            advance(12000);
            require(battery_endurance_woke() == ESP_OK, "first completed wake failed");
            require(writes == initial_writes + 1, "first wake not immediate durable write");
            require((published.current.flags & FirstWakeRecorded) != 0, "first wake marker missing");
            require(published.current.first_wake_elapsed_ms == 12000, "first wake elapsed incorrect");
            advance(1000);
            require(battery_endurance_woke() == ESP_OK, "second wake failed");
            require(published.current.first_wake_elapsed_ms == 12000, "second wake replaced first");
        } else if (scenario == "shutdown") {
            advance(10000);
            require(battery_endurance_power_off(true) == ESP_OK, "shutdown marker failed");
            require(published.current.phase == EndurancePhase::ShutdownRequested, "shutdown claimed actual death");
            advance(5000);
            require(battery_endurance_power_off(false) == ESP_OK, "cancel shutdown failed");
            require(published.current.phase == EndurancePhase::Running, "cancel did not reopen session");
            require(published.current.elapsed_ms == 15000, "cancel lost elapsed time");
        } else if (scenario == "write-error") {
            const auto before = stored;
            const auto elapsed = published.current.elapsed_ms;
            write_error = ESP_FAIL;
            advance(60000);
            require(battery_endurance_tick(false) == ESP_FAIL, "persistence failure hidden");
            require(stored == before, "failed write changed committed journal");
            require(published.current.elapsed_ms == elapsed, "uncommitted elapsed published as durable");
        } else if (scenario == "clock-unavailable") {
            advance(60000);
            require(battery_endurance_tick(true) == ESP_OK, "clockless monotonic heartbeat failed");
            require((published.current.flags & (StartUtcValid | AliveUtcValid)) == 0, "clockless UTC fabricated");
            require(published.current.started_utc_s == 0 && published.current.last_alive_utc_s == 0, "clockless UTC fields populated");
        } else require(false, "unknown scenario");
    }
    std::printf("PASS %s\n", scenario.c_str());
    return 0;
}

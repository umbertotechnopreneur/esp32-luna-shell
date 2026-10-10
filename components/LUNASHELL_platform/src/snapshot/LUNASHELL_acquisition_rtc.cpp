#include "LUNASHELL_acquisition_rtc.hpp"
#include "LUNASHELL_acquisition_internal.hpp"
#include "LUNASHELL_acquisition_i2c.hpp"
#include "LUNASHELL_rtc_calendar.hpp"

#include <atomic>
#include <mutex>
#include <cstring>
#include "esp_bit_defs.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace LUNASHELL::snapshot_detail::rtc {
using namespace acquisition_i2c;
using RtcLatchObservation = LatchObservation;
namespace {
constexpr char TAG[] = "LUNASHELL_rtc";
constexpr uint8_t PCF85063_ADDRESS = 0x51;
constexpr uint8_t PCF85063_REG_CTRL1 = 0x00;
constexpr uint8_t PCF85063_REG_RAM = 0x03;
constexpr uint8_t PCF85063_REG_SECONDS = 0x04;
i2c_master_dev_handle_t s_rtc;
bool s_rtc_ready;
std::atomic_bool s_rtc_oscillator_stop_latched{false};
std::atomic_bool s_rtc_recovery_readback_required{false};
std::atomic<uint32_t> s_rtc_trusted_write_generation{0};
std::atomic<esp_err_t> s_rtc_persistence_error_latched{ESP_OK};
std::atomic<int64_t> s_rtc_persistence_failure_at_us{0};
std::mutex s_rtc_operation_mutex;

// time: Calendar acquired by the RTC owner.
// read_error: Original hardware/calendar result.
// publication_error: Result of publishing the same acquisition.
// trusted_write_generation_at_read: Generation rejecting observations superseded by a write.
RtcLatchObservation make_rtc_latch_observation(
    const LUNASHELL_calendar_time_t &time,
    esp_err_t read_error,
    esp_err_t publication_error,
    uint32_t trusted_write_generation_at_read
)
{
    RtcLatchObservation observation;
    if (publication_error != ESP_OK) {
        return observation;
    }
    if (read_error != ESP_ERR_INVALID_STATE ||
        !LUNASHELL::rtc_calendar::valid_rtc_register_calendar(time) ||
        time.clock_integrity) {
        return observation;
    }

    observation.valid = true;
    observation.oscillator_stop = true;
    observation.trusted_write_generation_at_read =
        trusted_write_generation_at_read;
    return observation;
}

// observation: Commit-gated integrity observation; caller holds the RTC operation lock.
void apply_rtc_latch_observation_locked(
    const RtcLatchObservation &observation
)
{
    if (!observation.valid) {
        return;
    }
    if (!observation.oscillator_stop) {
        return;
    }

    if (s_rtc_trusted_write_generation.load(std::memory_order_acquire) !=
        observation.trusted_write_generation_at_read) {
        // A trusted write superseded this sample. Do not touch the latch: a
        // newer read may already own a valid oscillator-stop observation.
        return;
    }
    s_rtc_oscillator_stop_latched.store(true, std::memory_order_release);
}

// observation: Committed integrity observation, serialized against trusted writes.
void apply_rtc_latch_observation(const RtcLatchObservation &observation)
{
    // Pair the generation comparison with the latch update. RTC reads and
    // trusted writes use the same mutex, so a stale observation can neither
    // relatch after a write nor clear a newer observation.
    std::lock_guard<std::mutex> rtc_lock(s_rtc_operation_mutex);
    apply_rtc_latch_observation_locked(observation);
}

// observation: Original oscillator-stop read requiring verified trusted recovery.
void require_rtc_recovery_readback(
    const RtcLatchObservation &observation
)
{
    if (observation.valid && observation.oscillator_stop) {
        // This is intentionally immediate rather than commit-gated. A valid
        // GNSS fix can write the RTC later in the same staged Full Snapshot;
        // that write must verify recovery even before the persistent latch is
        // applied after snapshot commit.
        s_rtc_recovery_readback_required.store(
            true,
            std::memory_order_release
        );
    }
}

// bus: Shared BSP I2C bus retained by its owner.
// Exception: Returns the original initialization or RAM restoration failure.
#if CONFIG_LUNASHELL_BUILD_ENABLE_RTC
esp_err_t init_rtc(i2c_master_bus_handle_t bus)
{
    esp_err_t error = add_i2c_device(bus, PCF85063_ADDRESS, 400000, &s_rtc);
    uint8_t ram_original = 0;
    if (error == ESP_OK) {
        error = read_register(s_rtc, PCF85063_REG_RAM, &ram_original, 1);
    }
    const bool ram_original_valid = error == ESP_OK;
    uint8_t ram_test = ram_original;
    if (error == ESP_OK) {
        error = write_register(s_rtc, PCF85063_REG_RAM, static_cast<uint8_t>(ram_original ^ BIT(7)));
    }
    if (error == ESP_OK) {
        error = read_register(s_rtc, PCF85063_REG_RAM, &ram_test, 1);
    }
    if (ram_original_valid) {
        const esp_err_t restore_error =
            write_register(s_rtc, PCF85063_REG_RAM, ram_original);
        if (restore_error != ESP_OK) {
            ESP_LOGE(
                TAG,
                "RTC PCF85063 RAM probe restore failed (%s)",
                esp_err_to_name(restore_error)
            );
            record_first_error(restore_error, &error);
        }
    }
    if (error == ESP_OK && ((ram_test ^ ram_original) & BIT(7)) == 0) {
        error = ESP_ERR_NOT_FOUND;
    }

    uint8_t ctrl1 = 0;
    if (error == ESP_OK) {
        error = read_register(s_rtc, PCF85063_REG_CTRL1, &ctrl1, 1);
    }
    if (error == ESP_OK) {
        ctrl1 &= static_cast<uint8_t>(~(BIT(5) | BIT(1)));
        error = write_register(s_rtc, PCF85063_REG_CTRL1, ctrl1);
    }
    s_rtc_ready = error == ESP_OK;
    if (s_rtc_ready) {
        s_rtc_oscillator_stop_latched.store(
            false,
            std::memory_order_release
        );
        s_rtc_recovery_readback_required.store(
            false,
            std::memory_order_release
        );
        s_rtc_persistence_error_latched.store(
            ESP_OK,
            std::memory_order_release
        );
        s_rtc_persistence_failure_at_us.store(
            0,
            std::memory_order_release
        );
    }
    update_snapshot([error](LUNASHELL::DeviceSnapshot &snapshot) {
        set_component(snapshot.rtc, error == ESP_OK, error);
    });
    if (!s_rtc_ready) {
        ESP_LOGW(TAG, "RTC PCF85063: initialization failed (%s)", esp_err_to_name(error));
        return error;
    }
    ESP_LOGI(TAG, "RTC PCF85063: detected, 24-hour mode configured");
    return ESP_OK;
}

#endif

// time: Required output for an actual calendar register acquisition.
// Exception: Returns admission, I2C or calendar-integrity failure.
esp_err_t read_rtc_calendar(LUNASHELL_calendar_time_t *time)
{
    if (time == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_rtc_ready || s_rtc == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t raw[7] = {};
    esp_err_t error =
        read_register(s_rtc, PCF85063_REG_SECONDS, raw, sizeof(raw));
    if (error != ESP_OK) {
        return error;
    }

    return LUNASHELL::rtc_calendar::decode_registers(raw, *time);
}

// time: Original calendar; untrusted diagnostics remain explicit.
// error: Original acquisition outcome controlling validity.
// attempted_at_us: Original hardware timestamp.
// Exception: Returns the canonical publication error.
esp_err_t publish_rtc_read(
    const LUNASHELL_calendar_time_t &time,
    esp_err_t error,
    int64_t attempted_at_us
)
{
    if (error != ESP_OK) {
        return update_snapshot(
            [time, error, attempted_at_us](LUNASHELL::DeviceSnapshot &snapshot) {
                if (error == ESP_ERR_INVALID_STATE &&
                    LUNASHELL::rtc_calendar::valid_rtc_register_calendar(time)) {
                    snapshot.rtc.clock_integrity = time.clock_integrity;
                    snapshot.rtc.second = time.second;
                    snapshot.rtc.minute = time.minute;
                    snapshot.rtc.hour = time.hour;
                    snapshot.rtc.day = time.day;
                    snapshot.rtc.month = time.month;
                    snapshot.rtc.year = time.year;
                }
                mark_sample_failure(
                    snapshot.rtc,
                    error,
                    attempted_at_us
                );
            }
        );
    }

    LUNASHELL::RtcSnapshot sample;
    sample.clock_integrity = time.clock_integrity;
    sample.second = time.second;
    sample.minute = time.minute;
    sample.hour = time.hour;
    sample.day = time.day;
    sample.month = time.month;
    sample.year = time.year;
    mark_sample_success(sample, attempted_at_us);
    const esp_err_t persistence_error =
        s_rtc_persistence_error_latched.load(std::memory_order_acquire);
    const int64_t persistence_failure_at_us =
        s_rtc_persistence_failure_at_us.load(std::memory_order_acquire);
    return update_snapshot(
        [&sample, persistence_error, persistence_failure_at_us](
            LUNASHELL::DeviceSnapshot &snapshot
        ) {
            const LUNASHELL::ComponentSnapshot system_time =
                snapshot.rtc.system_time;
            const bool system_time_valid =
                snapshot.rtc.system_time_valid;
            const bool synchronized = snapshot.rtc.synchronized;
            char source[sizeof(snapshot.rtc.source)] = {};
            char timezone[sizeof(snapshot.rtc.timezone)] = {};
            strlcpy(source, snapshot.rtc.source, sizeof(source));
            strlcpy(
                timezone,
                snapshot.rtc.timezone,
                sizeof(timezone)
            );
            snapshot.rtc = sample;
            snapshot.rtc.system_time = system_time;
            snapshot.rtc.system_time_valid = system_time_valid;
            snapshot.rtc.synchronized = synchronized;
            strlcpy(
                snapshot.rtc.source,
                source,
                sizeof(snapshot.rtc.source)
            );
            strlcpy(
                snapshot.rtc.timezone,
                timezone,
                sizeof(snapshot.rtc.timezone)
            );
            if (persistence_error != ESP_OK) {
                // A successful read of the old calendar does not prove that a
                // failed trusted write persisted. Keep the original failure
                // and attempt timestamp visible until a write commits.
                mark_sample_failure(
                    snapshot.rtc,
                    persistence_error,
                    persistence_failure_at_us
                );
            }
        }
    );
}

// phase: Caller-owned cadence/diagnostic identity.
// latch_observation: Optional commit-gated integrity output; never retained.
// Exception: Returns original acquisition or publication failure.
esp_err_t sample_rtc(
    const char *phase,
    RtcLatchObservation *latch_observation
)
{
    if (latch_observation != nullptr) {
        *latch_observation = {};
    }
    if (high_frequency_sample_phase(phase) &&
        s_rtc_oscillator_stop_latched.load(std::memory_order_acquire)) {
        // The previous physical read already proved the OS condition. Keep the
        // real degraded result without repeating I2C traffic or pretending a
        // new sample attempt occurred; a trusted write performs the recovery.
        return ESP_ERR_INVALID_STATE;
    }
    // Serialize the complete read/observation sequence with direct reads and
    // trusted write+readback transactions. The GNSS path calls rtc_write later
    // in the same refresh, after this guard has been released.
    std::lock_guard<std::mutex> rtc_lock(s_rtc_operation_mutex);
    if (high_frequency_sample_phase(phase) &&
        s_rtc_oscillator_stop_latched.load(std::memory_order_acquire)) {
        return ESP_ERR_INVALID_STATE;
    }
    LUNASHELL_calendar_time_t time = {};
    const uint32_t trusted_write_generation_at_read =
        s_rtc_trusted_write_generation.load(std::memory_order_acquire);
    const int64_t attempted_at_us = esp_timer_get_time();
    const esp_err_t error = read_rtc_calendar(&time);
    const esp_err_t publication_error =
        publish_rtc_read(time, error, attempted_at_us);
    const RtcLatchObservation observation = make_rtc_latch_observation(
        time,
        error,
        publication_error,
        trusted_write_generation_at_read
    );
    require_rtc_recovery_readback(observation);
    if (latch_observation != nullptr) {
        *latch_observation = observation;
    }
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "RTC PCF85063: %s sample failed (%s)", phase, esp_err_to_name(error));
        return sample_publication_result(
            "RTC PCF85063",
            phase,
            error,
            publication_error
        );
    }
    if (high_frequency_sample_phase(phase)) {
        ESP_LOGD(
            TAG,
            "RTC PCF85063 %s: %04u-%02u-%02u %02u:%02u:%02u, integrity=%s",
            phase,
            time.year,
            time.month,
            time.day,
            time.hour,
            time.minute,
            time.second,
            time.clock_integrity ? "valid" : "clock-reset"
        );
    } else {
        ESP_LOGI(
            TAG,
            "RTC PCF85063 %s: %04u-%02u-%02u %02u:%02u:%02u, integrity=%s",
            phase,
            time.year,
            time.month,
            time.day,
            time.hour,
            time.minute,
            time.second,
            time.clock_integrity ? "valid" : "clock-reset"
        );
    }
    return sample_publication_result(
        "RTC PCF85063",
        phase,
        ESP_OK,
        publication_error
    );
}

}  // namespace

// bus: Shared BSP I2C bus retained by its owner.
// Exception: Returns the original initialization error or explicit exclusion.
esp_err_t initialize(i2c_master_bus_handle_t bus)
{
#if CONFIG_LUNASHELL_BUILD_ENABLE_RTC
    return init_rtc(bus);
#else
    (void)bus;
    update_snapshot([](DeviceSnapshot &snapshot) {
        set_component(snapshot.rtc, false, ESP_ERR_NOT_SUPPORTED);
    });
    ESP_LOGI(TAG, "RTC: disabled by build profile");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

// No parameters: expose acquisition readiness without accessing hardware.
bool ready()
{
    return s_rtc_ready;
}

// phase: Caller-owned cadence/diagnostic identity.
// observation: Optional integrity output applied only after the staged snapshot commits.
// Exception: Returns the original acquisition/publication failure.
esp_err_t sample(const char *phase, LatchObservation *observation)
{
    return sample_rtc(phase, observation);
}

// observation: Successful stage's original integrity observation.
void apply_committed_observation(const LatchObservation &observation)
{
    apply_rtc_latch_observation(observation);
}
}  // namespace LUNASHELL::snapshot_detail::rtc

using namespace LUNASHELL::snapshot_detail::rtc;

// time: Required output assigned only after hardware acquisition and publication succeed.
// sampled_at_us: Original timestamp of that same canonical sample.
// Exception: Returns admission, read or publication failure.
extern "C" esp_err_t LUNASHELL_rtc_read(LUNASHELL_calendar_time_t *time, int64_t *sampled_at_us)
{
    if (time == nullptr || sampled_at_us == nullptr) {
        ESP_LOGE(TAG, "RTC read rejected: output is null");
        return ESP_ERR_INVALID_ARG;
    }
    std::lock_guard<std::mutex> rtc_lock(s_rtc_operation_mutex);
    LUNASHELL_calendar_time_t sample = {};
    const uint32_t trusted_write_generation_at_read =
        s_rtc_trusted_write_generation.load(std::memory_order_acquire);
    const int64_t attempted_at_us = esp_timer_get_time();
    const esp_err_t error = read_rtc_calendar(&sample);
    const esp_err_t publication_error =
        publish_rtc_read(sample, error, attempted_at_us);
    const RtcLatchObservation observation = make_rtc_latch_observation(
        sample,
        error,
        publication_error,
        trusted_write_generation_at_read
    );
    require_rtc_recovery_readback(observation);
    apply_rtc_latch_observation_locked(observation);
    const esp_err_t result = LUNASHELL::snapshot_detail::sample_publication_result(
        "RTC PCF85063",
        "direct-read",
        error,
        publication_error
    );
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "RTC read failed (%s)", esp_err_to_name(error));
        return result;
    }
    if (result != ESP_OK) {
        return result;
    }
    *time = sample;
    *sampled_at_us = attempted_at_us;
    return ESP_OK;
}

// time: Explicit trusted UTC calendar verified by the sole acquisition owner.
// Exception: Returns original admission, write, readback, persistence or publication failure.
extern "C" esp_err_t LUNASHELL_rtc_write(const LUNASHELL_calendar_time_t *time)
{
    if (time == nullptr || !LUNASHELL::rtc_calendar::valid_calendar_time(*time)) {
        ESP_LOGE(TAG, "RTC write rejected: invalid UTC calendar");
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_rtc_ready || s_rtc == nullptr) {
        ESP_LOGE(TAG, "RTC write rejected: device is not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    // Keep the register write and conditional recovery readback indivisible:
    // SNTP and GNSS are independent writers and must not verify each other's
    // calendar or publish their results out of order.
    std::lock_guard<std::mutex> rtc_lock(s_rtc_operation_mutex);
    const uint8_t raw[7] = {
        LUNASHELL::rtc_calendar::decimal_to_bcd(time->second),
        LUNASHELL::rtc_calendar::decimal_to_bcd(time->minute),
        LUNASHELL::rtc_calendar::decimal_to_bcd(time->hour),
        LUNASHELL::rtc_calendar::decimal_to_bcd(time->day),
        0,
        LUNASHELL::rtc_calendar::decimal_to_bcd(time->month),
        LUNASHELL::rtc_calendar::decimal_to_bcd(static_cast<uint8_t>(time->year - 2000U)),
    };
    const int64_t attempted_at_us = esp_timer_get_time();
    const bool pending_recovery_at_start =
        s_rtc_recovery_readback_required.exchange(
            false,
            std::memory_order_acq_rel
        );
    bool recovery_readback_required =
        s_rtc_oscillator_stop_latched.load(std::memory_order_acquire) ||
        pending_recovery_at_start;
    esp_err_t error =
        write_registers(s_rtc, PCF85063_REG_SECONDS, raw, sizeof(raw));
    esp_err_t publication_error = ESP_OK;
    if (error == ESP_OK) {
        LUNASHELL_calendar_time_t published = *time;
        published.clock_integrity = true;
        const bool pending_recovery_after_write =
            s_rtc_recovery_readback_required.exchange(
                false,
                std::memory_order_acq_rel
            );
        recovery_readback_required =
            recovery_readback_required ||
            s_rtc_oscillator_stop_latched.load(
                std::memory_order_acquire
            ) ||
            pending_recovery_after_write;
        if (recovery_readback_required) {
            error = read_rtc_calendar(&published);
            if (error == ESP_OK &&
                !LUNASHELL::rtc_calendar::rtc_write_readback_matches(*time, published)) {
                ESP_LOGE(
                    TAG,
                    "RTC recovery readback mismatch: requested=%04u-%02u-%02u %02u:%02u:%02u observed=%04u-%02u-%02u %02u:%02u:%02u (%s)",
                    time->year,
                    time->month,
                    time->day,
                    time->hour,
                    time->minute,
                    time->second,
                    published.year,
                    published.month,
                    published.day,
                    published.hour,
                    published.minute,
                    published.second,
                    esp_err_to_name(ESP_ERR_INVALID_RESPONSE)
                );
                error = ESP_ERR_INVALID_RESPONSE;
            }
        }
        if (error == ESP_OK) {
            // Clear the old latch before publishing the newly committed write.
            // If publication itself fails, the result below restores a latch
            // with that real error and the same attempt timestamp.
            s_rtc_persistence_error_latched.store(
                ESP_OK,
                std::memory_order_release
            );
            s_rtc_persistence_failure_at_us.store(
                0,
                std::memory_order_release
            );
        }
        publication_error =
            publish_rtc_read(published, error, attempted_at_us);
    } else {
        LUNASHELL_calendar_time_t unavailable = {};
        publication_error =
            publish_rtc_read(unavailable, error, attempted_at_us);
    }

    if (error == ESP_OK && publication_error == ESP_OK) {
        s_rtc_trusted_write_generation.fetch_add(
            1,
            std::memory_order_acq_rel
        );
        s_rtc_oscillator_stop_latched.store(
            false,
            std::memory_order_release
        );
        ESP_LOGI(
            TAG,
            "RTC PCF85063 updated%s: %04u-%02u-%02u %02u:%02u:%02u UTC",
            recovery_readback_required ? " and verified" : "",
            time->year,
            time->month,
            time->day,
            time->hour,
            time->minute,
            time->second
        );
    } else if (error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "RTC %s failed (%s)",
            recovery_readback_required ? "write/recovery" : "write",
            esp_err_to_name(error)
        );
    }
    if ((error != ESP_OK || publication_error != ESP_OK) &&
        recovery_readback_required) {
        // A consumed recovery request is single-owner but not lossy: any
        // failed write, readback, or publication requires the next trusted
        // write to verify the hardware again.
        s_rtc_recovery_readback_required.store(
            true,
            std::memory_order_release
        );
    }
    const esp_err_t result = LUNASHELL::snapshot_detail::sample_publication_result(
        "RTC PCF85063",
        "write",
        error,
        publication_error
    );
    if (result != ESP_OK) {
        s_rtc_persistence_failure_at_us.store(
            attempted_at_us,
            std::memory_order_release
        );
        s_rtc_persistence_error_latched.store(
            result,
            std::memory_order_release
        );
    }
    return result;
}

/*
 * Host contract for Cody's real weekly usage-alarm projection.
 * The snapshots below are synthetic inputs; no device, storage or audio owner is called.
 * Copyright (c) 2026 Umberto Giacobbi
 */

#include "CODY_usage_alarm.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {

// condition: Expected externally observable result from the real alarm projection.
// message: Failure explanation identifying the broken behavior.
// Exception: Exits unsuccessfully when the condition is false.
void require(bool condition, const char *message)
{
    if (!condition) {
        std::fprintf(stderr, "Weekly usage alarm contract failed: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

struct Fixture {
    CODY::UsageAlarmState state{};
    CODY::UsageAlarmOptions options{};
    LUNASHELL::CodexWeeklyLimitSnapshot weekly{};
    LUNASHELL::ClockSnapshot clock{};

    // No parameters: starts with a current weekly window and 100% remaining.
    // Exception: None; fixture inputs are local synthetic snapshots only.
    Fixture()
    {
        options.enabled = true;
        weekly.health = LUNASHELL::DeviceHealth::Ready;
        weekly.last_error = ESP_OK;
        weekly.data_valid = true;
        weekly.remaining_basis_points = 10000;
        weekly.reset_unix_seconds = 2000;
        clock.health = LUNASHELL::DeviceHealth::Ready;
        clock.last_error = ESP_OK;
        clock.data_valid = true;
        clock.utc_epoch_seconds = 1000;
    }

    // No parameters: asks the actual header-only implementation for its pending notice.
    // Exception: Terminates this host contract if the implementation returns an error.
    CODY::UsageAlarmNotice observe()
    {
        CODY::UsageAlarmNotice notice{};
        require(CODY::observe_usage_alarms(state, options, weekly, clock, notice) == ESP_OK,
            "valid observations must be accepted");
        return notice;
    }
};

// No parameters: exercises the exact five threshold edges and delivery acknowledgement.
// Exception: Terminates the fixture on a missing, repeated or mismatched notice.
void check_thresholds()
{
    Fixture fixture;
    require(fixture.observe().sequence == 0, "100 percent must not notify");
    constexpr std::array<uint16_t, 5> thresholds{7000, 5000, 2000, 1000, 500};
    constexpr std::array<uint8_t, 5> percentages{70, 50, 20, 10, 5};
    uint64_t last_sequence = 0;
    for (size_t index = 0; index < thresholds.size(); ++index) {
        fixture.weekly.remaining_basis_points = static_cast<uint16_t>(thresholds[index] + 1U);
        require(fixture.observe().sequence == 0, "values above the next threshold must stay quiet");
        fixture.weekly.remaining_basis_points = thresholds[index];
        const auto notice = fixture.observe();
        require(notice.sequence > last_sequence && notice.threshold_percent == percentages[index] &&
            notice.intensity == index + 1U, "each exact threshold must issue the corresponding alert once");
        require(notice.cue == (index < 3U ? CODY::PetCue::Jump : CODY::PetCue::Surprise),
            "more urgent thresholds must select the specified sound cue");
        require(fixture.observe().sequence == notice.sequence, "a pending notice must survive until acknowledgement");
        auto wrong = notice;
        ++wrong.sequence;
        CODY::acknowledge_usage_alarm(fixture.state, wrong);
        require(fixture.observe().sequence == notice.sequence, "another notice identity must not acknowledge this alert");
        CODY::acknowledge_usage_alarm(fixture.state, notice);
        require(fixture.observe().sequence == 0, "an acknowledged unchanged threshold must not replay");
        last_sequence = notice.sequence;
    }
}

// No parameters: validates actual freshness, health and pending-notice suppression.
// Exception: Terminates the fixture if stale or failed data generate an alert.
void check_freshness_and_pending()
{
    Fixture fixture;
    require(fixture.observe().sequence == 0, "baseline must stay quiet");
    fixture.weekly.remaining_basis_points = 7000;
    fixture.weekly.data_stale = true;
    require(fixture.observe().sequence == 0, "stale allowance must not trigger");
    fixture.weekly.data_stale = false;
    fixture.weekly.last_error = ESP_FAIL;
    require(fixture.observe().sequence == 0, "failed allowance must not trigger");
    fixture.weekly.last_error = ESP_OK;
    fixture.clock.data_stale = true;
    require(fixture.observe().sequence == 0, "stale clock must not trigger");
    fixture.clock.data_stale = false;
    fixture.clock.health = LUNASHELL::DeviceHealth::Degraded;
    require(fixture.observe().sequence == 0, "unhealthy clock must not trigger");
    fixture.clock.health = LUNASHELL::DeviceHealth::Ready;
    const auto pending = fixture.observe();
    require(pending.sequence != 0 && pending.threshold_percent == 70, "fresh data must admit the pending crossing");
    fixture.weekly.data_valid = false;
    require(fixture.observe().sequence == 0, "a pending alert must not be delivered while data are unavailable");
    fixture.weekly.data_valid = true;
    require(fixture.observe().sequence == pending.sequence, "the exact pending alert must return when data recover");
    fixture.clock.utc_epoch_seconds = fixture.weekly.reset_unix_seconds;
    require(fixture.observe().sequence == 0, "an expired weekly window must not deliver an alert");
    fixture.clock.utc_epoch_seconds = 1000;
    require(fixture.observe().sequence == pending.sequence, "temporarily invalid clock must not consume a notice");
    CODY::acknowledge_usage_alarm(fixture.state, pending);
}

// No parameters: checks skipped thresholds, recovery and weekly-window replacement.
// Exception: Terminates the fixture when a crossing is replayed or not rearmed.
void check_rearm_and_weekly_reset()
{
    Fixture fixture;
    (void)fixture.observe();
    fixture.weekly.remaining_basis_points = 400;
    const auto urgent = fixture.observe();
    require(urgent.sequence != 0 && urgent.threshold_percent == 5 && urgent.intensity == 5,
        "a jump below several thresholds must issue the most urgent alert");
    CODY::acknowledge_usage_alarm(fixture.state, urgent);
    require(fixture.observe().sequence == 0, "skipped thresholds must not replay later");
    fixture.weekly.remaining_basis_points = 2100;
    require(fixture.observe().sequence == 0, "quota recovery must not itself notify");
    fixture.weekly.remaining_basis_points = 2000;
    const auto rearmed = fixture.observe();
    require(rearmed.sequence > urgent.sequence && rearmed.threshold_percent == 20,
        "a recovered threshold must notify on its next downward crossing");
    CODY::acknowledge_usage_alarm(fixture.state, urgent);
    require(fixture.observe().sequence == rearmed.sequence,
        "an old notice must not consume a newer alert");
    CODY::acknowledge_usage_alarm(fixture.state, rearmed);
    fixture.weekly.reset_unix_seconds = 3000;
    fixture.weekly.remaining_basis_points = 10000;
    require(fixture.observe().sequence == 0, "a fresh weekly window at 100 percent must stay quiet");
    fixture.weekly.remaining_basis_points = 7000;
    require(fixture.observe().threshold_percent == 70, "a new weekly window must rearm its thresholds");
}

// No parameters: rejects unsupported settings and checks explicit disable behavior.
// Exception: Terminates the fixture on invalid policy acceptance or phantom notices.
void check_policy_and_limits()
{
    Fixture fixture;
    (void)fixture.observe();
    fixture.weekly.remaining_basis_points = 0;
    fixture.options.enabled = false;
    require(fixture.observe().sequence == 0, "disabled alarms must remain silent");
    fixture.options.enabled = true;
    const auto zero = fixture.observe();
    require(zero.sequence != 0 && zero.threshold_percent == 5,
        "a real zero balance must select the most urgent threshold");
    fixture.options.thresholds = 0x80;
    CODY::UsageAlarmNotice notice{};
    require(CODY::observe_usage_alarms(fixture.state, fixture.options, fixture.weekly, fixture.clock, notice) ==
        ESP_ERR_INVALID_ARG, "unsupported threshold bits must be rejected");

    Fixture exhausted;
    (void)exhausted.observe();
    exhausted.state.next_sequence = std::numeric_limits<uint64_t>::max();
    exhausted.weekly.remaining_basis_points = 7000;
    require(CODY::observe_usage_alarms(exhausted.state, exhausted.options, exhausted.weekly, exhausted.clock, notice) ==
        ESP_ERR_INVALID_STATE, "exhausted notice identities must not wrap");
}

} // namespace

// No parameters: executes the isolated host contract against the actual firmware header.
// Exception: Individual checks terminate unsuccessfully on a contract failure.
int main()
{
    check_thresholds();
    check_freshness_and_pending();
    check_rearm_and_weekly_reset();
    check_policy_and_limits();
    std::puts("Weekly usage alarm host contract passed; hardware and audio are not exercised.");
    return EXIT_SUCCESS;
}

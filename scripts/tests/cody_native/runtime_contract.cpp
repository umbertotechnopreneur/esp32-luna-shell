#include "CODY_benchmark.hpp"
#include "CODY_demo.hpp"
#include "CODY_response.hpp"
#include "CODY_companion_gestures.hpp"
#include "CODY_companion_mode.hpp"
#include "CODY_damage.hpp"
#include "CODY_debug_hud.hpp"
#include "host_diagnostics.hpp"
#include "cloud_rig_data.hpp"
#include "runtime_settings.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <vector>
#include <algorithm>
#include <cstring>
#include "CODY2D_paths.hpp"

// No parameters: shared status-overlay fixture reports its actual contract failures.
int check_status_overlay_contract();

namespace {
// condition: Contract that must hold for the actual portable firmware implementation.
// message: Concrete failure retained in the host check output.
void require(bool condition, const char *message)
{
    if (!condition) {
        std::fprintf(stderr, "Native runtime failure: %s\n", message);
        std::exit(1);
    }
}

// No parameters: synthetic acquisition fixtures exercise the actual shared recognizers.
// Exception: Reports a violated acquisition or navigation contract through require.
void companion_navigation_gesture_contracts()
{
    CODY::CompanionTapState tap{};
    require(!CODY::observe_companion_tap(tap, 233, 300, true, true, 1000000, 0, true, false),
        "navigation must wait for an acquired release");
    require(CODY::observe_companion_tap(tap, 0, 0, false, false, 1080000, 0, true, false),
        "BSP release without coordinates must admit a stationary short tap");
    require(!CODY::observe_companion_tap(tap, 0, 0, false, false, 1090000, 0, true, false),
        "one contact must produce only one navigation");
    CODY::observe_companion_tap(tap, 233, 300, true, true, 1500000, 0, true, false);
    require(CODY::observe_companion_tap(tap, 0, 0, false, false, 1720000, 0, true, false),
        "an IRQ-driven stationary short contact does not require invented held samples");
    CODY::observe_companion_tap(tap, 233, 300, true, true, 2000000, 0, true, false);
    CODY::observe_companion_tap(tap, 260, 300, true, false, 2050000, 0, true, false);
    require(!CODY::observe_companion_tap(tap, 233, 300, false, false, 2100000, 0, true, false),
        "a drag returning to its origin must not become a navigation tap");
    CODY::observe_companion_tap(tap, 233, 300, true, true, 3000000, 0, true, false);
    CODY::cancel_companion_tap(tap);
    require(!CODY::observe_companion_tap(tap, 0, 0, false, false, 3080000, 0, true, false),
        "standby or an explicit control must cancel its contact's navigation");
    CODY::observe_companion_tap(tap, 233, 300, true, true, 4000000, 0, true, false);
    require(!CODY::observe_companion_tap(tap, 0, 0, false, false, 4080000, 90, true, false),
        "rotation must cancel a contact measured in the previous view");
    CODY::observe_companion_tap(tap, 233, 300, true, true, 5000000, 0, true, false);
    require(!CODY::observe_companion_tap(tap, 0, 0, false, false, 5480000, 0, true, false),
        "a hold must not become navigation on release");

    CODY::CompanionShakeState shake{};
    CODY::MotionSample sample{};
    sample.stamp.health = CODY::SampleHealth::Current;
    sample.acceleration_mps2[CODY::Axis::Z] = 9.81f;
    // Explicit SIMULATED steady samples arm the real recognizer after its calm interval.
    for (int index = 0; index <= 6; ++index) {
        sample.stamp.sampled_at_us = 1000000 + index * 100000;
        require(!CODY::observe_companion_shake(shake, sample, sample.stamp.sampled_at_us, true),
            "steady gravity must not produce shake navigation");
    }
    sample.stamp.sampled_at_us += 50000;
    sample.acceleration_mps2[CODY::Axis::X] = 12;
    require(!CODY::observe_companion_shake(shake, sample, sample.stamp.sampled_at_us, true),
        "a single lift or acceleration peak must not produce navigation");
    require(!CODY::observe_companion_shake(shake, sample, sample.stamp.sampled_at_us, true),
        "a repeated canonical sample cannot supply another shake peak");
    sample.acceleration_mps2[CODY::Axis::X] = 0;
    sample.stamp.sampled_at_us += 50000;
    CODY::observe_companion_shake(shake, sample, sample.stamp.sampled_at_us, true);
    sample.acceleration_mps2[CODY::Axis::X] = -12;
    sample.stamp.sampled_at_us += 50000;
    require(!CODY::observe_companion_shake(shake, sample, sample.stamp.sampled_at_us, true),
        "two opposing peaks are insufficient for deliberate shake navigation");
    sample.acceleration_mps2[CODY::Axis::X] = 0;
    sample.stamp.sampled_at_us += 50000;
    CODY::observe_companion_shake(shake, sample, sample.stamp.sampled_at_us, true);
    sample.acceleration_mps2[CODY::Axis::X] = 12;
    sample.stamp.sampled_at_us += 50000;
    require(CODY::observe_companion_shake(shake, sample, sample.stamp.sampled_at_us, true),
        "three distinct opposing peaks after calm must admit one deliberate shake");
    CODY::suspend_companion_shake(shake, sample.stamp.sampled_at_us);
    sample.stamp.sampled_at_us += 50000;
    require(!CODY::observe_companion_shake(shake, sample, sample.stamp.sampled_at_us, true),
        "wake-causing motion must settle before it can navigate");
    sample.stamp.health = CODY::SampleHealth::Stale;
    require(!CODY::observe_companion_shake(shake, sample, sample.stamp.sampled_at_us, true),
        "stale canonical IMU data must never navigate");

    shake = {};
    sample = {};
    sample.stamp.health = CODY::SampleHealth::Current;
    sample.acceleration_mps2[CODY::Axis::Z] = 9.81f;
    for (int index = 0; index <= 6; ++index) {
        sample.stamp.sampled_at_us = 4000000 + index * 100000;
        require(!CODY::observe_companion_shake(shake, sample, sample.stamp.sampled_at_us, true),
            "the real 100 ms acquisition cadence can arm after measured calm");
    }
    for (int index = 0; index < 3; ++index) {
        sample.stamp.sampled_at_us += 100000;
        sample.acceleration_mps2[CODY::Axis::X] = index == 1 ? -12.f : 12.f;
        const bool accepted = CODY::observe_companion_shake(shake, sample, sample.stamp.sampled_at_us, true);
        require(accepted == (index == 2),
            "three measured opposed peaks navigate without an invented zero-crossing sample");
        require(!CODY::observe_companion_shake(shake, sample, sample.stamp.sampled_at_us, true),
            "replaying a strong opposed measurement cannot provide an extra peak or navigation");
    }
    shake = {};
    sample.acceleration_mps2[CODY::Axis::X] = 0;
    for (int index = 0; index <= 6; ++index) {
        sample.stamp.sampled_at_us += 100000;
        CODY::observe_companion_shake(shake, sample, sample.stamp.sampled_at_us, true);
    }
    for (int index = 0; index < 5; ++index) {
        sample.stamp.sampled_at_us += 100000;
        sample.acceleration_mps2[CODY::Axis::X] = 12.f;
        require(!CODY::observe_companion_shake(shake, sample, sample.stamp.sampled_at_us, true),
            "sustained acceleration cannot become three alternating peaks");
    }
    shake = {};
    sample.acceleration_mps2[CODY::Axis::X] = 0;
    for (int index = 0; index <= 6; ++index) {
        sample.stamp.sampled_at_us += 100000;
        CODY::observe_companion_shake(shake, sample, sample.stamp.sampled_at_us, true);
    }
    require(shake.armed, "walking rejection is exercised after genuine calm arming");
    for (int index = 0; index < 8; ++index) {
        sample.stamp.sampled_at_us += 100000;
        sample.acceleration_mps2[CODY::Axis::X] = index % 2 == 0 ? 3.f : -3.f;
        require(!CODY::observe_companion_shake(shake, sample, sample.stamp.sampled_at_us, true),
            "ordinary alternating walking-sized acceleration remains below the deliberate threshold");
    }
}

// component: Host-only measured fixture used to exercise the actual HUD formatter.
void diagnostics_current(LUNASHELL::ComponentSnapshot &component)
{
    component.health = LUNASHELL::DeviceHealth::Ready;
    component.last_error = ESP_OK;
    component.data_valid = true;
    component.data_stale = false;
    component.last_success_at_us = 2000000LL;
}

// No parameters: actual HUD formatting/rasterization is exercised with explicit host fixtures.
// Exception: Native raster storage allocation may throw std::bad_alloc.
void debug_hud_contracts()
{
    HostDiagnostics::observation = {};
    HostDiagnostics::read_error = ESP_OK;
    CODY::FrameRateMeasurement measurement;
    CODY::DebugReadout unavailable;
    require(CODY::prepare_debug_readout(measurement, 2000000LL, unavailable) == ESP_OK,
            "unmeasured HUD state must format successfully");
    require(std::strcmp(unavailable.frame_rate, "FPS UNAVAILABLE") == 0 &&
            std::strcmp(unavailable.processors, "CPU1 UNAVAILABLE CPU2 UNAVAILABLE") == 0 &&
            std::strcmp(unavailable.network, "IP UNAVAILABLE") == 0 &&
            std::strcmp(unavailable.memory, "RAM UNAVAILABLE PSRAM UNAVAILABLE") == 0 &&
            std::strcmp(unavailable.storage_state, "SD UNCONFIRMED NO ACCESS") == 0 &&
            std::strcmp(unavailable.storage_free, "SD FREE UNAVAILABLE") == 0,
            "HUD must not manufacture CPU, IP, memory, card presence or free space");

    auto &observation = HostDiagnostics::observation;
    diagnostics_current(observation.wifi);
    observation.wifi.connected = true;
    observation.wifi.ip_acquired = true;
    std::snprintf(observation.wifi.ip_address, sizeof(observation.wifi.ip_address), "255.255.255.255");
    diagnostics_current(observation.memory);
    observation.memory.internal_free_bytes = 120 * 1024;
    observation.memory.psram_free_bytes = 3 * 1024 * 1024;
    diagnostics_current(observation.processor);
    observation.processor.core_count = 2;
    for (auto &core : observation.processor.cores) {
        core.available = true;
        core.utilization_available = true;
    }
    observation.processor.cores[0].utilization_percent = 75;
    observation.processor.cores[1].utilization_percent = 0;
    diagnostics_current(observation.storage);
    observation.storage.mounted = true;
    observation.storage.mount_outcome = LUNASHELL::StorageMountOutcome::Mounted;
    observation.storage.card_response_verified = true;
    observation.storage.capacity_bytes = 8ULL * 1024 * 1024 * 1024;
    observation.storage.filesystem_total_bytes = 7ULL * 1024 * 1024 * 1024;
    observation.storage.free_bytes = 3ULL * 1024 * 1024 * 1024;
    for (int frame = 0; frame < 30; ++frame) {
        CODY::record_frame_rate(measurement, 1000000LL + frame * 33333LL,
                               frame == 29 ? 2000000LL : 1000000LL + (frame + 1) * 33333LL);
    }
    CODY::DebugReadout measured;
    require(CODY::prepare_debug_readout(measurement, 2000000LL, measured) == ESP_OK &&
            std::strcmp(measured.frame_rate, "FPS 30.0") == 0 &&
            std::strcmp(measured.processors, "CPU1 75% CPU2 0%") == 0 &&
            std::strcmp(measured.network, "IP 255.255.255.255") == 0 &&
            std::strcmp(measured.memory, "RAM FREE 120K PSRAM 3072K") == 0 &&
            std::strcmp(measured.storage_state, "SD PRESENT ACCESS OK") == 0 &&
            std::strcmp(measured.storage_free, "SD FREE 3072MIB") == 0,
            "HUD must preserve core identity and measured IP/memory/SD units");

    const auto bounds = CODY::debug_readout_damage_bounds();
    require(bounds.left == 0 && bounds.top == 390 && bounds.right == 466 && bounds.bottom == 445,
            "HUD damage must cover the five lower-arc lines without the greeting title");
    std::vector<uint16_t> pixels(466 * 466, 0);
    CODY2D::Surface surface{pixels.data(), 466, 466, 466,
                            static_cast<uint32_t>(pixels.size()), CODY2D::Rgb565ByteOrder::Native};
    for (const CODY::DebugReadout &readout : {unavailable, measured}) {
        std::fill(pixels.begin(), pixels.end(), 0);
        require(CODY::draw_debug_readout_rows(readout, surface, 0, 0, 0) == ESP_OK,
                "actual HUD glyph alphabet and each occupied circular chord must fit");
        bool line_pixels[5] = {};
        for (int y = 0; y < 466; ++y) {
            for (int x = 0; x < 466; ++x) {
                if (pixels[y * 466 + x] == 0) {
                    continue;
                }
                require(y >= 390 && y < 445 &&
                        (x - 233) * (x - 233) + (y - 233) * (y - 233) < 233 * 233,
                        "every HUD pixel must be below the title and inside its local circle");
                const int line = (y - 390) / 12;
                require(line >= 0 && line < 5, "HUD row escaped its authored lower arc");
                line_pixels[line] = true;
            }
        }
        for (bool painted : line_pixels) {
            require(painted, "all five diagnostic rows must rasterize visible glyphs");
        }
    }

    observation.memory.data_stale = true;
    observation.processor.data_stale = true;
    observation.storage.data_stale = true;
    CODY::DebugReadout stale;
    require(CODY::prepare_debug_readout(measurement, 5000001LL, stale) == ESP_OK &&
            std::strcmp(stale.frame_rate, "FPS UNAVAILABLE") == 0 &&
            std::strcmp(stale.processors, "CPU1 STALE CPU2 STALE") == 0 &&
            std::strcmp(stale.memory, "RAM STALE PSRAM STALE") == 0 &&
            std::strcmp(stale.storage_free, "SD FREE STALE") == 0,
            "stale acquired values must not remain numeric");
    observation.storage.data_stale = false;
    observation.storage.data_valid = false;
    observation.storage.mounted = false;
    observation.storage.last_error = ESP_FAIL;
    require(CODY::prepare_debug_readout(measurement, 2000000LL, stale) == ESP_OK &&
            std::strcmp(stale.storage_state, "SD PRESENT NO ACCESS") == 0 &&
            std::strcmp(stale.storage_free, "SD FREE UNAVAILABLE") == 0,
            "a verified response with failed FAT access must hide old free-space values");
    observation.storage.card_response_verified = false;
    require(CODY::prepare_debug_readout(measurement, 2000000LL, stale) == ESP_OK &&
            std::strcmp(stale.storage_state, "SD UNCONFIRMED NO ACCESS") == 0,
            "no response cannot prove absence on hardware without card detect");

    observation.storage = {};
    std::snprintf(observation.wifi.ip_address, sizeof(observation.wifi.ip_address), "999.1.1.1");
    require(CODY::prepare_debug_readout(measurement, 2000000LL, stale) == ESP_ERR_INVALID_RESPONSE,
            "invalid canonical IP must retain a real rejection error");
    HostDiagnostics::read_error = ESP_ERR_TIMEOUT;
    require(CODY::prepare_debug_readout(measurement, 2000000LL, stale) == ESP_ERR_TIMEOUT,
            "canonical read failure must retain its original error");
    HostDiagnostics::read_error = ESP_OK;
    HostDiagnostics::observation = {};
}

// stamp: Source metadata in a host fixture; nothing here publishes to firmware snapshots.
// now_us: Successful sample time shared by this deterministic fixture bundle.
void current(CODY::SampleStamp &stamp, int64_t now_us)
{
    stamp.health = CODY::SampleHealth::Current;
    stamp.error = ESP_OK;
    stamp.sampled_at_us = now_us;
}

// actual: Source metadata after applying the presentation preference.
// original: Acquired metadata that the preference must leave intact.
// Exception: Exits the host executable if source provenance changes.
void require_same_stamp(const CODY::SampleStamp &actual, const CODY::SampleStamp &original)
{
    require(actual.health == original.health && actual.origin == original.origin &&
        actual.sampled_at_us == original.sampled_at_us && actual.error == original.error,
        "Away must preserve source validity, origin, timestamp and original error");
}

// No parameters: labeled host inputs exercise shared Away policy and the real response model.
// Exception: Exits the host executable if presentation changes source data or normal responses.
void companion_usage_mode_contracts()
{
    auto original_config = CODY::Data::response;
    original_config.expression.base_breath_hz = .3f;
    auto original_contour = CODY::contour_defaults();
    original_contour[CODY::ContourControl::Amplitude] = .8f;
    original_contour[CODY::ContourControl::Speed] = 1;
    CODY::ResponseInputs original_inputs{};
    original_inputs.idle_scene_allowed = true;
    original_inputs.last_interaction_at_us = 1990000;
    current(original_inputs.motion.stamp, 2000000);
    original_inputs.motion.acceleration_mps2[CODY::Axis::Z] = 9.81f;
    current(original_inputs.power.stamp, 2000000);
    original_inputs.power.level_percent = 41;
    original_inputs.power.charging = true;
    current(original_inputs.microphone.stamp, 2000000);
    original_inputs.microphone.channel_count = 1;
    original_inputs.microphone.mean_level[0] = 1024;
    original_inputs.microphone.peak_level[0] = 2048;
    current(original_inputs.playback.stamp, 2000000);
    original_inputs.playback.active = true;
    original_inputs.playback.mean_level = 1024;
    original_inputs.playback.cue_id = 7;
    current(original_inputs.body_touch.stamp, 2000000);
    original_inputs.body_touch.pressed = true;
    original_inputs.body_touch.displacement_px = {3, 4};
    original_inputs.body_touch.gesture_started_at_us = 1990000;
    original_inputs.eye_tap.stamp.health = CODY::SampleHealth::Error;
    original_inputs.eye_tap.stamp.error = ESP_ERR_TIMEOUT;
    original_inputs.eye_tap.stamp.sampled_at_us = 1980000;

    auto desk_config = original_config;
    auto desk_contour = original_contour;
    auto desk_inputs = original_inputs;
    CODY::apply_away_rhythm(false, desk_config, desk_contour, desk_inputs);
    auto away_config = original_config;
    auto away_contour = original_contour;
    auto away_inputs = original_inputs;
    CODY::apply_away_rhythm(true, away_config, away_contour, away_inputs);
    require(desk_inputs.idle_scene_allowed && !away_inputs.idle_scene_allowed &&
        desk_config.expression.base_breath_hz == original_config.expression.base_breath_hz &&
        desk_config.expression.stress_breath_gain == original_config.expression.stress_breath_gain &&
        away_config.expression.base_breath_hz < desk_config.expression.base_breath_hz &&
        away_config.expression.stress_breath_gain <= desk_config.expression.stress_breath_gain,
        "Desk must retain its authored rhythm while Away calms optional breathing and disables idle scenes");
    for (size_t index = 0; index < CODY::kContourControlCount; ++index) {
        require(desk_contour.values[index] == original_contour.values[index],
            "Desk must leave every authored contour control unchanged");
    }
    require(away_contour[CODY::ContourControl::Amplitude] < desk_contour[CODY::ContourControl::Amplitude] &&
        away_contour[CODY::ContourControl::Speed] < desk_contour[CODY::ContourControl::Speed] &&
        away_contour[CODY::ContourControl::Fullness] == desk_contour[CODY::ContourControl::Fullness] &&
        away_contour[CODY::ContourControl::PhaseSpread] == desk_contour[CODY::ContourControl::PhaseSpread],
        "Away must calm the authored outline without changing its fullness or phase spread");
    for (const auto *inputs : {&desk_inputs, &away_inputs}) {
        require_same_stamp(inputs->motion.stamp, original_inputs.motion.stamp);
        require_same_stamp(inputs->power.stamp, original_inputs.power.stamp);
        require_same_stamp(inputs->microphone.stamp, original_inputs.microphone.stamp);
        require_same_stamp(inputs->playback.stamp, original_inputs.playback.stamp);
        require_same_stamp(inputs->body_touch.stamp, original_inputs.body_touch.stamp);
        require_same_stamp(inputs->eye_tap.stamp, original_inputs.eye_tap.stamp);
        require(inputs->motion.acceleration_mps2[CODY::Axis::Z] == 9.81f && inputs->power.level_percent == 41 &&
            inputs->power.charging && inputs->microphone.channel_count == 1 &&
            inputs->microphone.mean_level[0] == 1024 && inputs->microphone.peak_level[0] == 2048 &&
            inputs->playback.active && inputs->playback.mean_level == 1024 && inputs->playback.cue_id == 7 &&
            inputs->body_touch.pressed && inputs->body_touch.displacement_px.x == 3 &&
            inputs->body_touch.displacement_px.y == 4 && inputs->body_touch.gesture_started_at_us == 1990000 &&
            inputs->last_interaction_at_us == 1990000,
            "a presentation mode must leave acquired motion, power, sound and touch values intact");
    }
    const auto state = CODY::make_response_state(original_config);
    CODY::ResponseState baseline, desk, away;
    require(CODY::advance_response(original_config, state, original_inputs, 100, 2000000, true, baseline) == ESP_OK &&
        CODY::advance_response(desk_config, state, desk_inputs, 100, 2000000, true, desk) == ESP_OK &&
        CODY::advance_response(away_config, state, away_inputs, 100, 2000000, true, away) == ESP_OK,
        "both usage modes must remain valid inputs to the actual response model");
    require(desk.breath_phase == baseline.breath_phase && desk.breath_rate_hz == baseline.breath_rate_hz &&
        desk.blink_elapsed_ms == baseline.blink_elapsed_ms && away.blink_elapsed_ms == baseline.blink_elapsed_ms &&
        away.breath_rate_hz < baseline.breath_rate_hz && away.stress == baseline.stress &&
        away.fatigue == baseline.fatigue && away.attention == baseline.attention && away.speaking == baseline.speaking &&
        away.drag_x.position == baseline.drag_x.position && away.drag_y.position == baseline.drag_y.position &&
        away.touch_pressure.position == baseline.touch_pressure.position,
        "Away must slow breathing while preserving blink timing and real sensor/touch response gains");
    for (const auto axis : {CODY::Axis::X, CODY::Axis::Y, CODY::Axis::Z}) {
        require(away.body[CODY::to_index(axis)].position == baseline.body[CODY::to_index(axis)].position &&
            away.body[CODY::to_index(axis)].velocity == baseline.body[CODY::to_index(axis)].velocity,
            "Away must preserve the real motion response on every axis");
    }
}

// Exercise real bounded statistics, rollover, overlay separation and error boundaries.
void benchmark_contracts()
{
    CODY::FrameBenchmark collector;
    CODY::FrameDistribution output{};
    require(collector.summarize(CODY::OverlayMode::Artwork, 1000, output) == ESP_ERR_NOT_FOUND,
            "empty statistics must remain unavailable");
    for (uint32_t duration : {100U, 200U, 300U, 400U}) {
        require(collector.record(CODY::OverlayMode::Artwork, {10, 20, 30, duration}) == ESP_OK,
                "valid measured frame rejected");
    }
    require(collector.summarize(CODY::OverlayMode::Artwork, 250, output) == ESP_OK,
            "statistics unavailable");
    require(output.count == 4 && output.mean.total_us == 250 && output.median_us == 200 &&
            output.p95_us == 400 && output.maximum_us == 400 && output.missed_deadlines == 2 &&
            std::abs(output.standard_deviation_us - std::sqrt(12500.0)) < .001,
            "measured distribution differs");
    require(collector.summarize(CODY::OverlayMode::Skeleton, 250, output) == ESP_ERR_NOT_FOUND,
            "overlay windows must remain independent");
    require(collector.record(CODY::OverlayMode::Artwork, {100, 100, 100, 1}) == ESP_ERR_INVALID_ARG,
            "invalid stages accepted");
    for (uint32_t index = 0; index < 129; ++index) {
        require(collector.record(CODY::OverlayMode::Skeleton, {0, 0, 0, 1000 + index}) == ESP_OK,
                "rolling record failed");
    }
    require(collector.summarize(CODY::OverlayMode::Skeleton, 2000, output) == ESP_OK &&
            output.count == 128 && output.maximum_us == 1128 && output.mean.total_us == 1064,
            "rolling window did not discard the oldest frame");
    collector.reset();
    require(collector.summarize(CODY::OverlayMode::Artwork, 250, output) == ESP_ERR_NOT_FOUND,
            "explicit reset must clear retained observations");
}

// surface: Guarded reference destination with ordinary RGB565 pixels.
// first_row: Canvas origin of this reference tile.
// from: Segment start, including deliberately off-screen endpoints.
// to: Segment end; identical endpoints exercise round markers.
// width: Positive stroke diameter shared with the production raster.
// color: Exact RGB565 output value.
void reference_stroke(CODY2D::Surface surface, uint16_t first_row,
                      CODY2D::Vec2 from, CODY2D::Vec2 to, float width, uint16_t color)
{
    // Keep the previous exhaustive algorithm independent of the optimized
    // scanline bounds. Comparing two calls to the new raster would prove little.
    const float radius = width * .5f;
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float squared_length = dx * dx + dy * dy;
    const int left = std::max(0, static_cast<int>(std::floor(std::min(from.x, to.x) - radius)));
    const int right = std::min(static_cast<int>(surface.width) - 1,
        static_cast<int>(std::ceil(std::max(from.x, to.x) + radius)));
    const int top = std::max(static_cast<int>(first_row),
        static_cast<int>(std::floor(std::min(from.y, to.y) - radius)));
    const int bottom = std::min(static_cast<int>(first_row + surface.height) - 1,
        static_cast<int>(std::ceil(std::max(from.y, to.y) + radius)));
    for (int row = top; row <= bottom; ++row) {
        for (int column = left; column <= right; ++column) {
            const float px = column + .5f - from.x;
            const float py = row + .5f - from.y;
            const float position = squared_length == 0 ? 0 :
                std::max(0.f, std::min(1.f, (px * dx + py * dy) / squared_length));
            const float ex = px - position * dx;
            const float ey = py - position * dy;
            if (ex * ex + ey * ey <= radius * radius) {
                surface.pixels[(row - first_row) * surface.stride + column] = color;
            }
        }
    }
}

// Compare exact stroke pixels, round caps, row padding and guards across tiles.
// Exceptions: std::bad_alloc if a host fixture allocation fails.
void stroke_contracts()
{
    constexpr uint16_t size = 466;
    constexpr uint16_t stride = size + 3;
    struct Segment { CODY2D::Vec2 from; CODY2D::Vec2 to; };
    std::vector<Segment> segments = {
        {{12.25f, 20.5f}, {451.75f, 445.25f}},
        {{451.75f, 20.5f}, {12.25f, 445.25f}},
        {{12.25f, 445.25f}, {451.75f, 20.5f}},
        {{451.75f, 445.25f}, {12.25f, 20.5f}},
        {{0.f, 233.f}, {466.f, 233.f}},
        {{233.f, 0.f}, {233.f, 466.f}},
        {{-.25f, -.75f}, {465.5f, 465.5f}},
        {{-2048.f, 210.f}, {2048.f, 212.f}},
        {{210.f, -2048.f}, {212.f, 2048.f}},
        {{-2048.f, -2048.f}, {2048.f, 2048.f}},
        {{1.f, 233.f}, {465.f, 233.0005f}},
        {{1.f, 233.f}, {465.f, 233.0015f}},
        {{232.5f, 233.5f}, {232.5f, 233.5f}},
        {{-5.f, -5.f}, {-2.f, -2.f}},
        {{230.5f, 230.5f}, {230.501f, 230.501f}},
        {{232.5f, 233.f}, {233.5f, 233.f}},
        {{210.75f, 222.25f}, {250.125f, 242.625f}},
        {{-2048.f, -2047.5f}, {2048.f, 2047.5f}},
    };
    // Deterministic fractional endpoints expose reciprocal-rounding boundaries
    // without relying on the optimized implementation to generate expectations.
    uint32_t seed = 0x43ad521U;
    for (uint16_t sample = 0; sample < 32; ++sample) {
        float coordinates[4];
        for (float &coordinate : coordinates) {
            seed = seed * 1664525U + 1013904223U;
            coordinate = static_cast<float>(seed & 0xffffU) / 96.f - 100.f;
        }
        segments.push_back({{coordinates[0], coordinates[1]}, {coordinates[2], coordinates[3]}});
    }
    for (const Segment &segment : segments) {
        for (float width : {.001f, .5f, 1.f, 2.75f, 7.f, CODY2D::kMaxStrokeWidth}) {
            for (uint16_t rows : {uint16_t{8}, uint16_t{16}, uint16_t{32}, uint16_t{64}}) {
                std::vector<uint16_t> expected(size * stride + 2, 0x35ad);
                auto actual = expected;
                for (uint16_t first = 0; first < size; first += rows) {
                    const uint16_t height = std::min<uint16_t>(rows, size - first);
                    const CODY2D::Surface reference{expected.data() + 1 + first * stride,
                        size, height, stride, static_cast<uint32_t>(height * stride)};
                    const CODY2D::Surface optimized{actual.data() + 1 + first * stride,
                        size, height, stride, static_cast<uint32_t>(height * stride)};
                    reference_stroke(reference, first, segment.from, segment.to, width, 0xffff);
                    require(CODY2D::stroke_rows(optimized, first, segment.from, segment.to,
                        width, 0xffff) == ESP_OK, "optimized stroke rejected valid geometry");
                }
                require(actual == expected, "scanline stroke changed reference pixels or guards");
            }
        }
    }
}

// Check analytically known RGB565 coverage and gradient slots, including clipped
// empty spans and padding. Expected colors do not call the implementation's blend.
void fill_contracts()
{
    uint16_t palette[256]{};
    palette[10] = 0xf800;
    palette[11] = 0x07e0;
    palette[12] = 0x001f;
    palette[13] = 0xffff;
    const CODY2D::Vec2 rectangle[] = {{0, 0}, {8, 0}, {8, 4}, {0, 4}};
    struct Coverage { uint8_t opacity; uint16_t expected; };
    for (const Coverage coverage : {Coverage{0, 0x001f}, Coverage{64, 0x4017},
            Coverage{128, 0x800f}, Coverage{255, 0xf800}}) {
        uint16_t pixels[40];
        std::fill(std::begin(pixels), std::end(pixels), 0x001f);
        const CODY2D::Surface surface{pixels, 8, 4, 10, 40};
        require(CODY2D::fill_path_rows(surface, 0, rectangle, 4, palette, 10, 10,
                coverage.opacity) == ESP_OK, "known alpha fill rejected");
        for (uint16_t row = 0; row < 4; ++row) {
            for (uint16_t column = 0; column < 10; ++column) {
                require(pixels[row * 10 + column] == (column < 8 ? coverage.expected : 0x001f),
                    "RGB565 alpha rounding or padding changed");
            }
        }
    }
    for (const uint16_t rows : {uint16_t{1}, uint16_t{3}, uint16_t{4}}) {
        uint16_t pixels[40]{};
        for (uint16_t first = 0; first < 4; first += rows) {
            const uint16_t height = std::min<uint16_t>(rows, 4 - first);
            const CODY2D::Surface tile{pixels + first * 10, 8, height, 10, uint32_t(height * 10)};
            require(CODY2D::fill_path_rows(tile, first, rectangle, 4, palette, 10, 13) == ESP_OK,
                "known gradient fill rejected");
        }
        for (uint16_t row = 0; row < 4; ++row) {
            for (uint16_t column = 0; column < 10; ++column) {
                require(pixels[row * 10 + column] == (column < 8 ? palette[10 + row] : 0),
                    "gradient slots or tile/padding coverage changed");
            }
        }
        const auto before = std::vector<uint16_t>(pixels, pixels + 40);
        const CODY2D::Vec2 clipped[] = {{-12, 0}, {-4, 0}, {-4, 4}, {-12, 4}};
        const CODY2D::Surface surface{pixels, 8, 4, 10, 40};
        require(CODY2D::fill_path_rows(surface, 0, clipped, 4, palette, 10, 13) == ESP_OK &&
            std::equal(before.begin(), before.end(), pixels), "empty clipped span wrote outside its bounds");
    }
}

// Check every authored phase boundary using the same generated settings used on the device.
void demo_contracts()
{
    CODY::Channels face{};
    CODY::ContourControls contour{};
    CODY::DemoPhase phase{};
    int64_t elapsed_us = 0;
    for (const auto &step : CODY::RuntimeData::steps) {
        require(CODY::demo_targets(elapsed_us, face, contour, phase) == ESP_OK && phase == step.identity,
                "demo phase identity or boundary differs");
        require(face[CODY::FaceChannel::EyeLeft] == step.face[CODY::FaceChannel::EyeLeft] &&
                contour[CODY::ContourControl::Speed] == step.speed, "demo target differs");
        elapsed_us += step.duration_ms * 1000LL;
    }
    require(CODY::demo_targets(elapsed_us, face, contour, phase) == ESP_OK &&
            phase == CODY::RuntimeData::steps[0].identity, "demo cycle did not wrap");
    require(CODY::demo_targets(-1, face, contour, phase) == ESP_ERR_INVALID_ARG,
            "negative demo time accepted");
}
// Cover cache invalidation, paired columns, overlapping packing and pixel parity.
// Exceptions: std::bad_alloc if a host framebuffer allocation fails.
void damage_contracts()
{
    uint16_t cached_storage[40]{};
    uint16_t tile_storage[20]{};
    const CODY2D::Surface cache{cached_storage, 8, 4, 10, 40};
    const CODY2D::Surface tile{tile_storage, 8, 2, 10, 20};
    CODY::DamageRect damage;
    CODY::DamageRegions regions{{{0, 0, 8, 4}}, 1};
    require(CODY::measure_and_cache_damage_rows(tile, 1, cache, true, regions, damage) == ESP_OK && damage.empty(),
            "unchanged tile submitted pixels");
    tile_storage[3] = 0x1234;
    tile_storage[17] = 0xabcd;
    require(CODY::measure_and_cache_damage_rows(tile, 1, cache, true, regions, damage) == ESP_OK &&
            damage.left == 2 && damage.right == 8 && damage.top == 1 && damage.bottom == 3,
            "paired-column damage bounds differ");
    require(cached_storage[12] == 0 && cached_storage[13] == 0x1234 &&
            cached_storage[26] == 0 && cached_storage[27] == 0xabcd,
            "damage measurement did not stage only the paired row spans");
    CODY2D::Surface packed;
    require(CODY::pack_damage_rows(tile, 1, cache, damage, packed) == ESP_OK &&
            packed.width == 6 && packed.height == 2 && packed.stride == 6 &&
            packed.pixels[1] == 0x1234 && packed.pixels[11] == 0xabcd &&
            cached_storage[13] == 0x1234 && cached_storage[27] == 0xabcd &&
            cached_storage[18] == 0 && cached_storage[28] == 0,
            "packing corrupted candidate cache, cropped rows or stride padding");
    require(CODY::measure_and_cache_damage_rows(tile, 3, cache, true, regions, damage) == ESP_ERR_INVALID_ARG,
            "out-of-canvas tile accepted");
    require(CODY::measure_and_cache_damage_rows(tile, 1, cache, false, regions, damage) == ESP_OK &&
            damage.left == 0 && damage.right == 8 && damage.top == 1 && damage.bottom == 3,
            "invalid cache did not force the complete tile");
    regions.rectangles[0].right = 9;
    require(CODY::measure_and_cache_damage_rows(tile, 1, cache, true, regions, damage) == ESP_ERR_INVALID_ARG,
            "out-of-canvas candidate accepted");
    regions.count = CODY::DamageRegions::capacity + 1;
    require(CODY::measure_and_cache_damage_rows(tile, 1, cache, true, regions, damage) == ESP_ERR_INVALID_SIZE,
            "candidate array overflow accepted");

    // Exercise every block boundary and two-byte-aligned rows. One changed
    // pixel must produce exactly its paired columns, including the final tail.
    for (uint16_t changed_column = 0; changed_column <= 140; ++changed_column) {
        uint16_t block_cache[142]{};
        uint16_t block_tile[142]{};
        block_cache[0] = block_tile[0] = 0x35ad;
        block_cache[141] = block_tile[141] = 0x35ad;
        if (changed_column < 140) {
            block_tile[changed_column + 1] = 0xffff;
        }
        const CODY2D::Surface block_current{block_tile + 1, 140, 1, 140, 140};
        const CODY2D::Surface block_previous{block_cache + 1, 140, 1, 140, 140};
        const CODY::DamageRegions block_regions{{{0, 0, 140, 1}}, 1};
        require(CODY::measure_and_cache_damage_rows(block_current, 0, block_previous, true, block_regions, damage) == ESP_OK,
            "block comparison rejected an aligned RGB565 row");
        if (changed_column < 140) {
            require(damage.left == (changed_column & ~1U) && damage.right == (changed_column | 1U) + 1 &&
                    damage.top == 0 && damage.bottom == 1, "block comparison lost a boundary pixel");
        } else {
            require(damage.empty(), "equal block row generated damage");
        }
        require(block_tile[0] == 0x35ad && block_tile[141] == 0x35ad &&
                block_cache[0] == 0x35ad && block_cache[141] == 0x35ad, "comparison overwrote row guards");
    }
    constexpr uint16_t panel_width = 466;
    constexpr uint16_t panel_height = 466;
    constexpr uint32_t frame_pixels = static_cast<uint32_t>(panel_width) * panel_height;
    for (const uint16_t tile_rows : {uint16_t{1}, uint16_t{8}, uint16_t{16}, uint16_t{32}, uint16_t{64}}) {
      for (const bool debug : {false, true}) {
        std::vector<uint16_t> previous(frame_pixels);
        std::vector<uint16_t> host_panel(frame_pixels, 0xf81f);
        std::vector<uint16_t> expected(frame_pixels);
        std::vector<uint16_t> rendered_tile(panel_width * tile_rows);
        const CODY2D::Surface previous_frame{previous.data(), panel_width, panel_height, panel_width, frame_pixels};
        const CODY2D::Surface expected_frame{expected.data(), panel_width, panel_height, panel_width, frame_pixels};
        bool cache_valid = false;
        CODY::DamageRect previous_bounds;
        bool previous_debug = debug;
        bool previous_paused = false;
        enum class Scenario { Initial, Unchanged, MouthChange, Translate, Shrink, Clip,
            SelectedMarker, ToggleOverlay, RestoreOverlay, Pause, PauseHold, Resume, Invalidate };
        for (const auto scenario : {Scenario::Initial, Scenario::Unchanged, Scenario::MouthChange,
                Scenario::Translate, Scenario::Shrink, Scenario::Clip, Scenario::SelectedMarker,
                Scenario::ToggleOverlay, Scenario::RestoreOverlay, Scenario::Pause, Scenario::PauseHold,
                Scenario::Resume, Scenario::Invalidate}) {
            auto channels = CODY::Data::ready;
            if (scenario == Scenario::MouthChange) {
                channels[CODY::FaceChannel::MouthOpen] = .9f;
            }
            auto placement = CODY::Data::placement;
            if (scenario == Scenario::Translate) {
                placement.x += 24;
                placement.y -= 35;
            } else if (scenario == Scenario::Shrink) {
                placement.scale *= .5f;
            } else if (scenario == Scenario::Clip) {
                placement.x = 30;
                placement.y = 30;
            }
            const bool paused = scenario == Scenario::Pause || scenario == Scenario::PauseHold;
            const bool current_debug = scenario == Scenario::ToggleOverlay ? !debug : debug;
            const auto selected = scenario == Scenario::SelectedMarker ? CODY::PointId::EyeLeft : CODY::kNoPoint;
            CODY::RigScene scene;
            CODY::DamageRect current_bounds;
            require(CODY::evaluate_rig(channels, placement, scene) == ESP_OK &&
                    CODY::draw_rig_rows(scene, expected_frame, 0, current_debug, selected, nullptr, paused) == ESP_OK &&
                    CODY::rig_damage_bounds(scene, current_debug, current_bounds) == ESP_OK,
                    "reference geometry/raster failed");
            // Hidden debug controls may make Pause/Resume visually unchanged.
            // The full reference framebuffer determines whether a transfer is needed.
            const bool reference_changed = expected != host_panel;
            regions.rectangles[0] = previous_bounds;
            regions.rectangles[1] = current_bounds;
            regions.count = 2;
            if (current_debug != previous_debug || paused != previous_paused) {
                regions.rectangles[regions.count++] = CODY::rig_navigation_damage_bounds();
            }
            uint32_t submitted_pixels = 0;
            const bool compare_previous = cache_valid && scenario != Scenario::Invalidate;
            for (uint16_t row = 0; row < panel_height; row += tile_rows) {
                const uint16_t height = panel_height - row < tile_rows ? panel_height - row : tile_rows;
                bool candidate_row = !compare_previous;
                for (uint16_t index = 0; index < regions.count; ++index) {
                    const auto &region = regions.rectangles[index];
                    candidate_row |= !region.empty() && row < region.bottom && row + height > region.top;
                }
                if (!candidate_row) {
                    continue; // Reference remains a full redraw; the candidate skips proven-static tiles.
                }
                const CODY2D::Surface rendered{rendered_tile.data(), panel_width, height, panel_width,
                    static_cast<uint32_t>(rendered_tile.size())};
                require(CODY::draw_rig_rows(scene, rendered, row, current_debug, selected, nullptr, paused) == ESP_OK &&
                        CODY::measure_and_cache_damage_rows(rendered, row, previous_frame, compare_previous, regions, damage) == ESP_OK,
                        "rendered tile damage failed");
                if (damage.empty()) {
                    continue;
                }
                require(CODY::pack_damage_rows(rendered, row, previous_frame, damage, packed) == ESP_OK,
                        "actual rig payload packing failed");
                for (uint16_t packed_row = 0; packed_row < packed.height; ++packed_row) {
                    for (uint16_t column = 0; column < packed.width; ++column) {
                        host_panel[(damage.top + packed_row) * panel_width + damage.left + column] =
                            packed.pixels[packed_row * packed.stride + column];
                    }
                }
                submitted_pixels += static_cast<uint32_t>(packed.width) * packed.height;
            }
            require(host_panel == expected && previous == expected,
                    "partial updates differ from the complete actual rig framebuffer");
            if (scenario == Scenario::Initial || scenario == Scenario::Invalidate) {
                require(submitted_pixels == frame_pixels, "invalid cache skipped canvas pixels");
            } else if (!reference_changed) {
                require(submitted_pixels == 0, "unchanged reference frame submitted a rectangle");
            } else {
                require(submitted_pixels > 0 && submitted_pixels < frame_pixels,
                        "face update did not use actual changed regions");
            }
            cache_valid = true;
            previous_bounds = current_bounds;
            previous_debug = current_debug;
            previous_paused = paused;
        }
      }
    }
}
} // namespace

// No parameters: emit fifty seconds from the actual C++ model for independent Python parity.
// Exception: Host framebuffer allocations may throw std::bad_alloc.
int main()
{
    companion_navigation_gesture_contracts();
    companion_usage_mode_contracts();
    require(CODY::rig_control_at({158, 47}) == CODY::RigControl::None &&
            CODY::rig_control_at({233, 47}) == CODY::RigControl::None &&
            CODY::rig_control_at({308, 47}) == CODY::RigControl::None,
            "hidden top controls must not intercept character contacts");
    require(CODY::rig_control_at({40, 233}) == CODY::RigControl::None &&
            CODY::rig_control_at({426, 233}) == CODY::RigControl::None &&
            CODY::rig_control_at({233, 233}) == CODY::RigControl::None,
            "top controls must not steal lateral or character taps");
    benchmark_contracts();
    debug_hud_contracts();
    require(check_status_overlay_contract() == 0, "status overlay glyph/geometry contracts failed");
    demo_contracts();
    damage_contracts();
    stroke_contracts();
    fill_contracts();
    require(CODY::validate_response_config(CODY::Data::response) == ESP_OK, "generated response rejected");
    auto state = CODY::make_response_state(CODY::Data::response);
    CODY::ResponseConfig fixture_config = CODY::Data::response;
    // Match the host fixture's shortened schedule without changing authored device timing.
    fixture_config.idle.after_seconds = 5;
    fixture_config.idle.cooldown_seconds = 2;
    CODY::RigScene scene;
    require(CODY::evaluate_rig(CODY::Data::ready, {245, 221, 135}, scene) == ESP_OK,
            "shared interaction geometry unavailable");
    CODY::EyeId eye = CODY::EyeId::None;
    require(CODY::hit_test_eye(scene, scene.world[CODY::to_index(CODY::PointId::EyeLeft)],
            CODY::Data::response.touch.eye_hit_radius, eye) == ESP_OK && eye == CODY::EyeId::Left,
            "translated/scaled left eye hit differs");
    require(CODY::hit_test_eye(scene, {20, 20}, CODY::Data::response.touch.eye_hit_radius, eye) == ESP_OK &&
            eye == CODY::EyeId::None, "eye miss became an eye interaction");
    CODY::ResponseInputs rejected{};
    current(rejected.microphone.stamp, 1000000LL);
    rejected.microphone.channel_count = 3;
    auto unchanged = state;
    require(CODY::advance_response(CODY::Data::response, state, rejected, 100, 1000000LL, true, unchanged) == ESP_ERR_INVALID_ARG &&
            unchanged.idle_seconds == state.idle_seconds && unchanged.breath_phase == state.breath_phase,
            "invalid microphone must reject the entire model transaction");
    std::puts("[");
    for (int tick = 0; tick < 500; ++tick) {
        const int64_t now_us = 1000000LL + tick * 100000LL;
        CODY::ResponseInputs inputs{};
        inputs.idle_scene_allowed = true;
        current(inputs.power.stamp, now_us);
        inputs.power.level_percent = tick < 150 ? 100.f : 12.f;
        if (tick >= 400) {
            inputs.power.stamp.health = CODY::SampleHealth::Unavailable;
            inputs.power.stamp.error = ESP_ERR_INVALID_STATE;
        }
        current(inputs.microphone.stamp, now_us);
        inputs.microphone.channel_count = 2;
        const bool heard = tick >= 80 && tick < 100;
        inputs.microphone.mean_level[0] = inputs.microphone.peak_level[0] = heard ? 6000 : 0;
        inputs.microphone.mean_level[1] = inputs.microphone.peak_level[1] = heard ? 3000 : 0;
        current(inputs.playback.stamp, now_us);
        inputs.playback.active = tick >= 110 && tick < 130;
        inputs.playback.mean_level = inputs.playback.active ? 5000 : 0;
        inputs.last_interaction_at_us = tick >= 344 ? 35400000LL : 0;
        if (tick >= 450) {
            current(inputs.eye_tap.stamp, 46000000LL);
            inputs.eye_tap.eye = CODY::EyeId::Left;
            inputs.last_interaction_at_us = 46000000LL;
        }
        if (tick >= 300 && tick < 336) {
            current(inputs.body_touch.stamp, now_us);
            inputs.body_touch.pressed = tick < 325;
            inputs.body_touch.displacement_px = {60, -25};
            inputs.body_touch.gesture_started_at_us = 31000000LL;
        }
        require(CODY::advance_response(fixture_config, state, inputs, 100, now_us, true, state) == ESP_OK,
                "response fixture update rejected");
        CODY::ResponseFrame frame{};
        require(CODY::compose_response(fixture_config, state, CODY::Data::ready,
            CODY::contour_defaults(), CODY::Data::placement, 0, true, frame) == ESP_OK,
            "response fixture composition rejected");
        std::printf("%s{\"tick\":%d,\"attention\":%.9g,\"speaking\":%.9g,\"fatigue\":%.9g,"
            "\"idle\":%.9g,\"yawn\":%.9g,\"idle_scene\":%lu,\"idle_scene_weight\":%.9g,"
            "\"mouth\":%.9g,\"eye\":%.9g,\"annoyance\":%.9g,\"closure\":%.9g,"
            "\"root_x\":%.9g,\"root_y\":%.9g,\"depth\":%.9g,\"amplitude\":%.9g,\"fullness\":%.9g}",
            tick == 0 ? "" : ",\n", tick, state.attention, state.speaking, state.fatigue,
            state.idle_seconds, state.yawn_weight, static_cast<unsigned long>(state.idle_scene),
            state.idle_scene_weight, frame.face[CODY::FaceChannel::MouthOpen],
            frame.face[CODY::FaceChannel::EyeLeft], state.annoyance, state.protective_eye_closure,
            frame.current_root.x, frame.current_root.y, frame.depth_scale,
            frame.contour[CODY::ContourControl::Amplitude], frame.contour[CODY::ContourControl::Fullness]);
    }
    std::puts("\n]");
    return 0;
}

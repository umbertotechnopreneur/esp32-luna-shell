#include "CODY_benchmark.hpp"
#include "CODY_demo.hpp"
#include "CODY_response.hpp"
#include "cloud_rig_data.hpp"
#include "poc_settings.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

namespace {
// condition: Contract that must hold for the actual portable firmware implementation.
// message: Concrete failure retained in the host check output.
void require(bool condition, const char *message)
{
    if (!condition) {
        std::fprintf(stderr, "Native PoC failure: %s\n", message);
        std::exit(1);
    }
}

// stamp: Source metadata in a host fixture; nothing here publishes to firmware snapshots.
// now_us: Successful sample time shared by this deterministic fixture bundle.
void current(CODY::SampleStamp &stamp, int64_t now_us)
{
    stamp.health = CODY::SampleHealth::Current;
    stamp.error = ESP_OK;
    stamp.sampled_at_us = now_us;
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

// Check every authored phase boundary using the same generated settings used on the device.
void demo_contracts()
{
    CODY::Channels face{};
    CODY::ContourControls contour{};
    CODY::DemoPhase phase{};
    int64_t elapsed_us = 0;
    for (const auto &step : CODY::PocData::steps) {
        require(CODY::demo_targets(elapsed_us, face, contour, phase) == ESP_OK && phase == step.identity,
                "demo phase identity or boundary differs");
        require(face[CODY::FaceChannel::EyeLeft] == step.face[CODY::FaceChannel::EyeLeft] &&
                contour[CODY::ContourControl::Speed] == step.speed, "demo target differs");
        elapsed_us += step.duration_ms * 1000LL;
    }
    require(CODY::demo_targets(elapsed_us, face, contour, phase) == ESP_OK &&
            phase == CODY::PocData::steps[0].identity, "demo cycle did not wrap");
    require(CODY::demo_targets(-1, face, contour, phase) == ESP_ERR_INVALID_ARG,
            "negative demo time accepted");
}
} // namespace

// Emit a fifty-second fixture from the actual C++ model for independent Python parity.
int main()
{
    benchmark_contracts();
    demo_contracts();
    require(CODY::validate_response_config(CODY::Data::response) == ESP_OK, "generated response rejected");
    auto state = CODY::make_response_state(CODY::Data::response);
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
        require(CODY::advance_response(CODY::Data::response, state, inputs, 100, now_us, true, state) == ESP_OK,
                "response fixture update rejected");
        CODY::ResponseFrame frame{};
        require(CODY::compose_response(CODY::Data::response, state, CODY::Data::ready,
            CODY::contour_defaults(), CODY::Data::placement, 0, true, frame) == ESP_OK,
            "response fixture composition rejected");
        std::printf("%s{\"tick\":%d,\"attention\":%.9g,\"speaking\":%.9g,\"fatigue\":%.9g,"
            "\"idle\":%.9g,\"yawn\":%.9g,\"mouth\":%.9g,\"eye\":%.9g,\"annoyance\":%.9g,\"closure\":%.9g}",
            tick == 0 ? "" : ",\n", tick, state.attention, state.speaking, state.fatigue,
            state.idle_seconds, state.yawn_weight, frame.face[CODY::FaceChannel::MouthOpen],
            frame.face[CODY::FaceChannel::EyeLeft], state.annoyance, state.protective_eye_closure);
    }
    std::puts("\n]");
    return 0;
}

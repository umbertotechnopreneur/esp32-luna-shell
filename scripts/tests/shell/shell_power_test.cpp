#include <cassert>
#include <iostream>
#include "../../../components/LUNASHELL_app/LUNASHELL_app_shell.cpp"

using namespace shell_test;
using namespace LUNASHELL;
using esp_brookesia::system::core::AppContext;

namespace LUNASHELL {
const lv_image_dsc_t *MascotAsset::idle_front_image() {
    static const lv_image_dsc_t image{};
    return &image;
}
}

int main() {
    AppContext context;
    ShellApp app;
    assert(app.on_start(context));
    assert(app.on_action(context, kTouchAction));
    assert(!app.on_action(context, "unknown"));
#if CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS
    auto &power = DeviceSnapshotService::instance().current.power;
    const auto tick = [&] { return app.on_timer(context, 1, "shell.power"); };
    const auto enter = [&](uint32_t sequence) {
        power = {DeviceOperatingState::EnteringStandby, sequence};
        assert(tick());
    };
    enter(1);
    assert(fixture.acknowledgements.empty()); // No acknowledgement before drain.
    assert(app.on_action(context, kTouchAction));
    fixture.drains.back()({});
    assert((fixture.acknowledgements.back() == std::pair<uint32_t, int>{1, ESP_OK}));
    power.state = DeviceOperatingState::Standby;
    assert(tick());
    assert(fixture.resumes == 0);
    power.state = DeviceOperatingState::Active;
    fixture.fail_drain = true;
    assert(!tick());
    assert(fixture.resumes == 0);
    fixture.fail_drain = false;
    fixture.fail_resume = true;
    assert(!tick());
    assert(app.on_action(context, kTouchAction));
    fixture.fail_resume = false;
    assert(tick());
    assert(fixture.resumes == 1);
    assert(app.on_action(context, kTouchAction));

    enter(2);
    auto stale = fixture.drains.back();
    power.state = DeviceOperatingState::Active; // Power task timed out/rolled back.
    assert(tick());
    enter(3);
    auto count = fixture.acknowledgements.size();
    stale({});
    assert(fixture.acknowledgements.size() == count);
    fixture.drains.back()(std::unexpected("asynchronous drain failure"));
    assert((fixture.acknowledgements.back() == std::pair<uint32_t, int>{3, ESP_FAIL}));
    power.state = DeviceOperatingState::Active;
    assert(tick());

    fixture.fail_quiesce = true;
    power = {DeviceOperatingState::EnteringStandby, 4};
    assert(!tick());
    assert((fixture.acknowledgements.back() == std::pair<uint32_t, int>{4, ESP_FAIL}));
    fixture.fail_quiesce = false;
    power.state = DeviceOperatingState::Active;
    assert(tick());
    enter(5);
    auto after_stop = fixture.drains.back();
    fixture.fail_stop = true;
    assert(!app.on_stop(context));
    fixture.fail_stop = false;
    assert(app.on_stop(context));
    count = fixture.acknowledgements.size();
    after_stop({});
    assert(fixture.acknowledgements.size() == count);

    Drain after_destruction;
    {
        ShellApp temporary;
        assert(temporary.on_start(context));
        power = {DeviceOperatingState::EnteringStandby, 6};
        assert(temporary.on_timer(context, 1, "shell.power"));
        after_destruction = fixture.drains.back();
    }
    after_destruction({});
    assert(fixture.acknowledgements.size() == count);
    fixture.fail_timer = true;
    ShellApp failed_start;
    assert(!failed_start.on_start(context));
#else
    assert(fixture.timers_started == 0);
#endif
    std::cout << "Shell lifecycle passed (PMU=" << CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS << ")\n";
}

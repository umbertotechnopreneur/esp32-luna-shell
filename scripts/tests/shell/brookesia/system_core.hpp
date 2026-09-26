// Minimal host doubles for testing the actual shell lifecycle, without hardware.
#pragma once
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace shell_test {
using Result = std::expected<void, std::string>;
using Drain = std::function<void(Result)>;
struct Fixture {
    bool fail_subscribe = false;
    bool fail_timer = false;
    bool fail_stop = false;
    bool fail_quiesce = false;
    bool fail_resume = false;
    bool fail_drain = false;
    int timers_started = 0;
    int resumes = 0;
    std::string text;
    std::vector<Drain> drains;
    std::vector<std::pair<uint32_t, int>> acknowledgements;
};
inline Fixture fixture;
}
namespace esp_brookesia::runtime {
enum class BackendType { Unknown };
}
namespace esp_brookesia::gui {
using SubscriptionId = uint32_t;
}
namespace esp_brookesia::system::core {
using TimerId = uint32_t;
constexpr TimerId INVALID_TIMER_ID = 0;
enum class AppKind { Native };
enum class GuiRootKind { File };
enum class GuiAppLayer { AppDefault };
struct AppManifest {
    std::string id, name;
    std::vector<int> localized_names;
    std::string version;
    AppKind kind;
    bool visible, preload_dom;
    std::string icon_id;
    std::vector<int> supported_systems;
    std::string icon_path;
    runtime::BackendType runtime_type;
    std::string app_path, entry, resource_dir;
    std::vector<int> arguments;
};
struct ScreenFlow { std::string screen_flow; GuiAppLayer layer; };
struct AppGuiDescriptor {
    GuiRootKind root_kind;
    std::string root;
    std::vector<int> resources;
    std::vector<ScreenFlow> screen_flows;
};
struct Gui {
    shell_test::Result subscribe_actions(const std::vector<std::string>&) {
        if (shell_test::fixture.fail_subscribe) return std::unexpected("subscribe failure");
        return {};
    }
    shell_test::Result set_text(std::string_view, const std::string &text) {
        shell_test::fixture.text = text;
        return {};
    }
    shell_test::Result quiesce_input(shell_test::Drain callback) {
        if (shell_test::fixture.fail_quiesce) return std::unexpected("quiesce failure");
        shell_test::fixture.drains.push_back(std::move(callback));
        return {};
    }
    shell_test::Result drain_pending_work() {
        if (shell_test::fixture.fail_drain) return std::unexpected("drain failure");
        return {};
    }
    shell_test::Result resume_input() {
        if (shell_test::fixture.fail_resume) return std::unexpected("resume failure");
        ++shell_test::fixture.resumes;
        return {};
    }
};
struct Timer {
    std::expected<TimerId, std::string> start_periodic(std::string_view, int) {
        if (shell_test::fixture.fail_timer) return std::unexpected("timer failure");
        ++shell_test::fixture.timers_started;
        return 1;
    }
    bool stop(TimerId) { return !shell_test::fixture.fail_stop; }
};
struct AppContext {
    Gui gui_;
    Timer timer_;
    Gui &gui() { return gui_; }
    Timer &timer() { return timer_; }
};
struct IApp {
    virtual ~IApp() = default;
    virtual AppManifest get_manifest() const = 0;
    virtual AppGuiDescriptor get_gui_descriptor() const = 0;
    virtual shell_test::Result on_start(AppContext&) { return {}; }
    virtual shell_test::Result on_stop(AppContext&) { return {}; }
    virtual shell_test::Result on_action(AppContext&, std::string_view) { return {}; }
    virtual shell_test::Result on_timer(AppContext&, TimerId, std::string_view) { return {}; }
};
struct IAppProvider {
    virtual ~IAppProvider() = default;
    virtual AppManifest get_manifest() const = 0;
    virtual std::shared_ptr<IApp> create_app() = 0;
};
}
#define BROOKESIA_SYSTEM_CORE_APP_PROVIDER_REGISTER_WITH_SYMBOL(...)

// Production startup selection with bounded canonical inputs and presentation boundaries.
#include "esp_err.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstddef>

// condition: Required production startup result.
// message: Actionable routing or deadline failure.
void require(bool condition, const char *message)
{
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
// tag: Actual production component identity.
// format: Actual diagnostic format.
// arguments: Diagnostic values consumed without suppressing compiler warnings.
template<typename... Args>
void host_log(const char *tag, const char *format, Args&&... arguments)
{
    (void)tag; (void)format; ((void)arguments, ...);
}
#define ESP_LOGE(...) host_log(__VA_ARGS__)
#define ESP_LOGI(...) host_log(__VA_ARGS__)

namespace LUNASHELL {
enum class DeviceHealth { Unknown, Ready, Degraded, Missing };
struct WifiSnapshot {
    bool data_valid = true, data_stale = false, credentials_configured = true;
    bool connected = false, ip_acquired = false, started = true;
    DeviceHealth health = DeviceHealth::Ready;
    esp_err_t last_error = ESP_OK;
    char ip_address[16] = "";
};
struct Link {
    bool data_valid = true, data_stale = false, pairing_repair_required = false;
    bool retained_link_present = true, pending_pairing_present = false;
    esp_err_t last_error = ESP_OK;
};
struct ConnectivityObservation { struct { WifiSnapshot wifi; Link codex_link; } radios; };
ConnectivityObservation canonical;
esp_err_t read_error = ESP_OK;
class DeviceSnapshotService {
public:
    // No parameters: deterministic host boundary for the existing canonical owner.
    static DeviceSnapshotService &instance() { static DeviceSnapshotService service; return service; }
    // observation: Receives the exact configured canonical sample; no fallback is synthesized.
    esp_err_t readConnectivityObservation(ConnectivityObservation &observation)
    {
        observation = canonical; return read_error;
    }
};
}

constexpr const char *kTag = "startup-host";
constexpr bool kCodexIntegrationEnabled = true;
enum class Route { None, Hello04, NetworkWait, CodexChoice, LookingForPc14, SetupMenu };
struct SceneFrame { unsigned marker = 73; };
Route s_route = Route::None;
bool s_startup_pending = false;
int64_t fake_now_us = 0;
unsigned presentation_calls = 0;
esp_err_t injected_presentation_error = ESP_OK, visible_error = ESP_OK;
enum class OnboardingAction { None, Later, NetworkSetup, DeclineCodex, AcceptCodex, ConnectCodex, Options };
struct Text { const char *data; std::size_t size; };
namespace CODY2D { struct TextLine {}; }
constexpr int kOptionsBounds = 0;
struct {
    unsigned lines[1]{};
    unsigned line_count = 0;
    bool actions_visible = false;
    OnboardingAction options_id = OnboardingAction::None;
    int options = 0;
} s_view;
bool primary_visible = false, alternative_visible = false;
esp_err_t s_startup_wifi_error = ESP_OK;
esp_err_t attachment_error = ESP_OK;
// No parameters: deterministic monotonic boot time, independent of rendering delay.
int64_t esp_timer_get_time() { return fake_now_us; }
// scene: Current frame receiving production title text.
// text: Exact bounded production title.
esp_err_t set_scene_title(SceneFrame &scene, Text text)
{
    (void)scene; (void)text; return injected_presentation_error;
}
// text: Exact production caption.
// y: Production caption position.
// line: Presenter-owned text destination.
esp_err_t prepare_caption(const char *text, int y, unsigned &line)
{
    (void)text; (void)y; (void)line; return injected_presentation_error;
}
// action: Production semantic identity.
// visible: Whether the primary control is admitted.
esp_err_t prepare_action(OnboardingAction action, bool visible)
{
    require(action == OnboardingAction::Later && visible, "recovery primary retains Later identity");
    primary_visible = visible; return injected_presentation_error;
}
// action: Production semantic identity.
// visible: Whether the secondary control is admitted.
esp_err_t prepare_alternative(OnboardingAction action, bool visible)
{
    require(action == OnboardingAction::NetworkSetup && visible, "recovery secondary retains Setup identity");
    alternative_visible = visible; return injected_presentation_error;
}
// scene: Frame attachment boundary; no synthetic geometry is tested here.
esp_err_t attach_content(SceneFrame &scene) { (void)scene; return attachment_error; }
// action: Setup-menu identity outside the exercised startup routes.
// label: Existing button caption.
// bounds: Host projection of the renderer-owned bounds.
// visible: Button admission flag.
// selected: Selection flag.
// button: Presenter-owned button output.
// text: Presenter-owned text output.
esp_err_t prepare_button(OnboardingAction action, const char *label, int bounds,
                         bool visible, bool selected, int &button, CODY2D::TextLine &text)
{
    (void)action; (void)label; (void)bounds; (void)visible; (void)selected; (void)button; (void)text;
    require(false, "startup fixture must not enter unrelated setup-menu options");
    return ESP_FAIL;
}
extern bool s_startup_network_passive;
// route: Actual production startup destination.
// scene: Frame passed to verbatim production passive/recovery control branches.
esp_err_t enter_route(Route route, SceneFrame &scene)
{
    s_route = route; ++presentation_calls; s_view = {};
    primary_visible = alternative_visible = false;
    if (injected_presentation_error != ESP_OK) { return injected_presentation_error; }
    // This matches the checked owner wipe boundary, not a replica of its route policy.
    SceneFrame &prepared = scene;
    esp_err_t error = ESP_OK;
#include "startup_route_controls_under_test.inc"
    s_view.actions_visible = primary_visible || alternative_visible;
    if (error == ESP_OK) { error = attach_content(scene); }
    return error;
}
// scene: Existing startup frame.
// error: Canonical CCP failure retained without claiming absence.
esp_err_t show_codex_error(SceneFrame &scene, esp_err_t error)
{
    (void)scene; visible_error = error; ++presentation_calls; return injected_presentation_error;
}
// scene: Existing startup frame.
// link: Exact canonical retained identity used by the production selector.
esp_err_t resume_codex_connection(SceneFrame &scene, const LUNASHELL::Link &link)
{
    require(link.retained_link_present || link.pending_pairing_present, "resume requires retained identity");
    return enter_route(Route::LookingForPc14, scene);
}

// The runner writes verbatim production grace state/helpers before compiling this file.
#include "startup_network_under_test.inc"

// No parameters: clear host observation/presentation state and use the real new-request reset.
void reset()
{
    LUNASHELL::canonical = {}; LUNASHELL::read_error = ESP_OK;
    s_route = Route::None; fake_now_us = 0; presentation_calls = 0;
    injected_presentation_error = visible_error = s_startup_wifi_error = ESP_OK;
    attachment_error = ESP_OK; s_view = {};
    primary_visible = alternative_visible = false;
    request_saved_setup_startup();
}
// No parameters: publish an actual valid connectivity projection at the reader boundary.
void make_online()
{
    auto &wifi = LUNASHELL::canonical.radios.wifi;
    wifi.connected = wifi.ip_acquired = true;
    wifi.ip_address[0] = '1';
}
// No parameters: execute real startup selector, grace gate, request reset and passive presenter.
int main()
{
    SceneFrame scene;
    bool changed = false;
    reset(); fake_now_us = 60000000;
    require(evaluate_saved_setup(scene, changed) == ESP_OK && changed && s_startup_pending &&
            s_startup_network_passive && !primary_visible && !alternative_visible &&
            s_startup_wifi_wait_started_us == 60000000,
            "branding before first evaluation does not consume visible grace");
    changed = false; fake_now_us += 14999999;
    const unsigned previous_presentations = presentation_calls;
    require(evaluate_saved_setup(scene, changed) == ESP_OK && !changed &&
            presentation_calls == previous_presentations && s_startup_wifi_wait_started_us == 60000000,
            "repeated evaluation neither extends grace nor rebuilds unchanged view");
    ++fake_now_us;
    require(evaluate_saved_setup(scene, changed) == ESP_OK && changed && !s_startup_network_passive &&
            primary_visible && alternative_visible && s_route == Route::NetworkWait,
            "exact 15-second expiry restores existing recovery choices");
    fake_now_us += 1000000; changed = false;
    require(evaluate_saved_setup(scene, changed) == ESP_OK && !changed && !s_startup_network_passive,
            "expired grace stays expired across later frames");
    request_saved_setup_startup();
    require(s_startup_wifi_wait_started_us == -1 && !s_startup_network_passive && !s_startup_network_presented,
            "new request clears old grace rather than inheriting expired deadline");
    fake_now_us += 60000000;
    require(evaluate_saved_setup(scene, changed) == ESP_OK && s_startup_network_passive &&
            s_startup_wifi_wait_started_us == fake_now_us, "new request starts fresh on first eligible evaluation");

    reset(); evaluate_saved_setup(scene, changed); fake_now_us = 1; make_online();
    require(evaluate_saved_setup(scene, changed) == ESP_OK && !s_startup_pending &&
            s_route == Route::LookingForPc14, "healthy connection advances immediately inside grace");
    reset(); make_online();
    require(evaluate_saved_setup(scene, changed) == ESP_OK && s_route == Route::LookingForPc14 &&
            s_startup_wifi_wait_started_us == -1, "already online never starts grace");

    reset(); LUNASHELL::canonical.radios.wifi.credentials_configured = false;
    require(evaluate_saved_setup(scene, changed) == ESP_OK && s_route == Route::Hello04 &&
            !s_startup_pending && s_startup_wifi_wait_started_us == -1,
            "absent credentials enter first setup immediately");
    reset(); LUNASHELL::canonical.radios.wifi.credentials_configured = false;
    LUNASHELL::canonical.radios.wifi.last_error = ESP_FAIL;
    require(evaluate_saved_setup(scene, changed) == ESP_FAIL && presentation_calls == 0,
            "credential storage failure cannot become absent profile");

    reset(); LUNASHELL::canonical.radios.wifi.data_stale = true;
    LUNASHELL::canonical.radios.wifi.last_error = ESP_ERR_TIMEOUT;
    require(evaluate_saved_setup(scene, changed) == ESP_OK && s_startup_network_passive &&
            s_startup_wifi_error == ESP_ERR_TIMEOUT, "stale saved sample retains real error during grace");
    fake_now_us = 15000000;
    require(evaluate_saved_setup(scene, changed) == ESP_OK && !s_startup_network_passive &&
            s_startup_wifi_error == ESP_ERR_TIMEOUT && primary_visible && alternative_visible,
            "expired stale sample exposes recovery with original failure");
    reset(); make_online(); LUNASHELL::canonical.radios.wifi.health = LUNASHELL::DeviceHealth::Degraded;
    require(evaluate_saved_setup(scene, changed) == ESP_OK && s_startup_network_passive,
            "connected booleans do not bypass canonical health");
    reset(); LUNASHELL::read_error = ESP_ERR_NO_MEM;
    require(evaluate_saved_setup(scene, changed) == ESP_ERR_NO_MEM && presentation_calls == 0 &&
            s_startup_wifi_wait_started_us == -1, "reader failure remains original and does not start grace");
    reset(); injected_presentation_error = ESP_ERR_INVALID_SIZE;
    require(evaluate_saved_setup(scene, changed) == ESP_ERR_INVALID_SIZE &&
            !s_startup_network_presented && !s_view.actions_visible,
            "presentation failure survives startup selection and blocks actions");
    require(evaluate_saved_setup(scene, changed) == ESP_ERR_INVALID_SIZE && presentation_calls == 2,
            "same-view retry cannot conceal a failed route render");
    reset(); attachment_error = ESP_ERR_NO_MEM;
    require(evaluate_saved_setup(scene, changed) == ESP_ERR_NO_MEM && !s_startup_network_presented &&
            !s_view.actions_visible && !primary_visible && !alternative_visible,
            "failed final attachment leaves no admitted recovery controls");
    require(evaluate_saved_setup(scene, changed) == ESP_ERR_NO_MEM && presentation_calls == 2,
            "failed final attachment is retried and retains its real failure");
    attachment_error = ESP_OK;
    require(evaluate_saved_setup(scene, changed) == ESP_OK && s_startup_network_presented &&
            presentation_calls == 3, "successful attachment commits stable passive presentation");
    fake_now_us = 15000000; attachment_error = ESP_ERR_NO_MEM;
    require(evaluate_saved_setup(scene, changed) == ESP_ERR_NO_MEM && !s_startup_network_presented &&
            !s_view.actions_visible, "failed recovery attachment cannot admit newly prepared controls");
    std::puts("Production startup network grace contracts passed");
    return 0;
}

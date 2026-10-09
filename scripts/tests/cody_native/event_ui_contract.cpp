#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#include "esp_log.h"
#define ESP_LOGW(tag, ...) host_error_log(tag, __VA_ARGS__)

// Compile the real presenter in this translation unit so the fixture can inspect
// its private, immutable prepared controls. Unused worker/network paths are not
// linked; no production-only test branch or copied layout implementation exists.
#include "../../../components/CODY_companion/src/scenes/CODY_onboarding.cpp"

namespace CODY {
// scene: Not used; entering the look selector is outside this event-only fixture.
// control_revision: Not used; the test must stay inside event routes.
// Exception: Aborts on accidental cross-feature execution.
esp_err_t attach_look_content(SceneFrame &, uint32_t)
{
    std::fputs("Unexpected look route in event layout fixture\n", stderr);
    std::abort();
}
} // namespace CODY

namespace {
using namespace CODY;
using Kind = LUNASHELL::CodexEventKind;
using State = LUNASHELL::CodexEventState;

// condition: Actual native result or invariant under test.
// message: Identifies the public behavior whose regression must stop this check.
// Exception: Exits with failure; assertions remain active in optimized host builds.
void require(bool condition, const char *message)
{
    if (!condition) {
        std::fprintf(stderr, "Event UI contract failed: %s\n", message);
        std::exit(1);
    }
}

// target: Fixed canonical wire field including its null terminator.
// value: Deliberately bounded fixture content, never real user data.
template <size_t Size>
void copy_field(char (&target)[Size], const std::string &value)
{
    require(value.size() < Size, "fixture fits canonical field");
    std::memcpy(target, value.c_str(), value.size() + 1U);
}

// No parameters: reset only presenter-owned memory before an independent fixture.
void start_question()
{
    reset_event_presentation();
    s_presented_event.kind = Kind::Question;
    s_presented_event.state = State::Pending;
    s_presented_event.last_error = ESP_OK;
    s_presented_event.data_valid = true;
    copy_field(s_presented_event.request_id, std::string(32, 'a'));
    copy_field(s_presented_event.title, std::string(24, 'T'));
    copy_field(s_presented_event.body, std::string(63, 'B'));
    s_presented_event.choice_count = 3;
    const char *identities[] = {"first", "second", "third"};
    const char *labels[] = {"ABCDEFGHIJKL", "Choose other", "Maybe later "};
    const LUNASHELL::CodexChoiceTone tones[] = {LUNASHELL::CodexChoiceTone::Positive,
        LUNASHELL::CodexChoiceTone::Negative, LUNASHELL::CodexChoiceTone::Info};
    for (uint8_t index = 0; index < 3; ++index) {
        auto &choice = s_presented_event.choices[index];
        copy_field(choice.id, identities[index]);
        copy_field(choice.label, labels[index]);
        copy_field(choice.detail, std::string(63, static_cast<char>('D' + index)));
        choice.tone = tones[index];
    }
    s_event_online = true;
}

// route: Actual private presenter route selected by the fixture.
// identity: Registered scene identity whose real generated geometry is used.
// Exception: Stops on missing generation, invalid text or circle-layout failures.
SceneFrame prepare(Route route, SceneId identity)
{
    s_view = {};
    s_route = route;
    const SceneDefinition *definition = nullptr;
    require(find_scene(identity, definition) == ESP_OK, "event scene is generated and registered");
    SceneFrame scene;
    require(begin_scene(*definition, scene) == ESP_OK, "authored event scene is valid");
    require(prepare_event_content(scene) == ESP_OK, "canonical event payload fits native presentation");
    require(attach_content(scene) == ESP_OK, "content and controls fit the real scene validator");
    return scene;
}

// bounds: Prepared raster or hit rectangle in the display's logical coordinates.
void require_circle(CODY2D::Rect bounds)
{
    for (int row : {static_cast<int>(bounds.y), static_cast<int>(bounds.y + bounds.height - 1U)}) {
        for (int column : {static_cast<int>(bounds.x), static_cast<int>(bounds.x + bounds.width - 1U)}) {
            const int dx = column - 233;
            const int dy = row - 233;
            require(dx * dx + dy * dy <= 233 * 233, "occupied bounds fit every circular chord");
        }
    }
}

// No parameters: every prepared narrative row must remain separate from later rows and actions.
void require_text_layout()
{
    uint16_t previous_bottom = 0;
    for (uint8_t index = 0; index < s_view.line_count; ++index) {
        const auto &bounds = s_view.lines[index].bounds;
        require(bounds.y >= previous_bottom, "event text rows never overlap");
        previous_bottom = bounds.y + bounds.height;
        require_circle(bounds);
        if (s_view.action_visible) {
            require(previous_bottom <= s_view.action.bounds.y, "narrative ends before the single action");
        }
    }
}

// text: Authored text whose space separators may become line breaks.
std::string nonspace(std::string text)
{
    text.erase(std::remove(text.begin(), text.end(), ' '), text.end());
    return text;
}

// No parameters: legal long words cannot expand a 63-character payload past three rows.
void check_bounded_text()
{
    const std::vector<std::string> texts = {std::string(63, 'W'),
        "a " + std::string(26, 'b') + " c " + std::string(26, 'd') + " tail",
        std::string(63, ' '), " a b c d e f g h i j k l m n o p q r s t u v w x y z 0123456789"};
    for (const auto &text : texts) {
        require(text.size() <= 63, "adversarial text is legal wire data");
        s_view = {};
        uint16_t top = 222;
        require(append_event_text(text.c_str(), top) == ESP_OK, "word wrap accepts every bounded legal body");
        require(s_view.line_count <= 3, "word wrapping never exceeds hard-wrap line budget");
        std::string joined;
        for (uint8_t index = 0; index < s_view.line_count; ++index) {
            joined.append(s_view.lines[index].text.characters, s_view.lines[index].text.length);
        }
        require(nonspace(joined) == nonspace(text), "body content is never truncated or substituted");
        start_question();
        copy_field(s_presented_event.body, text);
        prepare(Route::DecisionNotice26, SceneId::DecisionNotice);
        require_text_layout();
        copy_field(s_event_selected_id, "first");
        copy_field(s_presented_event.choices[0].detail, text);
        prepare(Route::DecisionConfirm27, SceneId::DecisionConfirm);
        require_text_layout();
    }
}

// scene: Prepared actual presenter, advanced right up to its final narrative character.
void complete_narrative(SceneFrame &scene)
{
    uint32_t length = scene.title.text.length;
    for (uint8_t index = 0; index < s_view.line_count; ++index) {
        if (!s_view.instant_lines[index]) {
            length += s_view.lines[index].text.length;
        }
    }
    const int64_t duration_us = static_cast<int64_t>(length) * scene.definition->character_interval_ms * 1000;
    require(duration_us > 0, "fixture exercises the authored teletype cadence");
    require(advance_scene(scene, duration_us - 1) == ESP_OK, "narrative advances to its last character");
    require(attach_content(scene) == ESP_OK && !s_view.actions_visible,
        "no caption or touch action is admitted before the entire event narrative finishes");
    require(advance_scene(scene, 1) == ESP_OK && attach_content(scene) == ESP_OK,
        "narrative completes on the actual cadence boundary");
    require(s_view.actions_visible, "complete controls appear together after narration");
    require(scene.content.visible_characters == length - scene.title.text.length,
        "button captions and instant statuses do not enter typewriter/audio counts");
}

// No parameters: actual card rendering and typed labels share the full-control visibility gate.
void check_choice_cards()
{
    for (uint8_t count : {2, 3}) {
        start_question();
        s_presented_event.choice_count = count;
        SceneFrame scene = prepare(Route::DecisionChoices27, SceneId::DecisionChoices);
        require(s_view.event_choice_count == count, "all offered choices retain their stable identities");
        std::vector<uint16_t> pixels(466U * 466U, 0);
        const CODY2D::Surface surface{pixels.data(), 466, 466, 466,
            static_cast<uint32_t>(pixels.size()), CODY2D::Rgb565ByteOrder::Native};
        require(draw_content(&s_view, surface, 0, 0) == ESP_OK, "hidden card frame renders");
        require(std::all_of(pixels.begin() + 260U * 466U, pixels.end(), [](uint16_t value) { return value == 0; }),
            "cards and their captions are entirely absent before the narrative gate");
        complete_narrative(scene);
        require(previous_control_visible() && !next_navigation_available(),
            "only explicit visible choice controls and canonical Back navigate");
        for (uint8_t index = 0; index < count; ++index) {
            const auto &control = s_view.event_choices[index].control;
            require_circle(control.bounds);
            require(control.icon != CODY2D::ActionIcon::None && control.top_color != control.bottom_color,
                "every dynamic choice uses its semantic gradient and icon");
            require(std::strcmp(s_view.event_choices[index].id, s_presented_event.choices[index].id) == 0,
                "identity is copied without depending on caption or array position");
            require(CODY2D::capsule_contains(control, {static_cast<int16_t>(control.bounds.x + control.bounds.width / 2U),
                static_cast<int16_t>(control.bounds.y + control.bounds.height / 2U)}),
                "complete card retains its shared raster/hit geometry");
        }
        require(s_view.event_choices[0].control.top_color != s_view.event_choices[1].control.top_color,
            "positive and negative choices do not share a blue-only palette");
        require(draw_content(&s_view, surface, 0, 0) == ESP_OK, "all complete cards render after narration");
        require(std::any_of(pixels.begin() + 260U * 466U, pixels.end(), [](uint16_t value) { return value != 0; }),
            "visible card pixels appear only after the gate");
    }
}

// No parameters: readonly PR details use every authenticated check name without an action per row.
void check_pr_views()
{
    start_question();
    s_presented_event.kind = Kind::PrPending;
    prepare(Route::PrPending21, SceneId::PrPending);
    require(!s_view.action_visible && s_view.event_choice_count == 0, "PR pending is informational");
    s_presented_event.kind = Kind::ChecksRunning;
    s_presented_event.progress_valid = false;
    s_presented_event.passed = s_presented_event.total = UINT16_MAX;
    prepare(Route::ChecksRunning22, SceneId::ChecksRunning);
    require(!s_view.action_visible, "running checks expose no rerun or merge action");
    require(std::strcmp(s_view.lines[s_view.line_count - 1U].text.characters, "Progress unavailable") == 0,
        "invalid numeric check progress remains unavailable");
    s_presented_event.kind = Kind::ChecksPassed;
    s_presented_event.progress_valid = true;
    require(codex_event_destination() == Route::ChecksRunning22, "passed checks use screen22 without an action screen25");
    prepare(Route::ChecksRunning22, SceneId::ChecksRunning);
    require(!s_view.action_visible, "passed checks remain informational during their readable dwell");
    require_text_layout();
    s_presented_event.kind = Kind::ChecksFailed;
    prepare(Route::ChecksFailed23, SceneId::ChecksFailed);
    require(s_view.action_id == OnboardingAction::ViewFailures, "failed checks expose only View failures");
    s_presented_event.failure_count = 6;
    for (uint8_t index = 0; index < 6; ++index) {
        copy_field(s_presented_event.failed_checks[index],
            std::string(24, static_cast<char>('a' + index)) + " " + std::string(23, 'X'));
    }
    for (uint8_t page : {0, 1}) {
        s_event_failure_page = page;
        prepare(Route::CheckFailures24, SceneId::CheckFailures);
        require(!s_view.action_visible && s_view.event_choice_count == 0, "failure names are readonly");
        require(s_view.line_count == 7, "three exact two-line names plus one scroll hint fit each page");
        require_text_layout();
        for (uint8_t index = 0; index < 3; ++index) {
            const auto &first = s_view.lines[index * 2U].text;
            const auto &second = s_view.lines[index * 2U + 1U].text;
            const std::string name = std::string(first.characters, first.length) +
                std::string(second.characters, second.length);
            require(name == s_presented_event.failed_checks[page * 3U + index],
                "48-character check names retain spaces, suffixes and complete identity");
        }
    }
}

// No parameters: event lifecycle gates are read from the real presenter, without a mock response model.
void check_reply_semantics()
{
    start_question();
    copy_field(s_event_selected_id, "first");
    s_event_stage = EventStage::Confirm;
    s_event_online = false;
    require(codex_event_destination() == Route::DecisionConfirm27, "offline retains the selected subroute");
    SceneFrame scene = prepare(Route::DecisionConfirm27, SceneId::DecisionConfirm);
    require(s_view.action_id == OnboardingAction::SendReply && !s_view.action.enabled,
        "a new reply cannot be sent offline");
    complete_narrative(scene);
    require(!CODY2D::capsule_contains(s_view.action, {233, 388}), "disabled Send has no active touch target");
    s_presented_event.state = State::Sending;
    require(codex_event_destination() == Route::DecisionWaiting, "admitted reply becomes immutable waiting");
    s_presented_event.state = State::Received;
    require(codex_event_destination() == Route::DecisionWaiting, "CCP receipt never claims delivery");
    scene = prepare(Route::DecisionWaiting, SceneId::DecisionWaiting);
    require(!s_view.action_visible && !previous_navigation_available(), "waiting cannot change a submitted choice");
    s_presented_event.state = State::Delivered;
    require(codex_event_destination() == Route::ReplySent31, "only delivered state reaches Reply sent");
    for (State state : {State::Expired, State::Cancelled, State::Rejected}) {
        s_presented_event.state = state;
        require(codex_event_destination() == Route::CodexNotice, "terminal errors are not success screens");
        prepare(Route::CodexNotice, SceneId::CodexNotice);
        require(s_view.action_id == OnboardingAction::DismissEvent, "terminal states expose Done without resubmission");
        require_text_layout();
    }
    s_presented_event.state = State::Expired;
    copy_field(s_presented_event.selected_id, "first");
    scene = prepare(Route::CodexNotice, SceneId::CodexNotice);
    require(std::string(scene.title.text.characters, scene.title.text.length) == "Reply unconfirmed",
        "expired admitted reply never implies the recipient took no action");
    require(std::strcmp(s_view.lines[1].text.characters, "Delivery unconfirmed") == 0 &&
        std::strcmp(s_view.lines[2].text.characters, "Check on your PC") == 0,
        "lost acknowledgement leaves an explicit unconfirmed outcome");
    s_presented_event.kind = Kind::ApprovalOnPc;
    s_presented_event.state = State::Pending;
    prepare(Route::ApprovalOnPc, SceneId::ApprovalOnPc);
    require(!s_view.action_visible && s_view.event_choice_count == 0, "native permissions stay on the PC");
}
} // namespace

// No parameters: compile/run is available only through the explicitly requested native suite.
int main()
{
    check_bounded_text();
    check_choice_cards();
    check_pr_views();
    check_reply_semantics();
    std::puts("Real Cody event presenter: bounded text, semantic cards, teletype gates, readonly PR and delivered-only success passed");
    return 0;
}

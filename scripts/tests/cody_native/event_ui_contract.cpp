/* VBWR B
 *
 * Project: ESP32 Luna Shell
 * Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
 * Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
 *
 * VibeWare: Human intent, AI execution, and plenty of tokens
 * Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
 *
 * Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
 * Human guidance: Umberto Giacobbi; requested VibeWare branding.
 *
 * Copyright (c) 2026 Umberto Giacobbi
 *
 * VBWR E */

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
// its private, immutable prepared controls. Owner boundaries abort if entered;
// no production-only test branch or copied layout implementation exists.
#include "../../../components/CODY_companion/src/scenes/CODY_onboarding.cpp"

// No parameters: shared-model regression scenarios never enter device owners.
void verify_shared_event_presentation();

namespace HomeFixture {
extern const LUNASHELL::CodexTechnicalObservation *observation;
extern int64_t now_us;
extern esp_err_t read_error;
extern unsigned inactive_look_resets;
// No parameters: inspect the real route; never emulate an active Look lifecycle.
bool look_is_inactive()
{
    return CODY::s_route != CODY::Route::Look11;
}
}

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

// No parameters: prepares cadence boundaries and owner-state retirement without playing audio.
// Exception: Stops when a pending question replays, catches up, or survives a terminal outcome.
void check_question_audio_schedule()
{
    start_question();
    QuestionAudioSchedule schedule{};
    // now_us: Synthetic awake clock, never a physical timing claim.
    // current: Simulated visibility and canonical freshness.
    // replied: Simulated owner admission of the user's response.
    const auto due = [&](int64_t now_us, bool current = true, bool replied = false) {
        return question_audio_due(s_presented_event, current, replied, now_us, schedule);
    };
    require(due(0) == QuestionAudioAlert::Initial, "fresh question starts the longer initial alert");
    require(due(9999999) == QuestionAudioAlert::None, "no reminder before ten seconds");
    require(due(10000000) == QuestionAudioAlert::Reminder, "four-note reminder at ten seconds");
    require(due(10000000) == QuestionAudioAlert::None, "status polling cannot duplicate an alert");
    require(due(90000000) == QuestionAudioAlert::Reminder && due(90000000) == QuestionAudioAlert::None,
        "late scheduling attempts one reminder without a catch-up burst");
    require(due(100000000, false) == QuestionAudioAlert::None &&
        due(110000000) == QuestionAudioAlert::None && due(120000000) == QuestionAudioAlert::Reminder,
        "hidden or offline periods renew cadence without replaying the initial alert");
    require(due(130000000, true, true) == QuestionAudioAlert::None,
        "admitted Send stops alerts before delivery confirmation");
    for (const auto state : {State::Sending, State::Received, State::Delivered, State::Cancelled,
                             State::Expired, State::Rejected}) {
        s_presented_event.state = state;
        require(due(200000000) == QuestionAudioAlert::None, "non-pending owner states stop reminders");
    }
    s_presented_event.state = State::Pending;
    s_presented_event.data_stale = true;
    require(due(210000000) == QuestionAudioAlert::None, "stale retained questions cannot sound current");
    s_presented_event.data_stale = false;
    copy_field(s_presented_event.request_id, std::string(32, 'b'));
    require(due(220000000) == QuestionAudioAlert::Initial, "a new stable request has its own first alert");
}

// route: Actual private presenter route selected by the fixture.
// identity: Registered scene identity whose real generated geometry is used.
// Exception: Stops on missing generation, invalid text or circle-layout failures.
SceneFrame prepare(Route route, SceneId identity)
{
    s_view = {};
    s_route = route;
    s_event_stage = route == Route::DecisionQueue ? EventStage::Questions :
        route == Route::DecisionChoices27 ? EventStage::Choices :
        route == Route::DecisionConfirm27 ? EventStage::Confirm :
        route == Route::CheckFailures24 ? EventStage::Failures : EventStage::Notice;
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
        event_presentation::Plan wrapping;
        const auto &font = SceneData::body_font;
        const size_t capacity = (300U - font.width) / font.advance + 1U;
        require(event_presentation::append_lines(wrapping, text.c_str(), top, capacity) == ESP_OK,
            "production word wrap accepts every bounded legal body");
        for (uint8_t index = 0; index < wrapping.line_count; ++index) {
            require(append_event_line(wrapping.lines[index]) == ESP_OK,
                "firmware renderer accepts every fully wrapped shared line");
        }
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

// No parameters: numbered rows share raster/hit geometry and the complete narrative gate.
void check_choice_cards()
{
    for (uint8_t count : {uint8_t{2}, uint8_t{3}}) {
        start_question();
        s_presented_event.choice_count = count;
        SceneFrame scene = prepare(Route::DecisionChoices27, SceneId::DecisionChoices);
        require(s_view.event_choice_count == 2, "only two offered replies are visible at once");
        std::vector<uint16_t> pixels(466U * 466U, 0);
        const CODY2D::Surface surface{pixels.data(), 466, 466, 466,
            static_cast<uint32_t>(pixels.size()), CODY2D::Rgb565ByteOrder::Native};
        require(draw_content(&s_view, surface, 0, 0) == ESP_OK, "hidden card frame renders");
        require(std::all_of(pixels.begin() + 260U * 466U, pixels.end(), [](uint16_t value) { return value == 0; }),
            "cards and their captions are entirely absent before the narrative gate");
        complete_narrative(scene);
        require(previous_control_visible() && !next_navigation_available(),
            "only explicit visible choice controls and canonical Back navigate");
        for (uint8_t index = 0; index < s_view.event_choice_count; ++index) {
            const auto &control = s_view.event_choices[index].control;
            require_circle(control.bounds);
            require(control.icon == CODY2D::ActionIcon::None && control.top_color != control.bottom_color,
                "every reply is text-only and retains its semantic gradient");
            require(control.bounds.x == 73 && control.bounds.width == 320 && !s_view.event_choices[index].second.valid,
                "numbered replies occupy a single line across the full safe width");
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

// No parameters: long original replies remain readable and only Send requests transport.
// Exception: Stops on lost text, an implicit reply, hidden selection or invalid circular bounds.
void check_paged_reply_review()
{
    namespace P = event_presentation;
    start_question();
    copy_field(s_presented_event.choices[2].label, std::string(63, 'L'));
    copy_field(s_presented_event.choices[2].detail, std::string(255, 'D'));
    s_event_choice_page = 1;
    SceneFrame scene = prepare(Route::DecisionChoices27, SceneId::DecisionChoices);
    require(s_view.event_choice_count == 1 && std::strcmp(s_view.event_choices[0].id, "third") == 0,
        "the third reply is accessible without changing its semantic identity");
    require(std::strstr(s_event_plan.actions[0].label, "...") != nullptr,
        "an oversized one-line label visibly announces its continuation");
    complete_narrative(scene);
    auto navigation = event_navigation();
    P::Effect effect;
    require(P::activate(s_presented_event, event_context(), s_event_plan.actions[0], navigation, effect) == ESP_OK &&
        effect == P::Effect::Render, "choosing a row only opens its full review");
    retain_event_navigation(navigation, effect);
    std::string reviewed;
    uint8_t pages = 0;
    for (uint8_t page = 0; page == 0 || page < pages; ++page) {
        s_event_detail_page = page;
        scene = prepare(Route::DecisionConfirm27, SceneId::DecisionConfirm);
        require_text_layout();
        pages = s_event_plan.page_count;
        for (uint8_t index = 0; index < s_event_plan.line_count; ++index) {
            if (s_event_plan.lines[index].top < 326) { reviewed += s_event_plan.lines[index].text; }
        }
        require(std::strcmp(s_event_plan.actions[0].label, "Send reply") == 0,
            "the full review always has an explicit Send reply action");
    }
    require(pages == 3 && reviewed == "3. " + std::string(63, 'L') + std::string(255, 'D'),
        "all canonical label and detail characters survive every review page");
    navigation = event_navigation();
    const P::Action *action = nullptr;
    require(P::release(s_event_plan, navigation, true, 233, 240, 233, 240, action) == P::Effect::Render &&
        navigation.detail_page == 0 && action == nullptr, "tapping the last detail page returns to its beginning");
    require(P::release(s_event_plan, navigation, true, 233, 385, 233, 385, action) == P::Effect::None &&
        action != nullptr && action->id == P::ActionId::Send, "tapping Send does not scroll the review");
    require(P::activate(s_presented_event, event_context(), *action, navigation, effect) == ESP_OK &&
        effect == P::Effect::Reply, "only the explicit Send tap yields a reply effect");
}

// No parameters: the bounded canonical queue exposes questions without answering any.
// Exception: Stops on an opaque caption, offline handoff or premature selection success.
void check_pending_question_rows()
{
    namespace P = event_presentation;
    start_question();
    s_presented_event.pending_question_count = 3;
    s_presented_event.pending_entry_count = 2;
    for (uint8_t index = 0; index < 2; ++index) {
        auto &pending = s_presented_event.pending_questions[index];
        copy_field(pending.request_id, std::string(32, static_cast<char>('a' + index)));
        copy_field(pending.title, index == 0 ? "Review build mode" : "Choose next task");
        copy_field(pending.context, "SIMULATED project");
    }
    SceneFrame scene = prepare(Route::DecisionQueue, SceneId::DecisionChoices);
    require(s_view.event_choice_count == 2 && std::strcmp(s_event_plan.title, "Pending questions") == 0,
        "pending questions share two full-width rows with a clear title");
    require_text_layout();
    complete_narrative(scene);
    auto navigation = event_navigation();
    P::Effect effect;
    require(P::activate(s_presented_event, event_context(), s_event_plan.actions[1], navigation, effect) == ESP_OK &&
        effect == P::Effect::SelectQuestion && navigation.stage == EventStage::Questions,
        "opening another question requests an owner handoff and waits for its real event");
    require(P::activate(s_presented_event, event_context(), s_event_plan.actions[0], navigation, effect) == ESP_OK &&
        effect == P::Effect::Render && navigation.stage == EventStage::Choices,
        "the already retained question can open directly without transport");
    s_event_online = false;
    prepare(Route::DecisionQueue, SceneId::DecisionChoices);
    require(!s_event_plan.actions[0].enabled && !s_event_plan.actions[1].enabled,
        "offline queue entries cannot request a source handoff");
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
    for (uint8_t page : {uint8_t{0}, uint8_t{1}}) {
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

// No parameters: typed background replies use the real shared layout, native fonts and dismissal intent.
// Exception: Stops on repeated copy, lost chat text, invalid circular bounds or an implied task outcome.
void check_reply_ready_notice()
{
    namespace P = event_presentation;
    const std::vector<std::string> titles{
        "A SIMULATED separate chat about the circular display",
        std::string(63, 'W'),
        "Open the other chat in Codex"
    };
    for (const auto &chat_title : titles) {
        reset_event_presentation();
        s_presented_event.kind = Kind::ReplyReady;
        s_presented_event.state = State::Pending;
        s_presented_event.last_error = ESP_OK;
        s_presented_event.data_valid = true;
        copy_field(s_presented_event.request_id, std::string(32, 'd'));
        copy_field(s_presented_event.title, "Reply ready");
        copy_field(s_presented_event.body, chat_title);
        copy_field(s_presented_event.context, "SIMULATED Cody project");
        s_event_online = true;
        require(codex_event_destination() == Route::CodexNotice,
            "typed reply readiness uses a readonly notification route");
        auto scene = prepare(Route::CodexNotice, SceneId::CodexNotice);
        require(std::string(scene.title.text.characters, scene.title.text.length) == "Reply ready" &&
            scene.title.color == 0x5FF9 && scene.title.origin.y == 150,
            "one mint Reply ready headline replaces From Codex");
        require(s_view.line_count >= 2 && s_view.line_count <= 4,
            "complete bounded chat title and one subordinate context line fit the notice");
        std::string displayed_title;
        for (uint8_t index = 0; index + 1U < s_view.line_count; ++index) {
            const auto &line = s_view.lines[index];
            require(line.origin.y == 214 + index * 28 && !s_view.instant_lines[index],
                "chat text owns the separated primary rows below the single headline");
            displayed_title.append(line.text.characters, line.text.length);
        }
        require(nonspace(displayed_title) == nonspace(chat_title),
            "the originating chat title or honest fallback remains complete without a GUID");
        const auto &context_line = s_view.lines[s_view.line_count - 1U];
        require(context_line.origin.y == 312 && context_line.color == 0xBDF7 &&
            std::strcmp(context_line.text.characters, "SIMULATED Cody project") == 0 &&
            s_view.instant_lines[s_view.line_count - 1U],
            "project context is visually subordinate and excluded from narration timing");
        require_text_layout();
        require_circle(s_view.action.bounds);
        require(s_view.action_id == OnboardingAction::DismissEvent &&
            std::strcmp(s_event_plan.actions[0].label, "Got it") == 0 &&
            s_event_plan.actions[0].tone == LUNASHELL::CodexChoiceTone::Info &&
            s_view.action.icon == CODY2D::ActionIcon::None &&
            s_view.action.bounds.x == 113 && s_view.action.bounds.y == 352 &&
            s_view.action.bounds.width == 240 && s_view.action.bounds.height == 72,
            "Got it retains one proven iconless informational capsule without a success symbol");
        require(!P::hit_action(s_event_plan, s_view.actions_visible, 233, 388),
            "the acknowledgement cannot be tapped before its complete narrative is visible");
        complete_narrative(scene);
        P::Navigation navigation{};
        P::Effect effect{};
        require(P::activate(s_presented_event, event_context(), s_event_plan.actions[0], navigation, effect) == ESP_OK &&
            effect == P::Effect::Dismiss && s_presented_event.state == State::Pending,
            "Got it requests the existing signed dismissal and never answers or invents task success");
        int64_t readable_at = -1;
        require(!P::checks_passed_dismiss_due(s_presented_event, event_context(), true, 9000000, readable_at) &&
            readable_at == -1, "reply readiness cannot trigger checks-passed auto-dismissal");

        std::vector<uint16_t> full(466U * 466U);
        require(draw_scene_rows(scene, {full.data(), 466, 466, 466, static_cast<uint32_t>(full.size())}, 0, 0) == ESP_OK,
            "the complete reply-ready view draws through the native production compositor");
        require(std::find(full.begin(), full.end(), 0x5FF9) != full.end(),
            "the mint headline is present in actual native pixels");
        for (const auto order : {CODY2D::Rgb565ByteOrder::Native, CODY2D::Rgb565ByteOrder::ByteSwapped}) {
            for (const uint16_t rows : {2, 16}) {
                std::vector<uint16_t> tiled(full.size());
                for (uint16_t first = 0; first < 466; first += rows) {
                    const CODY2D::Surface strip{tiled.data() + first * 466U, 466,
                        static_cast<uint16_t>(std::min<unsigned>(rows, 466 - first)), 466,
                        static_cast<uint32_t>(tiled.size() - first * 466U), order};
                    require(draw_scene_rows(scene, strip, first, 0) == ESP_OK,
                        "reply-ready layout respects real DMA row partitions");
                }
                for (size_t pixel = 0; pixel < full.size(); ++pixel) {
                    const uint16_t expected = order == CODY2D::Rgb565ByteOrder::Native ? full[pixel] :
                        CODY2D::swap_rgb565_bytes(full[pixel]);
                    require(tiled[pixel] == expected, "reply-ready native/swapped and full/tiled pixels match");
                }
            }
        }
    }

    s_event_online = false;
    prepare(Route::CodexNotice, SceneId::CodexNotice);
    require(std::strcmp(s_view.lines[s_view.line_count - 1U].text.characters, "Offline - saved notice") == 0,
        "an offline reply notice retains its original content with explicit link state");
    s_event_online = true;
    s_event_ui_error = ESP_ERR_TIMEOUT;
    s_event_retry = EventRetry::Dismiss;
    prepare(Route::CodexNotice, SceneId::CodexNotice);
    require(s_view.action_id == OnboardingAction::RetryEvent &&
        std::strcmp(s_view.lines[s_view.line_count - 1U].text.characters, "Error: 0x00000107") == 0,
        "failed dismissal retains its real cause and explicit retry");
    s_event_ui_error = ESP_OK;
    s_event_retry = EventRetry::None;
    s_event_dismiss_admitted = true;
    prepare(Route::CodexNotice, SceneId::CodexNotice);
    require(!s_view.action.enabled, "an admitted acknowledgement cannot be repeated");
    s_event_dismiss_admitted = false;
    s_presented_event.state = State::Expired;
    auto expired = prepare(Route::CodexNotice, SceneId::CodexNotice);
    require(std::string(expired.title.text.characters, expired.title.text.length) == "Request expired" &&
        expired.title.color == 0xFFFF, "expiration keeps its truthful terminal outcome instead of reply-ready emphasis");

    s_presented_event.kind = Kind::Notice;
    s_presented_event.state = State::Pending;
    s_presented_event.context[0] = '\0';
    copy_field(s_presented_event.title, "Reply ready");
    copy_field(s_presented_event.body, "Reply ready");
    auto generic = prepare(Route::CodexNotice, SceneId::CodexNotice);
    require(s_view.line_count == 1 && std::strcmp(s_view.lines[0].text.characters, "Reply ready") == 0 &&
        std::string(generic.title.text.characters, generic.title.text.length) == "From Codex",
        "generic notices suppress identical body copy without inferring the new typed readiness event");
}
// No parameters: explicitly synthetic canonical observations exercise the real Home presenter.
// Exception: Stops on incorrect routes, titles, freshness or original service-error propagation.
void check_home_lifecycle()
{
    static LUNASHELL::CodexTechnicalObservation observation{};
    HomeFixture::observation = &observation;
    auto &link = observation.link;
    auto &activity = link.activity;
    auto &technical = observation.technical;
    link.state = LUNASHELL::CodexLinkState::Connected;
    link.health = activity.health = technical.health = LUNASHELL::DeviceHealth::Ready;
    link.last_error = activity.last_error = technical.last_error = ESP_OK;
    link.data_valid = activity.data_valid = technical.data_valid = true;
    link.retained_link_present = true;
    activity.state = LUNASHELL::CodexActivityState::Working;
    activity.revision = technical.revision = 1;
    copy_field(activity.chat_id, std::string(64, 'a'));
    copy_field(technical.chat_id, std::string(64, 'a'));
    copy_field(technical.turn_id, std::string(64, 'b'));
    copy_field(activity.project, "SIMULATED Home");
    technical.context_valid = technical.used_valid = technical.capacity_valid = true;
    technical.used = 25;
    technical.capacity = 100;
    technical.received_at_us = 1000000;
    s_view = {};
    s_presented_activity = {};
    s_presented_compaction = LUNASHELL::CodexCompactionState::None;
    s_home_reply_dwell = {};
    s_home_ready_dwell = {};
    s_command_hints = {};
    s_activity_observation = {};
    s_route = Route::Connected15;
    const SceneDefinition *definition = nullptr;
    require(find_scene(SceneId::Connected, definition) == ESP_OK, "Home initial scene exists");
    SceneFrame scene;
    require(begin_scene(*definition, scene) == ESP_OK, "Home initial scene prepares");
    // now_us: Deterministic presentation/receipt clock; no physical clock is accessed.
    // Exception: Stops on any unexpected presenter failure.
    const auto present = [&](int64_t now_us) {
        HomeFixture::now_us = now_us;
        const auto prior_route = s_route;
        const unsigned prior_resets = HomeFixture::inactive_look_resets;
        bool changed = false;
        require(present_companion_activity(now_us, scene, changed) == ESP_OK, "real Home presentation succeeds");
        require(HomeFixture::inactive_look_resets == prior_resets + (s_route != prior_route ? 1U : 0U),
            "only a real route transition retires the inactive Look view");
        // A newly entered route first completes its real entrance before any footer time is eligible.
        require(advance_scene(scene, 30000000) == ESP_OK &&
            present_companion_activity(now_us, scene, changed) == ESP_OK,
            "only actually visible Home content admits the finite footer");
    };
    // expected: Exact English title from the actual prepared native scene.
    const auto title_is = [&](const char *expected) {
        return std::string(scene.title.text.characters, scene.title.text.length) == expected;
    };
    present(1000000);
    require(s_route == Route::Working18, "working source reaches working Home");
    require(scene.activity_identity != nullptr && std::strcmp(s_view.activity_identity.token_fields[0], "Last 25 TT") == 0,
        "real Home retains canonical tokens");
    activity.state = LUNASHELL::CodexActivityState::ResponseReady;
    present(2000000);
    require(s_route == Route::Response19 && title_is("Reply ready"), "completion starts real reply introduction");
    ++activity.revision;
    technical.revision = activity.revision;
    present(6999999);
    require(s_route == Route::Response19, "metadata heartbeat cannot end reply introduction early");
    present(7000000);
    require(s_route == Route::CompanionIdle16 && title_is("Ready for more"), "five seconds retires reply into Ready for more");
    require(scene.command_hints != nullptr, "Ready for more shares the original finite footer lifetime");
    ++activity.revision;
    technical.revision = activity.revision;
    present(31999999);
    require(title_is("Ready for more"), "metadata heartbeat cannot reset idle dwell");
    present(32000000);
    require(title_is("Ready to team up") && activity.state == LUNASHELL::CodexActivityState::ResponseReady,
        "idle title settles after 25 seconds without changing canonical completion");
    activity.state = LUNASHELL::CodexActivityState::Working;
    technical.compaction_valid = technical.compaction_age_valid = true;
    technical.compaction = LUNASHELL::CodexCompactionState::Active;
    technical.compaction_sequence = 1;
    technical.received_at_us = 33000000;
    present(33000000);
    require(scene.title.color == 0xFD20 && s_view.activity_identity.compaction_timer && scene.command_hints == nullptr,
        "actual compaction Home is orange and suppresses command prompts");
    link.state = LUNASHELL::CodexLinkState::Discovering;
    link.last_error = ESP_ERR_TIMEOUT;
    link.health = LUNASHELL::DeviceHealth::Degraded;
    present(34000000);
    require(s_route == Route::LookingForPc14 && title_is("Connecting") && scene.activity_identity == nullptr,
        "retained discovery immediately presents real Connecting without stale metrics");
    HomeFixture::read_error = ESP_FAIL;
    bool changed = false;
    require(present_companion_activity(35000000, scene, changed) == ESP_FAIL,
        "Home preserves the canonical read failure");
    require(s_command_hints.sampled_us == -1, "canonical source failure pauses the retained footer clock");
    HomeFixture::read_error = ESP_OK;
    link.state = LUNASHELL::CodexLinkState::Connected;
    link.last_error = ESP_OK;
    link.health = LUNASHELL::DeviceHealth::Ready;
    technical.compaction = LUNASHELL::CodexCompactionState::None;
    technical.compaction_valid = technical.compaction_age_valid = false;
    present(36000000);
    present(300000000);
    require(s_command_hints.completed_passes == 3 && scene.command_hints == nullptr,
        "real presenter removes the footer after three complete active passes");
    const auto finished_revision = s_command_hints.revision;
    copy_field(activity.chat_id, std::string(64, 'c'));
    copy_field(technical.chat_id, std::string(64, 'c'));
    ++activity.revision;
    technical.revision = activity.revision;
    activity.state = LUNASHELL::CodexActivityState::ResponseReady;
    present(301000000);
    present(306000000);
    require(s_route == Route::CompanionIdle16 && scene.command_hints == nullptr &&
        s_command_hints.completed_passes == 3 && s_command_hints.revision == finished_revision,
        "new chat and Ready for more never replace the completed footer with another hint");
    reset_onboarding();
    require(s_command_hints.completed_passes == 3 && s_command_hints.revision == finished_revision &&
        s_command_hints.sampled_us == -1,
        "onboarding reset preserves display-owner completion instead of replaying the introduction");
    HomeFixture::observation = nullptr;
}
} // namespace

// No parameters: compile/run is available only through the explicitly requested native suite.
int main()
{
    verify_shared_event_presentation();
    check_question_audio_schedule();
    check_bounded_text();
    check_choice_cards();
    check_paged_reply_review();
    check_pending_question_rows();
    check_pr_views();
    check_reply_semantics();
    check_reply_ready_notice();
    check_home_lifecycle();
    std::puts("Real Cody event presenter: numbered rows, complete review, queue selection, reveal gates and delivered-only success passed");
    return 0;
}

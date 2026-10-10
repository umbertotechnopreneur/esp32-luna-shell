#pragma once

#include <cstdint>
#include <cstring>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include "CODY2D_controls.hpp"
#include "LUNASHELL_device_snapshot.hpp"
#include "esp_err.h"

namespace CODY::event_presentation {
enum class Route : uint8_t {
    None, Hello04, Invitation05, Install06, Pairing07, Failed08, Online09,
    MakeItYours10, Look11, ConnectCodex12, SetupComplete13, LookingForPc14,
    Connected15, Settings36, DebugText, CodexChoice, SetupMenu, NetworkWait, CodexReplace,
    CodexForget, CodexCleanup, CodexForgotten,
    Project17, Working18, Response19, Paused20, Ended20, Lost32, CompanionIdle16, ActivityUnavailable,
    PrPending21 = 64, ChecksRunning22 = 65, ChecksFailed23 = 66, CheckFailures24 = 67,
    DecisionNotice26 = 68, DecisionChoices27 = 69, DecisionConfirm27 = 70,
    DecisionWaiting = 71, ReplySent31 = 72, CodexNotice = 73, ApprovalOnPc = 74, EventUnavailable = 75
};
enum class EventStage : uint8_t { Notice = 0, Choices = 1, Confirm = 2, Waiting = 3, Failures = 4 };
enum class EventRetry : uint8_t { None = 0, Reply = 2, Dismiss = 3 };

// route: Stable presentation identity, independent of atlas page numbers or catalog order.
inline bool is_codex_event_route(Route route)
{
    switch (route) {
    case Route::PrPending21:
    case Route::ChecksRunning22:
    case Route::ChecksFailed23:
    case Route::CheckFailures24:
    case Route::DecisionNotice26:
    case Route::DecisionChoices27:
    case Route::DecisionConfirm27:
    case Route::DecisionWaiting:
    case Route::ReplySent31:
    case Route::CodexNotice:
    case Route::ApprovalOnPc:
    case Route::EventUnavailable:
        return true;
    default:
        return false;
    }
}

// event: Retained immutable event projection; no hardware or transport access occurs here.
// id: Stable semantic choice identity independent of card ordering.
inline const LUNASHELL::CodexEventChoice *find_choice(
    const LUNASHELL::CodexEventSnapshot &event, const char *id)
{
    if (id == nullptr || event.choice_count > 3) { return nullptr; }
    const LUNASHELL::CodexEventChoice *matched = nullptr;
    for (uint8_t index = 0; index < event.choice_count; ++index) {
        const auto &choice = event.choices[index];
        if (choice.id[0] != '\0' && std::strcmp(choice.id, id) == 0) {
            if (matched != nullptr) { return nullptr; }
            matched = &choice;
        }
    }
    return matched;
}

// event: Retained owner snapshot; terminal outcomes take precedence over local navigation.
// stage: Reversible local event navigation.
// reply_admitted: True only after the owner accepts a locally confirmed reply.
// observation_error: Actual canonical observation failure, displayed as unavailable.
inline Route destination(const LUNASHELL::CodexEventSnapshot &event,
                         EventStage stage, bool reply_admitted, esp_err_t observation_error)
{
    using Kind = LUNASHELL::CodexEventKind;
    using State = LUNASHELL::CodexEventState;
    if (observation_error != ESP_OK) {
        return Route::EventUnavailable;
    }
    if (event.kind == Kind::Question && event.state == State::Delivered) {
        return Route::ReplySent31;
    }
    if (LUNASHELL::codexEventIsTerminal(event.state)) {
        return Route::CodexNotice;
    }
    switch (event.kind) {
    case Kind::Question:
        if (reply_admitted || event.state == State::Sending || event.state == State::Received) {
            return Route::DecisionWaiting;
        }
        if (stage == EventStage::Confirm) {
            return Route::DecisionConfirm27;
        }
        return stage == EventStage::Choices ? Route::DecisionChoices27 : Route::DecisionNotice26;
    case Kind::PrPending: return Route::PrPending21;
    case Kind::ChecksRunning:
    case Kind::ChecksPassed: return Route::ChecksRunning22;
    case Kind::ChecksFailed:
        return stage == EventStage::Failures ? Route::CheckFailures24 : Route::ChecksFailed23;
    case Kind::ApprovalOnPc: return Route::ApprovalOnPc;
    case Kind::Notice: return Route::CodexNotice;
    case Kind::None: return Route::Connected15;
    }
    return Route::EventUnavailable;
}

enum class ActionId : uint8_t { None, Open, Choose, Send, Dismiss, Retry, Failures, Back };
enum class Effect : uint8_t { None, Render, Reply, Dismiss, Observe };
struct Bounds { uint16_t x = 0, y = 0, width = 0, height = 0; };
struct Line { char text[64]{}; uint16_t top = 0; bool instant = false; };
struct Action {
    ActionId id = ActionId::None;
    char choice_id[13]{};
    char label[16]{};
    LUNASHELL::CodexChoiceTone tone = LUNASHELL::CodexChoiceTone::Neutral;
    Bounds bounds{};
    bool enabled = false;
    bool card = false;
    uint16_t colors[4]{}; // Top, bottom, border and icon ink; owned by the shared semantic model.
    CODY2D::ActionIcon icon = CODY2D::ActionIcon::None;
};
struct Navigation {
    EventStage stage = EventStage::Notice;
    char selected_id[13]{};
    uint8_t failure_page = 0;
};
struct Context {
    bool online = false;
    bool reply_admitted = false;
    bool dismiss_admitted = false;
    esp_err_t observation_error = ESP_OK;
    esp_err_t ui_error = ESP_OK;
    EventRetry retry = EventRetry::None;
};
struct Plan {
    Route route = Route::None;
    char scene[24]{};
    char title[32]{};
    Line lines[8]{};
    uint8_t line_count = 0;
    Action actions[3]{};
    uint8_t action_count = 0;
    bool back = false;
};

// action: Complete semantic identity/tone receiving production colors and explicit icon.
inline void style(Action &action)
{
    using Tone = LUNASHELL::CodexChoiceTone;
    using Icon = CODY2D::ActionIcon;
    switch (action.tone) {
    case Tone::Neutral:
        action.colors[0] = 0x18E5;
        action.colors[1] = 0x10A3;
        action.colors[2] = 0x4A8C;
        action.colors[3] = 0xBDF7;
        action.icon = Icon::Arrow;
        break;
    case Tone::Positive:
        action.colors[0] = 0x1147;
        action.colors[1] = 0x08C4;
        action.colors[2] = 0x2B4F;
        action.colors[3] = 0x8F1D;
        action.icon = Icon::Check;
        break;
    case Tone::Negative:
        action.colors[0] = 0x28A4;
        action.colors[1] = 0x1883;
        action.colors[2] = 0x796B;
        action.colors[3] = 0xED53;
        action.icon = Icon::Close;
        break;
    case Tone::Info:
        action.colors[0] = 0x1106;
        action.colors[1] = 0x08A4;
        action.colors[2] = 0x3291;
        action.colors[3] = 0xA71F;
        action.icon = Icon::Link;
        break;
    }
    switch (action.id) {
    case ActionId::Send: action.icon = Icon::Link; break;
    case ActionId::Dismiss: action.icon = Icon::Check; break;
    case ActionId::Open: action.icon = Icon::Arrow; break;
    case ActionId::Retry:
    case ActionId::Failures:
        action.colors[0] = 0x2924;
        action.colors[1] = 0x18C3;
        action.colors[2] = 0x7BEF;
        action.colors[3] = 0xE5F1;
        action.icon = action.id == ActionId::Retry ? Icon::Retry : Icon::Arrow;
        break;
    default: break;
    }
}

// action: Model-owned semantic style, exact bounds and full untimed caption.
// font: Actual shared Cody body-font glyphs and metrics.
// control: Receives native immutable rounded control geometry/style and first caption.
// second: Receives a second caption only for an authored multiword card.
// Exception: Returns exact native caption/bounds errors; never truncates a label.
inline esp_err_t prepare_control(const Action &action, const CODY2D::MonoFont &font,
    CODY2D::CapsuleControl &control, CODY2D::TextLine &second)
{
    const auto &bounds = action.bounds;
    control = {};
    second = {};
    control.bounds = {bounds.x, bounds.y, bounds.width, bounds.height};
    control.enabled = action.enabled;
    control.corner_radius = action.card ? 28 : 0;
    control.top_color = action.colors[0];
    control.bottom_color = action.colors[1];
    control.border_color = action.colors[2];
    control.icon_color = action.colors[3];
    control.icon = action.icon;
    const uint16_t drop = action.card ? 10U : bounds.height >= 72 ? 6U : 4U;
    control.icon_center = {static_cast<int16_t>(action.card ? bounds.x + bounds.width / 2 : bounds.x + 27),
        static_cast<int16_t>((action.card ? bounds.y + 23 : bounds.y + bounds.height / 2) + drop)};
    const char *label = action.label;
    const char *space = action.card ? std::strchr(label, ' ') : nullptr;
    if (space == label || (space != nullptr && space[1] == '\0')) { space = nullptr; }
    const uint16_t length = static_cast<uint16_t>(space != nullptr ? space - label : std::strlen(label));
    const uint16_t text_width = action.card ? bounds.width : bounds.width - 56;
    if (length == 0 || bounds.width < 56 || bounds.height < font.height) { return ESP_ERR_INVALID_SIZE; }
    const uint16_t width = (length - 1U) * font.advance + font.width;
    if (width > text_width) { return ESP_ERR_INVALID_SIZE; }
    const uint16_t top = action.card ? bounds.y + (space != nullptr ? 48 : 57) :
        bounds.y + (bounds.height - font.height) / 2;
    const uint16_t left = action.card ? bounds.x + (bounds.width - width) / 2 :
        bounds.x + (bounds.width - width - 36U) / 2U + 36U;
    if (!action.card) {
        // Caption and icon form one centered group, with the same vertical
        // alignment used by onboarding's actual native controls.
        control.icon_center.x = static_cast<int16_t>(left - 24);
        control.icon_center.y = static_cast<int16_t>(top + font.height / 2U);
    }
    esp_err_t error = CODY2D::prepare_text_line(font, {label, length},
        {static_cast<int16_t>(left), static_cast<int16_t>(top)}, 0xFFFF, control.caption);
    if (error == ESP_OK && space != nullptr) {
        const char *text = space + 1;
        const uint16_t count = static_cast<uint16_t>(std::strlen(text));
        const uint16_t width_second = (count - 1U) * font.advance + font.width;
        if (width_second > bounds.width) { return ESP_ERR_INVALID_SIZE; }
        error = CODY2D::prepare_text_line(font, {text, count},
            {static_cast<int16_t>(bounds.x + (bounds.width - width_second) / 2),
             static_cast<int16_t>(bounds.y + 75)}, 0xFFFF, second);
    }
    return error;
}

// route: Stable event route mapped to its authored scene identity.
inline const char *scene_name(Route route)
{
    switch (route) {
    case Route::PrPending21: return "pr-pending";
    case Route::ChecksRunning22: return "checks-running";
    case Route::ChecksFailed23: return "checks-failed";
    case Route::CheckFailures24: return "check-failures";
    case Route::DecisionNotice26: return "decision-notice";
    case Route::DecisionChoices27: return "decision-choices";
    case Route::DecisionConfirm27: return "decision-confirm";
    case Route::DecisionWaiting: return "decision-waiting";
    case Route::ReplySent31: return "reply-sent";
    case Route::CodexNotice: return "codex-notice";
    case Route::ApprovalOnPc: return "approval-on-pc";
    case Route::EventUnavailable: return "event-unavailable";
    default: return nullptr;
    }
}

// plan: Caller-owned bounded projection; this owner never accesses transport or hardware.
// text: Complete bounded ASCII string, preserved without truncation.
// top: Logical line origin advanced only for successfully admitted lines.
// capacity: Exact character capacity derived from the real body font.
// pitch: Authored line spacing.
// instant: Status lines excluded from narrative timing and sound.
// words: Enables bounded word wrapping while retaining the allocated line count.
// Exception: Returns INVALID_SIZE for text/viewport/capacity overflow.
inline esp_err_t append_lines(Plan &plan, const char *text, uint16_t &top,
                             size_t capacity, uint16_t pitch = 26,
                             bool instant = false, bool words = true)
{
    if (text == nullptr || capacity == 0 || capacity >= sizeof(Line::text)) {
        return ESP_ERR_INVALID_SIZE;
    }
    const size_t total = std::strlen(text);
    size_t offset = 0;
    size_t remaining_lines = (total + capacity - 1U) / capacity;
    while (offset < total) {
        if (plan.line_count == 8 || top > 354) {
            return ESP_ERR_INVALID_SIZE;
        }
        size_t count = std::min(capacity, total - offset);
        if (words && offset + count < total) {
            for (size_t index = count; index > 0; --index) {
                if (text[offset + index] == ' ' &&
                    total - (offset + index + 1U) <= (remaining_lines - 1U) * capacity) {
                    count = index;
                    break;
                }
            }
        }
        auto &line = plan.lines[plan.line_count++];
        std::memcpy(line.text, text + offset, count);
        line.text[count] = '\0';
        line.top = top;
        line.instant = instant;
        top += pitch;
        offset += count;
        --remaining_lines;
        if (words && offset < total && text[offset] == ' ') {
            ++offset;
        }
    }
    return ESP_OK;
}

// plan: Owner buffer receiving one complete capsule/card.
// id: Semantic input identity independent of its caption.
// label: Reviewed ASCII caption bounded by the caller's contract.
// enabled: Current owner admission policy, separate from reveal/touch timing.
// tone: Semantic gradient identity.
inline void add_action(Plan &plan, ActionId id, const char *label, bool enabled,
                       LUNASHELL::CodexChoiceTone tone = LUNASHELL::CodexChoiceTone::Info)
{
    auto &action = plan.actions[plan.action_count++];
    action.id = id;
    std::strcpy(action.label, label);
    action.enabled = enabled;
    action.tone = tone;
    action.bounds = {113, 352, 240, 72};
}

// event: Coherent canonical event on firmware; isolated SIMULATED observation on host.
// navigation: Reversible local choices and read-only paging.
// context: Actual freshness, admissions and errors, never guessed transport success.
// capacity: Body-font character capacity shared with native rendering.
// plan: Presentation-owned output; valid only on ESP_OK.
// Exception: Returns exact route, selection and bounded text errors.
inline esp_err_t prepare(const LUNASHELL::CodexEventSnapshot &event,
                         const Navigation &navigation, const Context &context,
                         size_t capacity, Plan &plan)
{
    using State = LUNASHELL::CodexEventState;
    using Kind = LUNASHELL::CodexEventKind;
    using Tone = LUNASHELL::CodexChoiceTone;
    plan = {};
    if (event.choice_count > 3 || event.failure_count > 6) { return ESP_ERR_INVALID_SIZE; }
    for (uint8_t index = 0; index < event.choice_count; ++index) {
        const auto &choice = event.choices[index];
        if (!LUNASHELL::validCodexChoiceId(choice.id) || choice.label[0] == '\0' ||
            static_cast<uint8_t>(choice.tone) > static_cast<uint8_t>(Tone::Info)) { return ESP_ERR_INVALID_ARG; }
        for (uint8_t preceding = 0; preceding < index; ++preceding) {
            if (std::strcmp(choice.id, event.choices[preceding].id) == 0) { return ESP_ERR_INVALID_ARG; }
        }
    }
    plan.route = destination(event, navigation.stage, context.reply_admitted, context.observation_error);
    const char *identity = scene_name(plan.route);
    if (identity == nullptr) { return ESP_ERR_INVALID_STATE; }
    std::strcpy(plan.scene, identity);
    plan.back = plan.route == Route::DecisionChoices27 || plan.route == Route::DecisionConfirm27 ||
        plan.route == Route::CheckFailures24;
    uint16_t top = 196;
    esp_err_t error = ESP_OK;
    const auto text = [&](const char *value, bool instant = false, uint16_t pitch = 26, bool words = true) {
        if (error == ESP_OK) { error = append_lines(plan, value, top, capacity, pitch, instant, words); }
    };
    char status[64]{};
    if (plan.route == Route::EventUnavailable) {
        text("Event status unavailable");
        std::snprintf(status, sizeof(status), "Error: 0x%08lX", static_cast<unsigned long>(context.observation_error));
        text(status, true);
        add_action(plan, ActionId::Retry, "Retry", true);
    } else if (plan.route == Route::DecisionChoices27) {
        text(event.title);
        top = 228;
        text(context.online ? "Choose a response" : "Offline - pending", true);
        if (event.choice_count < 2 || event.choice_count > 3) { return ESP_ERR_INVALID_SIZE; }
        const Bounds bounds[] = {{73, 260, 154, 104}, {239, 260, 154, 104}, {113, 376, 240, 52}};
        for (uint8_t index = 0; index < event.choice_count; ++index) {
            const auto &choice = event.choices[index];
            auto &action = plan.actions[plan.action_count++];
            action.id = ActionId::Choose;
            std::memcpy(action.choice_id, choice.id, sizeof(action.choice_id));
            std::memcpy(action.label, choice.label, sizeof(choice.label));
            action.tone = choice.tone;
            action.bounds = bounds[index];
            action.enabled = event.state == State::Pending && !context.reply_admitted;
            action.card = index < 2;
        }
    } else if (plan.route == Route::DecisionConfirm27) {
        const auto *choice = find_choice(event, navigation.selected_id);
        if (choice == nullptr) { return ESP_ERR_NOT_FOUND; }
        text(choice->label);
        text(choice->detail[0] != '\0' ? choice->detail : event.body);
        top = 326;
        if (context.ui_error != ESP_OK) {
            std::snprintf(status, sizeof(status), context.online ? "Send error: 0x%08lX" : "Offline: 0x%08lX",
                static_cast<unsigned long>(context.ui_error));
            text(status, true);
        } else { text(context.online ? "Review before sending" : "Offline - not sent", true); }
        add_action(plan, context.ui_error == ESP_OK ? ActionId::Send : ActionId::Retry,
            context.ui_error == ESP_OK ? "Send" : "Retry",
            context.online && event.state == State::Pending && !context.reply_admitted, choice->tone);
    } else if (plan.route == Route::CheckFailures24) {
        if (event.failure_count > 6 || navigation.failure_page > 1) { return ESP_ERR_INVALID_SIZE; }
        const uint8_t first = navigation.failure_page * 3U;
        if (event.failure_count == 0) { text("No failure details"); }
        for (uint8_t index = first; index < event.failure_count && index < first + 3U; ++index) {
            top = 196U + (index - first) * 52U;
            text(event.failed_checks[index], true, 25, false);
        }
        top = 354;
        text(!context.online ? "Offline - saved details" : event.failure_count > 3 ?
            "Swipe to scroll" : "Read details on your PC", true);
    } else if (plan.route == Route::DecisionWaiting) {
        text(event.title);
        text(!context.online ? "Offline - reply pending" : event.state == State::Received ?
            "CCP received the reply" : "Sending your reply");
        text("Waiting for Codex", true);
        if (event.last_error != ESP_OK) {
            std::snprintf(status, sizeof(status), "Error: 0x%08lX", static_cast<unsigned long>(event.last_error));
            text(status, true);
        }
    } else if (plan.route == Route::ReplySent31) {
        text("Delivered to Codex");
        const auto *choice = find_choice(event, event.selected_id);
        if (choice != nullptr) { text(choice->label, true); }
        if (context.ui_error != ESP_OK) {
            top = 326;
            std::snprintf(status, sizeof(status), "Done error: 0x%08lX", static_cast<unsigned long>(context.ui_error));
            text(status, true);
        }
        add_action(plan, context.ui_error == ESP_OK ? ActionId::Dismiss : ActionId::Retry,
            context.ui_error == ESP_OK ? "Done" : "Retry", !context.dismiss_admitted, Tone::Positive);
    } else {
        const bool unconfirmed = plan.route == Route::CodexNotice && event.state == State::Expired &&
            event.selected_id[0] != '\0';
        text(event.title);
        text(unconfirmed ? "Delivery unconfirmed" : event.body);
        if (unconfirmed) { text("Check on your PC"); }
        if (plan.route == Route::ChecksRunning22) {
            if (event.progress_valid && context.online) {
                std::snprintf(status, sizeof(status), "%u / %u checks passed",
                    static_cast<unsigned>(event.passed), static_cast<unsigned>(event.total));
                text(status, true);
            } else { text("Progress unavailable", true); }
        }
        if (plan.route == Route::DecisionNotice26) { add_action(plan, ActionId::Open, "Open", event.state == State::Pending); }
        else if (plan.route == Route::ChecksFailed23) { add_action(plan, ActionId::Failures, "View failures", true); }
        else if (plan.route == Route::CodexNotice) {
            add_action(plan, context.ui_error == ESP_OK ? ActionId::Dismiss : ActionId::Retry,
                context.ui_error == ESP_OK ? "Done" : "Retry", !context.dismiss_admitted);
        } else if (event.kind == Kind::ChecksPassed && context.ui_error != ESP_OK) {
            add_action(plan, ActionId::Retry, "Retry", true);
        }
        if (plan.route == Route::ApprovalOnPc) { text("Continue on your PC", true); }
        const esp_err_t status_error = context.ui_error != ESP_OK ? context.ui_error : event.last_error;
        if (status_error != ESP_OK) {
            top = plan.action_count == 0 ? 354 : 326;
            std::snprintf(status, sizeof(status), context.online ? "Error: 0x%08lX" : "Offline: 0x%08lX",
                static_cast<unsigned long>(status_error));
            text(status, true);
        } else if (context.online && event.context[0] != '\0' && plan.route != Route::ChecksRunning22 &&
                   plan.route != Route::ApprovalOnPc) {
            top = 326;
            text(event.context, true);
        } else if (!context.online) {
            top = plan.action_count == 0 ? 354 : 326;
            text(LUNASHELL::codexEventIsTerminal(event.state) ? "Offline - saved result" : "Offline - pending", true);
        }
    }
    if (event.kind == Kind::ChecksPassed && plan.route == Route::ChecksRunning22) { std::strcpy(plan.title, "Checks passed"); }
    if (plan.route == Route::CodexNotice && LUNASHELL::codexEventIsTerminal(event.state) && event.state != State::Delivered) {
        std::strcpy(plan.title, event.state == State::Expired ? (event.selected_id[0] != '\0' ? "Reply unconfirmed" :
            "Request expired") : event.state == State::Cancelled ? "Request cancelled" : "Reply rejected");
    }
    for (uint8_t index = 0; index < plan.action_count; ++index) { style(plan.actions[index]); }
    return error;
}

// plan: Current complete presentation projection.
// visible_characters: Narrative typewriter budget, excluding status and control labels.
// title_characters: Complete authored/overridden title length.
inline bool controls_ready(const Plan &plan, size_t visible_characters, size_t title_characters)
{
    size_t narrative = title_characters;
    for (uint8_t index = 0; index < plan.line_count; ++index) {
        if (!plan.lines[index].instant) { narrative += std::strlen(plan.lines[index].text); }
    }
    return visible_characters >= narrative;
}

// bounds: Rendered rounded action geometry, with pixel-center coverage semantics.
// x: Logical horizontal coordinate after the single shared orientation transform.
// y: Logical vertical coordinate after the single shared orientation transform.
// radius: Native authored rounded corner radius.
inline bool contains(Bounds bounds, float x, float y, int radius)
{
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 || x >= 466 || y >= 466) { return false; }
    CODY2D::CapsuleControl control{};
    control.bounds = {bounds.x, bounds.y, bounds.width, bounds.height};
    control.enabled = true;
    control.corner_radius = static_cast<uint16_t>(radius);
    return CODY2D::capsule_contains(control, {static_cast<int16_t>(x), static_cast<int16_t>(y)});
}

// plan: Current complete controls; hidden controls and Back never admit touch.
// controls_visible: Real reveal state from the drawing owner.
// x: Logical horizontal touch coordinate.
// y: Logical vertical touch coordinate.
inline const Action *hit_action(const Plan &plan, bool controls_visible, float x, float y)
{
    if (!controls_visible) { return nullptr; }
    for (uint8_t index = 0; index < plan.action_count; ++index) {
        const auto &action = plan.actions[index];
        if (action.enabled && contains(action.bounds, x, y, action.card ? 28 : action.bounds.height / 2)) { return &action; }
    }
    return nullptr;
}

// plan: Current route with a real previous destination.
// controls_visible: Complete narrative reveal gate shared with all event controls.
// x: Logical horizontal touch coordinate.
// y: Logical vertical touch coordinate.
inline bool hit_back(const Plan &plan, bool controls_visible, float x, float y)
{
    return plan.back && controls_visible && contains({10, 203, 60, 60}, x, y, 30);
}

// event: Retained immutable event used to validate semantic choice identity.
// context: Current admission and actual error policy.
// action: Explicit reviewed control, never inferred from a label or card position.
// navigation: Local reversible state mutated only by accepted presentation intent.
// effect: Transport work returned to its original owner; no I/O occurs here.
// Exception: Returns INVALID_STATE/NOT_FOUND for stale or ambiguous selection.
inline esp_err_t activate(const LUNASHELL::CodexEventSnapshot &event, const Context &context,
                          const Action &action, Navigation &navigation, Effect &effect)
{
    effect = Effect::None;
    if (!action.enabled) { return ESP_ERR_INVALID_STATE; }
    switch (action.id) {
    case ActionId::Open: navigation.stage = EventStage::Choices; effect = Effect::Render; break;
    case ActionId::Failures: navigation.stage = EventStage::Failures; navigation.failure_page = 0; effect = Effect::Render; break;
    case ActionId::Choose:
        if (event.state != LUNASHELL::CodexEventState::Pending || find_choice(event, action.choice_id) == nullptr) { return ESP_ERR_NOT_FOUND; }
        std::memcpy(navigation.selected_id, action.choice_id, sizeof(navigation.selected_id));
        navigation.stage = EventStage::Confirm;
        effect = Effect::Render;
        break;
    case ActionId::Back:
        if (navigation.stage != EventStage::Confirm && navigation.stage != EventStage::Choices &&
            navigation.stage != EventStage::Failures) { return ESP_ERR_INVALID_STATE; }
        navigation.stage = navigation.stage == EventStage::Confirm ? EventStage::Choices : EventStage::Notice;
        effect = Effect::Render;
        break;
    case ActionId::Retry:
        if (context.observation_error != ESP_OK) { effect = Effect::Observe; break; }
        if (context.retry == EventRetry::Dismiss) { effect = Effect::Dismiss; break; }
        if (context.retry != EventRetry::Reply) { return ESP_ERR_INVALID_STATE; }
        [[fallthrough]];
    case ActionId::Send:
        if (!context.online || context.reply_admitted || event.state != LUNASHELL::CodexEventState::Pending ||
            find_choice(event, navigation.selected_id) == nullptr) { return ESP_ERR_INVALID_STATE; }
        effect = Effect::Reply;
        break;
    case ActionId::Dismiss:
        if (context.dismiss_admitted) { return ESP_ERR_INVALID_STATE; }
        effect = Effect::Dismiss;
        break;
    case ActionId::None: return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

// context: Local presentation errors, kept distinct from canonical transport errors.
// effect: Accepted reversible navigation result.
inline void accept_navigation(Context &context, Effect effect)
{
    if (effect == Effect::Render) {
        context.ui_error = ESP_OK;
        context.retry = EventRetry::None;
    }
}

// event: Canonical readonly failure collection.
// navigation: Current local page identity.
// controls_visible: Same reveal gate as every other input.
// dx: Released horizontal displacement in logical coordinates.
// dy: Released vertical displacement in logical coordinates.
inline bool scroll(const LUNASHELL::CodexEventSnapshot &event, Navigation &navigation,
                   bool controls_visible, int dx, int dy)
{
    if (!controls_visible || navigation.stage != EventStage::Failures || std::abs(dy) < 36 ||
        std::abs(dy) * 2 <= std::abs(dx) * 3) { return false; }
    const uint8_t previous = navigation.failure_page;
    if (dy < 0 && (navigation.failure_page + 1U) * 3U < event.failure_count) { ++navigation.failure_page; }
    else if (dy > 0 && navigation.failure_page > 0) { --navigation.failure_page; }
    return previous != navigation.failure_page;
}

// event: Coherent readonly checks result, never a question or native approval.
// context: Actual owner admission/failure state.
// controls_visible: Complete narrative reveal gate.
// now_us: Existing pausable UI clock; host clocks remain explicitly SIMULATED.
// readable_at_us: Caller-owned first readable timestamp, initially negative.
inline bool checks_passed_dismiss_due(const LUNASHELL::CodexEventSnapshot &event,
    const Context &context, bool controls_visible, int64_t now_us, int64_t &readable_at_us)
{
    if (event.kind != LUNASHELL::CodexEventKind::ChecksPassed || !controls_visible ||
        context.dismiss_admitted || context.ui_error != ESP_OK) { return false; }
    if (readable_at_us < 0) { readable_at_us = now_us; return false; }
    return now_us - readable_at_us >= 3000000LL;
}

} // namespace CODY::event_presentation

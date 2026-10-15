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
    DecisionWaiting = 71, ReplySent31 = 72, CodexNotice = 73, ApprovalOnPc = 74, EventUnavailable = 75,
    DecisionQueue = 76
};
enum class EventStage : uint8_t { Notice = 0, Choices = 1, Confirm = 2, Waiting = 3, Failures = 4, Questions = 5 };
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
    case Route::DecisionQueue:
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
        if (stage == EventStage::Questions) { return Route::DecisionQueue; }
        return stage == EventStage::Choices ? Route::DecisionChoices27 : Route::DecisionNotice26;
    case Kind::PrPending: return Route::PrPending21;
    case Kind::ChecksRunning:
    case Kind::ChecksPassed: return Route::ChecksRunning22;
    case Kind::ChecksFailed:
        return stage == EventStage::Failures ? Route::CheckFailures24 : Route::ChecksFailed23;
    case Kind::ApprovalOnPc: return Route::ApprovalOnPc;
    case Kind::Notice:
    case Kind::ReplyReady: return Route::CodexNotice;
    case Kind::None: return Route::Connected15;
    }
    return Route::EventUnavailable;
}

enum class ActionId : uint8_t { None, Open, Choose, Send, Dismiss, Retry, Failures, Back, SelectQuestion };
enum class Effect : uint8_t { None, Render, Reply, Dismiss, Observe, SelectQuestion };
struct Bounds { uint16_t x = 0, y = 0, width = 0, height = 0; };
struct Line {
    char text[64]{};
    uint16_t top = 0;
    bool instant = false;
    bool delivery_pulse = false;
};

// line: Shared narrative/status style; only an active delivery wait pulses.
// elapsed_us: Pausable scene time, unrelated to the actual delivery deadline.
inline uint16_t line_color(const Line &line, uint64_t elapsed_us)
{
    if (!line.delivery_pulse) { return line.instant ? 0xBDF7 : 0xFFFF; }
    // A 1.6-second breathing cycle stays readable at its darkest point. Quantize
    // to 50 ms so unchanged colors do not request redundant content redraws.
    const uint32_t step = (elapsed_us % 1600000U) / 50000U;
    const uint32_t ramp = (step <= 16U ? step : 32U - step) * 16U;
    const uint32_t blend = ramp * ramp * (768U - 2U * ramp) / 65536U;
    const uint16_t red = 3U + 2U * blend / 256U;
    const uint16_t green = 20U + 16U * blend / 256U;
    const uint16_t blue = 22U + 9U * blend / 256U;
    return static_cast<uint16_t>((red << 11U) | (green << 5U) | blue);
}
struct Action {
    ActionId id = ActionId::None;
    char choice_id[13]{};
    char label[68]{}; // Bounded preview only; full canonical text remains in the event.
    char request_id[33]{};
    LUNASHELL::CodexChoiceTone tone = LUNASHELL::CodexChoiceTone::Neutral;
    Bounds bounds{};
    bool enabled = false;
    bool card = false;
    bool row = false;
    uint16_t colors[4]{}; // Top, bottom, border and icon ink; owned by the shared semantic model.
    CODY2D::ActionIcon icon = CODY2D::ActionIcon::None;
};
struct Navigation {
    EventStage stage = EventStage::Notice;
    char selected_id[13]{};
    uint8_t failure_page = 0;
    uint8_t choice_page = 0;
    uint8_t detail_page = 0;
};
struct Context {
    bool online = false;
    bool reply_admitted = false;
    bool dismiss_admitted = false;
    esp_err_t observation_error = ESP_OK;
    esp_err_t ui_error = ESP_OK;
    EventRetry retry = EventRetry::None;
    bool selection_pending = false;
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
    uint8_t page_index = 0;
    uint8_t page_count = 1;
    bool scrollable = false;
    uint16_t title_color = 0xFFFF;
};

// action: Complete semantic identity/tone receiving text-only production colors.
inline void style(Action &action)
{
    using Tone = LUNASHELL::CodexChoiceTone;
    action.icon = CODY2D::ActionIcon::None;
    switch (action.tone) {
    case Tone::Neutral:
        action.colors[0] = 0x18E5;
        action.colors[1] = 0x10A3;
        action.colors[2] = 0x4A8C;
        action.colors[3] = 0xBDF7;
        break;
    case Tone::Positive:
        action.colors[0] = 0x1147;
        action.colors[1] = 0x08C4;
        action.colors[2] = 0x2B4F;
        action.colors[3] = 0x8F1D;
        break;
    case Tone::Negative:
        action.colors[0] = 0x28A4;
        action.colors[1] = 0x1883;
        action.colors[2] = 0x796B;
        action.colors[3] = 0xED53;
        break;
    case Tone::Info:
        action.colors[0] = 0x1106;
        action.colors[1] = 0x08A4;
        action.colors[2] = 0x3291;
        action.colors[3] = 0xA71F;
        break;
    }
    switch (action.id) {
    case ActionId::Retry:
    case ActionId::Failures:
        action.colors[0] = 0x2924;
        action.colors[1] = 0x18C3;
        action.colors[2] = 0x7BEF;
        action.colors[3] = 0xE5F1;
        break;
    default: break;
    }
}

// action: Model-owned semantic style, exact bounds and full untimed caption.
// font: Actual shared Cody body-font glyphs and metrics.
// control: Receives native immutable rounded control geometry/style and first caption.
// second: Receives a second caption only for an authored non-list card.
// Exception: Returns exact native caption/bounds errors; previews are already bounded.
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
    control.icon = CODY2D::ActionIcon::None;
    const char *label = action.label;
    const char *space = action.card && !action.row ? std::strchr(label, ' ') : nullptr;
    if (space == label || (space != nullptr && space[1] == '\0')) { space = nullptr; }
    const uint16_t length = static_cast<uint16_t>(space != nullptr ? space - label : std::strlen(label));
    if (length == 0 || bounds.width < 20 || bounds.height < font.height) { return ESP_ERR_INVALID_SIZE; }
    const uint16_t text_width = bounds.width - 20;
    const uint16_t width = (length - 1U) * font.advance + font.width;
    if (width > text_width) { return ESP_ERR_INVALID_SIZE; }
    const uint16_t height = space != nullptr ? font.height * 2U + 2U : font.height;
    if (height > bounds.height) { return ESP_ERR_INVALID_SIZE; }
    const uint16_t top = bounds.y + (bounds.height - height) / 2;
    const uint16_t left = action.row ? bounds.x + 10U : bounds.x + (bounds.width - width) / 2;
    esp_err_t error = CODY2D::prepare_text_line(font, {label, length},
        {static_cast<int16_t>(left), static_cast<int16_t>(top)}, 0xFFFF, control.caption);
    if (error == ESP_OK && space != nullptr) {
        const char *text = space + 1;
        const uint16_t count = static_cast<uint16_t>(std::strlen(text));
        const uint16_t width_second = (count - 1U) * font.advance + font.width;
        if (width_second > text_width) { return ESP_ERR_INVALID_SIZE; }
        error = CODY2D::prepare_text_line(font, {text, count},
            {static_cast<int16_t>(bounds.x + (bounds.width - width_second) / 2),
             static_cast<int16_t>(top + font.height + 2U)}, 0xFFFF, second);
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
    case Route::DecisionQueue: return "decision-choices";
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

// action: Retained single-line preview; the complete source stays canonical.
// number: One-based offered position, never used as the response identity.
// label: Original bounded ASCII choice or pending-question title.
// capacity: Exact number of body-font characters admitted by the 300-pixel inset.
// Exception: Returns INVALID_SIZE rather than overflowing or hiding continuation.
inline esp_err_t prepare_row_label(Action &action, uint8_t number, const char *label, size_t capacity)
{
    if (capacity < 7 || capacity >= sizeof(action.label)) { return ESP_ERR_INVALID_SIZE; }
    const size_t length = std::strlen(label);
    if (number == 0 || number > 9 || length + 3U >= sizeof(action.label)) { return ESP_ERR_INVALID_SIZE; }
    action.label[0] = static_cast<char>('0' + number);
    action.label[1] = '.';
    action.label[2] = ' ';
    std::memcpy(action.label + 3, label, length + 1U);
    if (length + 3U > capacity) {
        // A visible ellipsis names a preview. The review page renders every
        // original label/detail character and never sends this shortened text.
        std::memcpy(action.label + capacity - 3U, "...", 4);
    }
    return ESP_OK;
}

// plan: Receives only the requested five-line review viewport and its page count.
// label: Full canonical response caption, read before its description.
// detail: Full canonical detail (or the question body when detail is absent).
// page: Explicit local review page; no owner state is changed while reading.
// capacity: Measured body-font capacity, with no pixel or timing approximation.
// Exception: Returns INVALID_SIZE for an invalid viewport/page or oversized field.
inline esp_err_t prepare_detail_page(Plan &plan, const char *label, const char *detail,
                                    uint8_t page, size_t capacity)
{
    if (capacity < 7 || capacity >= sizeof(Line::text)) { return ESP_ERR_INVALID_SIZE; }
    constexpr uint8_t lines_per_page = 5;
    uint16_t line_number = 0;
    const char *sources[] = {label, detail};
    for (const char *source : sources) {
        const size_t length = std::strlen(source);
        size_t offset = 0;
        while (offset < length) {
            size_t count = std::min(capacity, length - offset);
            if (offset + count < length) {
                for (size_t split = count; split > 0; --split) {
                    // Keep an ordinal with a following long word instead of
                    // stranding "3." on a line by itself.
                    if (source[offset + split] == ' ' && split >= capacity / 2U) { count = split; break; }
                }
            }
            if (line_number / lines_per_page == page) {
                auto &line = plan.lines[plan.line_count++];
                std::memcpy(line.text, source + offset, count);
                line.text[count] = '\0';
                line.top = 196U + (line_number % lines_per_page) * 26U;
                // Review scrolling does not replay narration or typewriter audio.
                line.instant = true;
            }
            ++line_number;
            offset += count;
            if (offset < length && source[offset] == ' ') { ++offset; }
        }
    }
    const uint16_t pages = (line_number + lines_per_page - 1U) / lines_per_page;
    if (pages == 0 || pages > UINT8_MAX || page >= pages) { return ESP_ERR_INVALID_SIZE; }
    plan.page_index = page;
    plan.page_count = static_cast<uint8_t>(pages);
    plan.scrollable = pages > 1;
    return ESP_OK;
}

// plan: Prepared list viewport receiving one narrow hint below the safe square.
// message: Short ASCII status measured against the lower circular chord.
// Exception: Returns INVALID_SIZE if the fixed footer cannot retain the whole hint.
inline esp_err_t append_list_hint(Plan &plan, const char *message)
{
    const size_t length = std::strlen(message);
    if (plan.line_count >= 8 || length == 0 || length > 24) { return ESP_ERR_INVALID_SIZE; }
    auto &line = plan.lines[plan.line_count++];
    std::memcpy(line.text, message, length + 1U);
    line.top = 394;
    line.instant = true;
    return ESP_OK;
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
    if (event.choice_count > 3 || event.failure_count > 6 || event.pending_entry_count > 2) {
        return ESP_ERR_INVALID_SIZE;
    }
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
        plan.route == Route::CheckFailures24 || plan.route == Route::DecisionQueue;
    uint16_t top = 196;
    esp_err_t error = ESP_OK;
    // value: Complete English copy or canonical event text, never shortened here.
    // instant: Excludes status text from narrative timing.
    // pitch: Existing logical line spacing.
    // words: Preserves word boundaries where the fixed line budget permits.
    const auto text = [&](const char *value, bool instant = false, uint16_t pitch = 26, bool words = true) {
        if (error == ESP_OK) { error = append_lines(plan, value, top, capacity, pitch, instant, words); }
    };
    char status[64]{};
    if (plan.route == Route::EventUnavailable) {
        text("Update unavailable");
        std::snprintf(status, sizeof(status), "Error: 0x%08lX", static_cast<unsigned long>(context.observation_error));
        text(status, true);
        add_action(plan, ActionId::Retry, "Try again", true);
    } else if (plan.route == Route::DecisionChoices27) {
        text(event.title);
        if (event.choice_count < 2 || event.choice_count > 3) { return ESP_ERR_INVALID_SIZE; }
        plan.page_count = (event.choice_count + 1U) / 2U;
        plan.page_index = navigation.choice_page;
        plan.scrollable = plan.page_count > 1;
        if (plan.page_index >= plan.page_count) { return ESP_ERR_INVALID_SIZE; }
        const uint8_t first = plan.page_index * 2U;
        for (uint8_t index = first; index < event.choice_count && index < first + 2U; ++index) {
            const auto &choice = event.choices[index];
            auto &action = plan.actions[plan.action_count++];
            action.id = ActionId::Choose;
            std::memcpy(action.choice_id, choice.id, sizeof(action.choice_id));
            if (error == ESP_OK) { error = prepare_row_label(action, index + 1U, choice.label, capacity); }
            action.tone = choice.tone;
            action.bounds = {73, static_cast<uint16_t>(256U + (index - first) * 72U), 320, 60};
            action.enabled = event.state == State::Pending && !context.reply_admitted;
            action.card = action.row = true;
        }
        if (plan.scrollable) {
            std::snprintf(status, sizeof(status), "Swipe %u/%u",
                static_cast<unsigned>(plan.page_index + 1U), static_cast<unsigned>(plan.page_count));
        } else { std::strcpy(status, "Tap to read reply"); }
        if (error == ESP_OK) { error = append_list_hint(plan, context.online ? status : "Offline - read replies"); }
    } else if (plan.route == Route::DecisionQueue) {
        std::strcpy(plan.title, "Pending questions");
        if (event.pending_entry_count == 0) { return ESP_ERR_INVALID_STATE; }
        for (uint8_t index = 0; index < event.pending_entry_count; ++index) {
            const auto &pending = event.pending_questions[index];
            if (pending.request_id[0] == '\0' || pending.title[0] == '\0') { return ESP_ERR_INVALID_ARG; }
            for (uint8_t preceding = 0; preceding < index; ++preceding) {
                if (std::strcmp(pending.request_id, event.pending_questions[preceding].request_id) == 0) {
                    return ESP_ERR_INVALID_ARG;
                }
            }
            auto &action = plan.actions[plan.action_count++];
            action.id = ActionId::SelectQuestion;
            std::memcpy(action.request_id, pending.request_id, sizeof(action.request_id));
            if (error == ESP_OK) {
                error = prepare_row_label(action, index + 1U,
                    pending.context[0] != '\0' ? pending.context : "Context unavailable", capacity);
                text(action.label);
            }
            if (error == ESP_OK) { error = prepare_row_label(action, index + 1U, pending.title, capacity); }
            action.tone = Tone::Info;
            action.bounds = {73, static_cast<uint16_t>(256U + index * 72U), 320, 60};
            action.enabled = context.online && !context.reply_admitted && !context.selection_pending &&
                event.state == State::Pending;
            action.card = action.row = true;
        }
        if (error == ESP_OK) {
            std::snprintf(status, sizeof(status), "%u waiting; tap to open",
                static_cast<unsigned>(event.pending_question_count));
            error = append_list_hint(plan, context.selection_pending ? "Opening question..." :
                context.ui_error != ESP_OK ? "Selection failed - retry" :
                context.online ? status : "Offline - pending");
        }
    } else if (plan.route == Route::DecisionConfirm27) {
        const auto *choice = find_choice(event, navigation.selected_id);
        if (choice == nullptr) { return ESP_ERR_NOT_FOUND; }
        // Only this 68-byte display prefix is copied; full detail stays borrowed
        // from the canonical event. Its ordinal never replaces the stable ID.
        char review_label[68]{};
        const size_t label_length = std::strlen(choice->label);
        if (label_length + 3U >= sizeof(review_label)) { return ESP_ERR_INVALID_SIZE; }
        review_label[0] = static_cast<char>('1' + (choice - event.choices));
        review_label[1] = '.';
        review_label[2] = ' ';
        std::memcpy(review_label + 3, choice->label, label_length + 1U);
        error = prepare_detail_page(plan, review_label,
            choice->detail[0] != '\0' ? choice->detail : event.body, navigation.detail_page, capacity);
        top = 326;
        if (context.ui_error != ESP_OK) {
            std::snprintf(status, sizeof(status), context.online ? "Send error: 0x%08lX" : "Offline: 0x%08lX",
                static_cast<unsigned long>(context.ui_error));
            text(status, true);
        } else {
            if (plan.scrollable) {
                std::snprintf(status, sizeof(status), "Tap or swipe %u/%u",
                    static_cast<unsigned>(plan.page_index + 1U), static_cast<unsigned>(plan.page_count));
            } else { std::strcpy(status, "Review before sending"); }
            text(context.online ? status : "Offline - not sent", true);
        }
        add_action(plan, context.ui_error == ESP_OK ? ActionId::Send : ActionId::Retry,
            context.ui_error == ESP_OK ? "Send reply" : "Try again",
            context.online && event.state == State::Pending && !context.reply_admitted, choice->tone);
    } else if (plan.route == Route::CheckFailures24) {
        if (event.failure_count > 6 || navigation.failure_page > 1) { return ESP_ERR_INVALID_SIZE; }
        plan.page_index = navigation.failure_page;
        plan.page_count = std::max<uint8_t>(1, (event.failure_count + 2U) / 3U);
        plan.scrollable = plan.page_count > 1;
        const uint8_t first = navigation.failure_page * 3U;
        if (event.failure_count == 0) { text("No failure details"); }
        for (uint8_t index = first; index < event.failure_count && index < first + 3U; ++index) {
            top = 196U + (index - first) * 52U;
            text(event.failed_checks[index], true, 25, false);
        }
        top = 354;
        text(!context.online ? "Offline - saved details" : event.failure_count > 3 ?
            "Swipe for more" : "Full details in Codex", true);
    } else if (plan.route == Route::DecisionWaiting) {
        text(event.title);
        text(!context.online ? "Offline - reply pending" : event.state == State::Received ?
            "Reply reached your PC" : "Sending your reply");
        // Keep the single delivery indicator below the question and transport
        // status. It is read-only and never enters the action or audio budget.
        top = 320;
        text("Confirming delivery", true);
        if (error == ESP_OK) {
            plan.lines[plan.line_count - 1U].delivery_pulse = context.online && event.last_error == ESP_OK;
        }
        if (event.last_error != ESP_OK) {
            top = 354;
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
            context.ui_error == ESP_OK ? "Done" : "Try again", !context.dismiss_admitted, Tone::Positive);
    } else if (event.kind == Kind::ReplyReady && !LUNASHELL::codexEventIsTerminal(event.state)) {
        // A typed response-ready observation identifies an available reply, never a successful task.
        // Its body belongs to the originating chat; selected Home metadata cannot replace it.
        std::strcpy(plan.title, "Reply ready");
        plan.title_color = 0x5FF9;
        top = 214;
        text(event.body, false, 28);
        top = 312;
        const esp_err_t status_error = context.ui_error != ESP_OK ? context.ui_error : event.last_error;
        if (status_error != ESP_OK) {
            std::snprintf(status, sizeof(status), "Error: 0x%08lX", static_cast<unsigned long>(status_error));
            text(status, true);
        } else if (!context.online) {
            text("Offline - saved notice", true);
        } else {
            text(event.context[0] != '\0' ? event.context : "Project unavailable", true);
        }
        add_action(plan, context.ui_error == ESP_OK ? ActionId::Dismiss : ActionId::Retry,
            context.ui_error == ESP_OK ? "Got it" : "Try again", !context.dismiss_admitted);
    } else {
        const bool unconfirmed = plan.route == Route::CodexNotice && event.state == State::Expired &&
            event.selected_id[0] != '\0';
        text(event.title);
        if (unconfirmed || event.kind != Kind::Notice || std::strcmp(event.title, event.body) != 0) {
            text(unconfirmed ? "Delivery unconfirmed" : event.body);
        }
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
                context.ui_error == ESP_OK ? "Done" : "Try again", !context.dismiss_admitted);
        } else if (event.kind == Kind::ChecksPassed && context.ui_error != ESP_OK) {
            add_action(plan, ActionId::Retry, "Try again", true);
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
    case ActionId::Open:
        navigation.stage = event.pending_question_count > 1 ? EventStage::Questions : EventStage::Choices;
        navigation.choice_page = navigation.detail_page = 0;
        effect = Effect::Render;
        break;
    case ActionId::Failures: navigation.stage = EventStage::Failures; navigation.failure_page = 0; effect = Effect::Render; break;
    case ActionId::Choose:
        if (event.state != LUNASHELL::CodexEventState::Pending || find_choice(event, action.choice_id) == nullptr) { return ESP_ERR_NOT_FOUND; }
        std::memcpy(navigation.selected_id, action.choice_id, sizeof(navigation.selected_id));
        navigation.stage = EventStage::Confirm;
        navigation.detail_page = 0;
        effect = Effect::Render;
        break;
    case ActionId::Back:
        if (navigation.stage != EventStage::Confirm && navigation.stage != EventStage::Choices &&
            navigation.stage != EventStage::Failures && navigation.stage != EventStage::Questions) {
            return ESP_ERR_INVALID_STATE;
        }
        navigation.stage = navigation.stage == EventStage::Confirm ? EventStage::Choices :
            navigation.stage == EventStage::Choices && event.pending_question_count > 1 ?
                EventStage::Questions : EventStage::Notice;
        effect = Effect::Render;
        break;
    case ActionId::SelectQuestion:
        if (!context.online || context.reply_admitted || context.selection_pending ||
            event.state != LUNASHELL::CodexEventState::Pending ||
            event.pending_entry_count > 2) { return ESP_ERR_INVALID_STATE; }
        for (uint8_t index = 0; index < event.pending_entry_count; ++index) {
            if (std::strcmp(event.pending_questions[index].request_id, action.request_id) == 0) {
                if (std::strcmp(event.request_id, action.request_id) == 0) {
                    navigation.stage = EventStage::Choices;
                    navigation.choice_page = navigation.detail_page = 0;
                    effect = Effect::Render;
                } else {
                    // Admission requests a source change only. The next canonical
                    // observation supplies that question's complete choices.
                    effect = Effect::SelectQuestion;
                }
                return ESP_OK;
            }
        }
        return ESP_ERR_NOT_FOUND;
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

// plan: Measured page limits from the current immutable presentation.
// navigation: Current local page identity.
// controls_visible: Same reveal gate as every other input.
// dx: Released horizontal displacement in logical coordinates.
// dy: Released vertical displacement in logical coordinates.
inline bool scroll(const Plan &plan, Navigation &navigation,
                   bool controls_visible, int dx, int dy)
{
    if (!controls_visible || !plan.scrollable || std::abs(dy) < 36 ||
        std::abs(dy) * 2 <= std::abs(dx) * 3) { return false; }
    uint8_t *page = navigation.stage == EventStage::Choices ? &navigation.choice_page :
        navigation.stage == EventStage::Confirm ? &navigation.detail_page :
        navigation.stage == EventStage::Failures ? &navigation.failure_page : nullptr;
    if (page == nullptr || *page != plan.page_index) { return false; }
    const uint8_t previous = *page;
    if (dy < 0 && *page + 1U < plan.page_count) { ++*page; }
    else if (dy > 0 && *page > 0) { --*page; }
    return previous != *page;
}

// plan: Retained plan that owned both endpoints; caller cancels on route/source changes.
// navigation: Receives only a bounded read-only page change.
// controls_visible: Actual completed-narrative visibility gate.
// start_x: Logical horizontal contact origin after shared orientation mapping.
// start_y: Logical vertical contact origin after shared orientation mapping.
// end_x: Last pressed logical horizontal coordinate, not a fabricated release sample.
// end_y: Last pressed logical vertical coordinate.
// action: Receives the exact tapped action, or null for scrolling and rejected gestures.
inline Effect release(const Plan &plan, Navigation &navigation, bool controls_visible,
                      int start_x, int start_y, int end_x, int end_y, const Action *&action)
{
    action = nullptr;
    if (!controls_visible) { return Effect::None; }
    const int dx = end_x - start_x;
    const int dy = end_y - start_y;
    if (scroll(plan, navigation, controls_visible, dx, dy)) { return Effect::Render; }
    // A diagonal drag must not slip through two independent axis thresholds.
    if (dx * dx + dy * dy >= 144) { return Effect::None; }
    if (navigation.stage == EventStage::Confirm && plan.scrollable &&
        start_x >= 73 && start_x < 393 && end_x >= 73 && end_x < 393 &&
        start_y >= 196 && start_y < 325 && end_y >= 196 && end_y < 325) {
        navigation.detail_page = (navigation.detail_page + 1U) % plan.page_count;
        return Effect::Render;
    }
    const Action *start = hit_action(plan, controls_visible, start_x, start_y);
    const Action *end = hit_action(plan, controls_visible, end_x, end_y);
    if (start != nullptr && start == end) { action = start; }
    return Effect::None;
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

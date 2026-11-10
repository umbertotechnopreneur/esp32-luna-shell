/* Copyright (c) 2026 Umberto Giacobbi */
#include "CODY_codex_browser.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "LUNASHELL_device_snapshot.hpp"
#include "CODY_flat_menu_row.hpp"
#include "CODY_page_marks.hpp"
#include "scene_data.hpp"

namespace CODY {
namespace {

using Action = CodexBrowserAction;
using Destination = CodexBrowserDestination;
using Route = CodexBrowserRoute;
using Icon = CODY2D::ActionIcon;
using Domain::Availability;
using Domain::Freshness;
using Domain::StateQueryKind;
using Domain::StateViewHealth;
using LUNASHELL::DeviceSnapshotService;

constexpr DamageRect kBounds{73, 73, 393, 393};
constexpr uint16_t kWhite = 0xF7BE;
constexpr uint16_t kMuted = 0x9CF3;
constexpr uint16_t kAmber = 0xFE27;
constexpr uint16_t kCyan = 0x6F9E;
constexpr uint16_t kMint = 0x67F0;
constexpr int64_t kRefreshUs = 350000;

// out: Fixed display-owned caption buffer.
// text: Immutable ASCII text from the bounded projection or a literal.
template<size_t Size>
void copy(std::array<char, Size> &out, const char *text)
{
    out.fill(0);
    std::copy_n(text, std::min(std::strlen(text), Size - 1), out.data());
}

// route: Stable browser route identity.
const char *heading(Route route)
{
    switch (route) {
    case Route::Hub: return "Codex";
    case Route::Chats: return "Chats";
    case Route::Turns: return "Turns";
    case Route::Calls: return "Tool calls";
    case Route::ActiveCalls: return "Active calls";
    case Route::PendingQuestions: return "Pending questions";
    default: return "";
    }
}

// fact: Field evidence copied independently from the link lease.
const char *state_note(const Domain::StateFactView &fact)
{
    switch (fact.availability) {
    case Availability::Failed: return "Observation failed";
    case Availability::Conflict: return "Conflicting evidence";
    case Availability::Absent: return "No observed value";
    case Availability::Unsupported: return "Unsupported";
    case Availability::Unknown: return "Unknown";
    case Availability::Known: break;
    }
    switch (fact.freshness) {
    case Freshness::Historical: return "Historical";
    case Freshness::Stale: return "Stale";
    case Freshness::Unknown: return "Freshness unknown";
    case Freshness::Current: return nullptr;
    }
    return "Unknown";
}

// value: Typed, copied chat lifecycle.
const char *lifecycle(Domain::ChatLifecycle value)
{
    switch (value) {
    case Domain::ChatLifecycle::Open: return "Open";
    case Domain::ChatLifecycle::Closed: return "Closed";
    case Domain::ChatLifecycle::Archived: return "Archived";
    default: return "Unknown lifecycle";
    }
}

// value: Typed turn execution, independent of outcome.
const char *execution(Domain::ExecutionState value)
{
    switch (value) {
    case Domain::ExecutionState::Queued: return "Queued";
    case Domain::ExecutionState::Running: return "Running";
    case Domain::ExecutionState::Waiting: return "Waiting";
    case Domain::ExecutionState::Stopped: return "Stopped";
    case Domain::ExecutionState::Interrupted: return "Interrupted";
    default: return "Unknown state";
    }
}

// value: Observed tool invocation state, not task success.
const char *call_state(Domain::CallState value)
{
    switch (value) {
    case Domain::CallState::Queued: return "Queued";
    case Domain::CallState::Running: return "Running";
    case Domain::CallState::Returned: return "Returned";
    case Domain::CallState::Failed: return "Failed";
    case Domain::CallState::Cancelled: return "Cancelled";
    default: return "Unknown state";
    }
}

// value: Observed result of this exact turn or call.
const char *outcome(Domain::Outcome value)
{
    switch (value) {
    case Domain::Outcome::Succeeded: return "Succeeded";
    case Domain::Outcome::Failed: return "Failed";
    case Domain::Outcome::Cancelled: return "Cancelled";
    default: return "Outcome unknown";
    }
}

// category: Native glyph selected only from the explicit domain category.
Icon tool_icon(Domain::ToolCategory category)
{
    switch (category) {
    case Domain::ToolCategory::Command: return Icon::ChevronRight;
    case Domain::ToolCategory::FileRead: return Icon::PlanAccount;
    case Domain::ToolCategory::FileEdit: return Icon::Orientation;
    case Domain::ToolCategory::Search: return Icon::Qr;
    case Domain::ToolCategory::Browser: return Icon::Link;
    case Domain::ToolCategory::Agent: return Icon::Motion;
    case Domain::ToolCategory::Service: return Icon::Gear;
    default: return Icon::Gear;
    }
}

// destination: Presentation-only row owned by the current browser page.
// label: Bounded title or tool label.
// detail: Bounded typed status caption.
// icon: Semantic presentation artwork.
// color: Semantic glyph/secondary text color.
// control: Stable identity associated with the visible row, or empty for read-only rows.
void set_row(CodexBrowserRow &destination, const char *label, const char *detail,
             Icon icon, uint16_t color, CodexBrowserControl control = {})
{
    copy(destination.label, label);
    copy(destination.detail, detail);
    destination.icon = icon;
    destination.color = color;
    destination.control = control;
}

// row: Prepared visual row containing a bounded status caption.
// fact: Exact typed field observation projected by the domain owner.
// value: Caption for its last admitted value.
void append_stale_state(CodexBrowserRow &row, const Domain::StateFactView &fact, const char *value)
{
    if (fact.availability != Availability::Known) return;
    if (fact.freshness == Freshness::Stale || fact.freshness == Freshness::Historical) {
        std::snprintf(row.detail.data(), row.detail.size(), "%s / %s", value,
            fact.freshness == Freshness::Historical ? "Historical" : "Stale");
    }
}

// destination: Status caption for an unavailable or rejected source.
// error: Original query or optional bookmark admission result.
void show_error(std::array<char, 72> &destination, esp_err_t error)
{
    if (error == ESP_ERR_INVALID_STATE || error == ESP_ERR_NOT_FOUND) {
        copy(destination, "Waiting for Codex state");
    } else if (error == ESP_ERR_NOT_SUPPORTED) {
        copy(destination, "Codex unavailable");
    } else {
        std::snprintf(destination.data(), destination.size(), "Error: %s", esp_err_to_name(error));
    }
}

// browser: Owner of the current route and its stable page trails.
CodexBrowserTrail &trail(CodexBrowser &browser)
{
    if (browser.route == Route::Chats) return browser.chats;
    if (browser.route == Route::Turns) return browser.turns;
    return browser.records;
}

// browser: Query scratch owned by the display worker.
// chat: Exact selected identity from the visible row or verified bookmark.
// Exception: Returns the canonical owner error for an unavailable query.
esp_err_t read_chat(CodexBrowser &browser, Domain::ChatId chat)
{
    Domain::StateQuery request{};
    request.kind = StateQueryKind::Chat;
    request.chat = chat;
    return DeviceSnapshotService::instance().readCodexDomainObservation(request, browser.observation);
}

// browser: Display-owned projection of the hub controls and optional Resume action.
void read_hub(CodexBrowser &browser)
{
    auto &draft = browser.draft;
    draft.count = 3;
    set_row(draft.rows[0], "Chats", "Browse chats and turns", Icon::PlanAccount, kCyan, {Action::Chats});
    set_row(draft.rows[1], "Active calls", "Inspect tool activity", Icon::Motion, kMint, {Action::ActiveCalls});
    set_row(draft.rows[2], "Questions", "View pending", Icon::Clock, kAmber,
        {Action::PendingQuestions});

    auto &service = DeviceSnapshotService::instance();
    const auto summary_error = service.readCodexDomainObservation({}, browser.observation);
    if (summary_error != ESP_OK) {
        show_error(draft.note, summary_error);
    } else if (browser.observation.health == StateViewHealth::Historical) {
        copy(draft.note, "Historical summary");
    } else if (browser.observation.health != StateViewHealth::Live) {
        copy(draft.note, "Codex state is stale");
    } else {
        const auto &attention = browser.observation.attention;
        if (attention.questions_complete) {
            auto &detail = draft.rows[2].detail;
            std::snprintf(detail.data(), detail.size(), "%lu pending",
                static_cast<unsigned long>(attention.pending_questions));
        }
        if (attention.calls_complete) {
            std::snprintf(draft.note.data(), draft.note.size(), "%lu active calls",
                static_cast<unsigned long>(attention.active_calls));
        } else {
            copy(draft.note, "Activity incomplete");
        }
    }

    Domain::ChatId bookmark{};
    const auto bookmark_error = service.readCodexSelection(bookmark);
    if (bookmark_error == ESP_OK && bookmark.valid() && read_chat(browser, bookmark) == ESP_OK &&
        browser.observation.health == StateViewHealth::Live) {
        const auto *page = std::get_if<Domain::StateChatPage>(&browser.observation.page);
        if (page && page->count == 1 && page->rows[0].id == bookmark) draft.resume = bookmark;
    } else if (bookmark_error != ESP_OK && bookmark_error != ESP_ERR_NOT_FOUND &&
               bookmark_error != ESP_ERR_NOT_FINISHED && bookmark_error != ESP_ERR_NOT_SUPPORTED) {
        std::snprintf(draft.note.data(), draft.note.size(), "Bookmark: %s", esp_err_to_name(bookmark_error));
    }

    LUNASHELL::CodexRecoveryObservation recovery{};
    const auto recovery_error = service.readCodexRecovery(recovery);
    if (recovery_error == ESP_OK && recovery.attempt_count != 0) {
        // Historical attempts supply no current question, selection or Send action.
        copy(draft.note, "Saved reply unconfirmed");
    } else if (recovery_error != ESP_OK && recovery_error != ESP_ERR_NOT_FOUND &&
               recovery_error != ESP_ERR_NOT_SUPPORTED && recovery_error != ESP_ERR_NOT_FINISHED) {
        std::snprintf(draft.note.data(), draft.note.size(), "Recovery: %s", esp_err_to_name(recovery_error));
    }
}

// browser: Current route and stable selected identities.
// position: Page history scoped to one generation.
Domain::StateQuery page_request(const CodexBrowser &browser, const CodexBrowserTrail &position)
{
    Domain::StateQuery request{};
    request.cursor = position.cursor;
    switch (browser.route) {
    case Route::Chats: request.kind = StateQueryKind::Chats; break;
    case Route::Turns: request.kind = StateQueryKind::Turns; request.chat = browser.chat; break;
    case Route::Calls: request.kind = StateQueryKind::Calls; request.turn = browser.turn; break;
    case Route::ActiveCalls: request.kind = StateQueryKind::ActiveCalls; break;
    case Route::PendingQuestions: request.kind = StateQueryKind::PendingQuestions; break;
    default: break;
    }
    return request;
}

// draft: Bounded visual projection owned by the display worker.
// source: Copied rows from one committed generation.
void project_chats(CodexBrowserPage &draft, const Domain::StateChatPage &source)
{
    draft.count = source.count;
    for (uint8_t index = 0; index < source.count; ++index) {
        const auto &row = source.rows[index];
        const auto *note = state_note(row.lifecycle.fact);
        const auto *label = row.title.fact.availability == Availability::Known && row.title.value[0] != '\0'
            ? row.title.value.data() : "Untitled chat";
        const bool stale_title = row.title.fact.availability == Availability::Known &&
            row.title.fact.freshness != Freshness::Current;
        auto &visual = draft.rows[index];
        set_row(visual, label, note ? note : lifecycle(row.lifecycle.value), Icon::PlanAccount,
            note || stale_title ? kAmber : kCyan, {Action::Chat, row.id.value});
        append_stale_state(visual, row.lifecycle.fact, lifecycle(row.lifecycle.value));
        if (stale_title && !note) copy(visual.detail, "Title not current");
    }
}

// draft: Bounded visual projection owned by the display worker.
// source: Copied rows from the selected chat.
// current: Its same-generation current turn, if known.
void project_turns(CodexBrowserPage &draft, const Domain::StateTurnPage &source, Domain::TurnId current)
{
    draft.count = source.count;
    for (uint8_t index = 0; index < source.count; ++index) {
        const auto &row = source.rows[index];
        const auto *note = state_note(row.execution.fact);
        auto &visual = draft.rows[index];
        set_row(visual, row.id == current ? "Current turn" : "Turn",
            note ? note : execution(row.execution.value), Icon::Clock, note ? kAmber : kCyan,
            {Action::Turn, row.id.value});
        append_stale_state(visual, row.execution.fact, execution(row.execution.value));
        if (!note && row.outcome.fact.availability == Availability::Known &&
            row.outcome.fact.freshness == Freshness::Current && row.outcome.value != Domain::Outcome::Unknown) {
            std::snprintf(visual.detail.data(), visual.detail.size(), "%s / %s",
                execution(row.execution.value), outcome(row.outcome.value));
        }
    }
}

// draft: Bounded visual projection owned by the display worker.
// source: Copied tool rows and their referenced definitions.
void project_calls(CodexBrowserPage &draft, const Domain::StateCallPage &source)
{
    draft.count = source.count;
    for (uint8_t index = 0; index < source.count; ++index) {
        const auto &row = source.rows[index];
        const auto *note = state_note(row.state.fact);
        const auto *label = row.label.fact.availability == Availability::Known && row.label.value[0] != '\0'
            ? row.label.value.data() : row.original_name.data();
        auto &visual = draft.rows[index];
        set_row(visual, label[0] ? label : "Unknown tool", note ? note : call_state(row.state.value),
            tool_icon(row.category.fact.availability == Availability::Known ? row.category.value : Domain::ToolCategory::Unknown),
            note ? kAmber : kCyan);
        append_stale_state(visual, row.state.fact, call_state(row.state.value));
        if (!note && row.outcome.fact.availability == Availability::Known &&
            row.outcome.fact.freshness == Freshness::Current && row.outcome.value != Domain::Outcome::Unknown) {
            std::snprintf(visual.detail.data(), visual.detail.size(), "%s / %s",
                call_state(row.state.value), outcome(row.outcome.value));
        }
    }
}

// draft: Display-owned read-only rows, independent of the signed reply owner.
// source: Pending question projections from one committed generation.
void project_questions(CodexBrowserPage &draft, const Domain::StateQuestionPage &source)
{
    draft.count = source.count;
    for (uint8_t index = 0; index < source.count; ++index) {
        const auto &row = source.rows[index];
        const bool prompt_known = row.prompt.fact.availability == Availability::Known && row.prompt.value[0] != '\0';
        auto &visual = draft.rows[index];
        set_row(visual, prompt_known ? row.prompt.value.data() : "Question text unavailable",
            "", Icon::Clock, kAmber);
        if (const auto *note = state_note(row.state.fact)) {
            copy(visual.detail, note);
            continue;
        }
        if (prompt_known && row.prompt.fact.freshness != Freshness::Current) {
            copy(visual.detail, "Text not current");
            continue;
        }
        if (row.chat.fact.availability == Availability::Absent) {
            copy(visual.detail, "No linked chat");
            continue;
        }
        if (row.chat.fact.availability != Availability::Known || !row.chat.value.valid()) {
            copy(visual.detail, "Chat link unknown");
            continue;
        }
        if (row.chat.fact.freshness != Freshness::Current) {
            copy(visual.detail, "Chat link stale");
            continue;
        }
        const auto *title = row.chat_title[0] ? row.chat_title.data() : "Title unavailable";
        const bool linked_turn = row.turn.fact.availability == Availability::Known &&
            row.turn.fact.freshness == Freshness::Current && row.turn.value.valid();
        // Keep the association cue visible even when the title needs ellipsis.
        const auto title_limit = static_cast<int>(visual.detail.size() - sizeof("Turn / "));
        std::snprintf(visual.detail.data(), visual.detail.size(), "%s / %.*s",
            linked_turn ? "Turn" : "Chat", title_limit, title);
    }
}

// browser: Current scope, page trail, and caller-owned query scratch.
void read_page(CodexBrowser &browser)
{
    auto &service = DeviceSnapshotService::instance();
    auto &draft = browser.draft;
    auto &position = trail(browser);
    Domain::TurnId current{};
    uint64_t header_generation = 0;
    std::array<char, 72> parent{};
    if (browser.route == Route::Turns && read_chat(browser, browser.chat) == ESP_OK) {
        const auto *page = std::get_if<Domain::StateChatPage>(&browser.observation.page);
        if (page && page->count == 1) {
            const auto &chat = page->rows[0];
            header_generation = browser.observation.generation;
            if (chat.title.fact.availability == Availability::Known) copy(parent, chat.title.value.data());
            if (chat.current_turn.fact.availability == Availability::Known &&
                chat.current_turn.fact.freshness == Freshness::Current) current = chat.current_turn.value;
        }
    } else if (browser.route == Route::Calls) {
        Domain::StateQuery request{};
        request.kind = StateQueryKind::Turn;
        request.turn = browser.turn;
        if (service.readCodexDomainObservation(request, browser.observation) == ESP_OK) {
            const auto *page = std::get_if<Domain::StateTurnPage>(&browser.observation.page);
            if (page && page->count == 1) {
                const auto &turn = page->rows[0];
                const auto *note = state_note(turn.execution.fact);
                header_generation = browser.observation.generation;
                copy(parent, note ? note : execution(turn.execution.value));
            }
        }
    }

    auto error = service.readCodexDomainObservation(page_request(browser, position), browser.observation);
    if (error == ESP_ERR_INVALID_STATE && position.cursor.generation != 0) {
        // Any intervening commit retires the cursor. Start at the first page
        // under the same identity scope instead of moving the selection.
        position.reset();
        error = service.readCodexDomainObservation(page_request(browser, position), browser.observation);
    }
    if (error != ESP_OK) {
        if (error == ESP_ERR_NOT_FOUND && (browser.route == Route::Turns || browser.route == Route::Calls)) {
            copy(draft.note, "Selection unavailable");
        } else {
            show_error(draft.note, error);
        }
        return;
    }

    const auto &result = browser.observation;
    draft.generation = result.generation;
    draft.source = result.summary.source;
    draft.next = result.next;
    draft.previous = position.count != 0;
    if (const auto *chats = std::get_if<Domain::StateChatPage>(&result.page)) {
        project_chats(draft, *chats);
    } else if (const auto *turns = std::get_if<Domain::StateTurnPage>(&result.page)) {
        project_turns(draft, *turns, header_generation == result.generation ? current : Domain::TurnId{});
    } else if (const auto *calls = std::get_if<Domain::StateCallPage>(&result.page)) {
        project_calls(draft, *calls);
    } else if (const auto *questions = std::get_if<Domain::StateQuestionPage>(&result.page)) {
        project_questions(draft, *questions);
    }

    auto kind = Domain::EntityKind::ToolCall;
    switch (browser.route) {
    case Route::Chats: kind = Domain::EntityKind::Chat; break;
    case Route::Turns: kind = Domain::EntityKind::Turn; break;
    case Route::PendingQuestions: kind = Domain::EntityKind::Question; break;
    default: break;
    }
    // A last page establishes a total only for this complete, live generation.
    // Unvisited cursor pages never imply an exact future total.
    const bool complete = result.health == StateViewHealth::Live && result.generation != 0 &&
        result.coverage[static_cast<uint8_t>(kind)].state == Domain::CoverageState::Complete;
    if (!complete || (position.known_generation != 0 && position.known_generation != result.generation)) {
        position.known_generation = 0;
        position.known_page_count = 0;
    }
    if (complete && result.next.after == 0) {
        position.known_generation = result.generation;
        position.known_page_count = static_cast<uint8_t>(position.count + 1U);
    }
    draft.page_index = position.count;
    if (complete && position.known_generation == result.generation &&
        position.known_page_count > position.count) {
        draft.known_page_count = position.known_page_count;
    }
    if (result.health != StateViewHealth::Live) {
        copy(draft.note, "Historical / stale data");
    } else if (result.coverage[static_cast<uint8_t>(kind)].state != Domain::CoverageState::Complete) {
        copy(draft.note, "Coverage incomplete");
    } else if (draft.count == 0) {
        const char *empty = "No records";
        if (browser.route == Route::ActiveCalls) empty = "No observed active calls";
        if (browser.route == Route::PendingQuestions) empty = "No pending questions";
        copy(draft.note, empty);
    } else if (parent[0] && header_generation == result.generation) {
        copy(draft.note, parent.data());
    }
    if (browser.saving_bookmark.valid()) {
        Domain::ChatId saved{};
        const auto saved_error = service.readCodexSelection(saved);
        if (saved_error == ESP_ERR_NOT_FINISHED) {
            copy(draft.note, "Saving bookmark");
        } else {
            if (saved_error != ESP_OK || saved != browser.saving_bookmark) {
                browser.selection_error = saved_error != ESP_OK ? saved_error : ESP_ERR_INVALID_RESPONSE;
            }
            browser.saving_bookmark = {};
        }
    }
    if (browser.selection_error != ESP_OK) {
        std::snprintf(draft.note.data(), draft.note.size(), "Selection: %s", esp_err_to_name(browser.selection_error));
    }
}

// left: Last rendered presentation, including retired control IDs.
// right: Newly projected captions and controls.
bool same_page(const CodexBrowserPage &left, const CodexBrowserPage &right)
{
    if (left.count != right.count || std::strcmp(left.note.data(), right.note.data()) != 0 ||
        left.source != right.source || left.resume != right.resume || left.generation != right.generation ||
        left.previous != right.previous || left.next.after != right.next.after ||
        left.page_index != right.page_index || left.known_page_count != right.known_page_count) return false;
    for (uint8_t index = 0; index < left.count; ++index) {
        const auto &a = left.rows[index];
        const auto &b = right.rows[index];
        if (std::strcmp(a.label.data(), b.label.data()) != 0 || std::strcmp(a.detail.data(), b.detail.data()) != 0 ||
            !(a.control == b.control) || a.icon != b.icon || a.color != b.color) return false;
    }
    return true;
}

// browser: Display owner with fixed text-line slots.
// text: ASCII source caption, copied and ellipsized before preparing glyphs.
// font: Existing native fixed-advance scene font.
// slot: Exact safe rectangle for the text line.
// color: Native RGB565 ink.
// centered: True for headings/footer rather than row titles.
// Exception: Preserves native font/layout error.
esp_err_t add_line(CodexBrowser &browser, const char *text, const CODY2D::MonoFont &font,
                   CODY2D::Rect slot, uint16_t color, bool centered)
{
    if (!text[0]) return ESP_OK;
    if (browser.line_count == browser.lines.size() || slot.height < font.height ||
        slot.width < font.width || font.advance == 0) return ESP_ERR_INVALID_SIZE;
    auto &prepared = browser.lines[browser.line_count];
    copy(prepared.text, text);
    const auto maximum = static_cast<size_t>((slot.width - font.width) / font.advance + 1);
    auto length = std::strlen(prepared.text.data());
    if (length > maximum) {
        if (maximum < 4) return ESP_ERR_INVALID_SIZE;
        length = maximum;
        std::memcpy(prepared.text.data() + length - 3, "...", 4);
    }
    const CODY2D::TextSpan span{prepared.text.data(), static_cast<uint16_t>(length)};
    uint32_t width = 0;
    const auto measured = CODY2D::measure_text_width(font, span, width);
    if (measured != ESP_OK) return measured;
    if (width > slot.width) return ESP_ERR_INVALID_SIZE;
    const auto x = slot.x + (centered ? (slot.width - width) / 2U : 0U);
    const auto y = slot.y + (slot.height - font.height) / 2U;
    const auto error = CODY2D::prepare_text_line(font, span,
        {static_cast<int16_t>(x), static_cast<int16_t>(y)}, color, prepared.line);
    if (error == ESP_OK) ++browser.line_count;
    return error;
}

// browser: Last prepared panel.
// index: Fixed button slot, separately tracked for touch.
// bounds: Complete rounded button within the circular safe square.
// action: Semantic target for this button.
// caption: Static English button caption.
// Exception: Preserves native glyph preparation errors.
esp_err_t add_button(CodexBrowser &browser, uint8_t index, CODY2D::Rect bounds,
                     CodexBrowserControl action, const char *caption)
{
    auto &control = browser.controls[index];
    control.bounds = bounds;
    control.top_color = 0x2947;
    control.bottom_color = 0x10A3;
    control.corner_radius = 16;
    control.border_color = 0x39E7;
    browser.actions[index] = action;
    const auto &font = SceneData::body_font;
    const CODY2D::TextSpan span{caption, static_cast<uint16_t>(std::strlen(caption))};
    uint32_t width = 0;
    const auto error = CODY2D::measure_text_width(font, span, width);
    if (error != ESP_OK) return error;
    if (width > bounds.width || font.height > bounds.height) return ESP_ERR_INVALID_SIZE;
    return CODY2D::prepare_text_line(font, span, {
        static_cast<int16_t>(bounds.x + (bounds.width - width) / 2U),
        static_cast<int16_t>(bounds.y + (bounds.height - font.height) / 2U)}, kWhite, control.caption);
}

// browser: Captured page from one canonical generation.
// Exception: Returns the first preparation error from the native renderer.
esp_err_t prepare_layout(CodexBrowser &browser)
{
    browser.line_count = 0;
    browser.controls = {};
    browser.actions = {};
    browser.paging_hint = {};
    const auto &page = browser.page;
    auto error = add_line(browser, heading(browser.route), SceneData::title_font, {73, 89, 320, 44}, kWhite, true);
    if (error == ESP_OK) error = add_line(browser, page.note.data(), SceneData::body_font,
        {73, 136, 320, 26}, kMuted, true);
    for (uint8_t index = 0; error == ESP_OK && index < page.count; ++index) {
        const auto y = static_cast<uint16_t>(168 + index * 60);
        const auto &row = page.rows[index];
        auto &control = browser.controls[index];
        control.bounds = {93, y, 280, 56};
        control.top_color = 0x2125;
        control.bottom_color = 0x1082;
        control.corner_radius = 16;
        control.border_color = 0x31A6;
        control.icon = row.icon;
        control.icon_center = {120, static_cast<int16_t>(y + 28)};
        control.icon_color = row.color;
        browser.actions[index] = row.control;
        error = add_line(browser, row.label.data(), SceneData::body_font,
            {141, static_cast<uint16_t>(y + 3), 218, 25}, kWhite, false);
        if (error == ESP_OK) error = add_line(browser, row.detail.data(), SceneData::body_font,
            {141, static_cast<uint16_t>(y + 28), 218, 25}, row.color, false);
    }
    if (error != ESP_OK) return error;
    if (browser.route == Route::Hub) {
        const bool resume = page.resume.valid();
        const CODY2D::Rect statistics = resume ? CODY2D::Rect{87, 350, 116, 42} : CODY2D::Rect{91, 350, 138, 42};
        const CODY2D::Rect events = resume ? CODY2D::Rect{211, 350, 80, 42} : CODY2D::Rect{237, 350, 138, 42};
        error = add_button(browser, 3, statistics, {Action::Statistics}, "Statistics");
        if (error == ESP_OK) error = add_button(browser, 4, events, {Action::Events}, "Events");
        if (error == ESP_OK && resume) {
            error = add_button(browser, 5, {299, 350, 80, 42}, {Action::Resume, page.resume.value}, "Resume");
        }
    } else if ((page.previous || page.next.after != 0) &&
               (page.known_page_count == 0 || page.known_page_count > 16)) {
        // Only a cursor is known; invite paging without presenting a made-up total.
        static constexpr char text[] = "Swipe to browse";
        const CODY2D::TextSpan span{text, sizeof(text) - 1U};
        uint32_t width = 0;
        error = CODY2D::measure_text_width(SceneData::body_font, span, width);
        if (error == ESP_OK && width > 260) error = ESP_ERR_INVALID_SIZE;
        if (error == ESP_OK) error = CODY2D::prepare_text_line(SceneData::body_font, span,
            {static_cast<int16_t>((466U - width) / 2U), 350}, kMuted, browser.paging_hint);
    }
    return error;
}

// state: Immutable display-owner panel through strip retirement.
// strip: Borrowed native RGB565 tile.
// row: Logical first row of the tile.
// column: Logical first column of the tile.
// Exception: Returns the first native raster error.
esp_err_t draw_rows(const void *state, CODY2D::Surface strip, uint16_t row, uint16_t column)
{
    if (!state) return ESP_ERR_INVALID_ARG;
    const auto &browser = *static_cast<const CodexBrowser *>(state);
    for (size_t index = 0; index < browser.controls.size(); ++index) {
        const auto &control = browser.controls[index];
        if (control.bounds.width == 0) continue;
        const auto error = index < browser.page.count ?
            draw_flat_menu_row(control, 30, strip, row, column) :
            CODY2D::draw_capsule_rows(control, strip, row, column);
        if (error != ESP_OK) return error;
    }
    for (uint8_t index = 0; index < browser.line_count; ++index) {
        const auto &line = browser.lines[index].line;
        const auto error = CODY2D::draw_text_rows(line, line.text.length, strip, row, column);
        if (error != ESP_OK) return error;
    }
    if (browser.paging_hint.valid) {
        const auto error = CODY2D::draw_text_rows(browser.paging_hint, browser.paging_hint.text.length,
            strip, row, column);
        if (error != ESP_OK) return error;
    }
    if (browser.route != Route::Hub && browser.page.known_page_count > 1 && browser.page.known_page_count <= 16) {
        return draw_page_marks(browser.page.page_index, browser.page.known_page_count, strip, row, column);
    }
    return ESP_OK;
}

// browser: Owner of the visible bounded page.
// chat: Exact row or resume target to confirm against the current generation.
// bookmark: True only for an explicit choice on the Chats page.
void choose_chat(CodexBrowser &browser, Domain::ChatId chat, bool bookmark)
{
    const auto error = read_chat(browser, chat);
    const auto *page = std::get_if<Domain::StateChatPage>(&browser.observation.page);
    if (error != ESP_OK || !page || page->count != 1 || page->rows[0].id != chat ||
        (!bookmark && browser.observation.health != StateViewHealth::Live)) {
        browser.selection_error = error != ESP_OK ? error : ESP_ERR_INVALID_STATE;
        return;
    }
    browser.chat = chat;
    browser.turns.reset();
    browser.route = Route::Turns;
    browser.selection_error = bookmark ? DeviceSnapshotService::instance().saveCodexSelection(chat) : ESP_OK;
    if (bookmark && browser.selection_error == ESP_OK) browser.saving_bookmark = chat;
}

} // namespace

// browser: Display-owned navigation, fresh entry from the native Tools page.
void open_codex_browser(CodexBrowser &browser)
{
    browser.route = Route::Hub;
    browser.refresh_at_us = 0;
    browser.selection_error = ESP_OK;
}

// browser: Display-owned browser retaining the page of each caller.
void back_codex_browser(CodexBrowser &browser)
{
    switch (browser.route) {
    case Route::Hub: browser.route = Route::Closed; break;
    case Route::Chats:
    case Route::ActiveCalls:
    case Route::PendingQuestions: browser.route = Route::Hub; break;
    case Route::Turns: browser.route = browser.turns_parent; break;
    case Route::Calls: browser.route = Route::Turns; break;
    default: break;
    }
    browser.refresh_at_us = 0;
    browser.selection_error = ESP_OK;
}

// browser: Off-stack display owner storage.
// now_us: Worker monotonic time for the refresh interval.
// content: Captured frame view, borrowed only through strip retirement.
// Exception: Preserves native caption/raster preparation errors.
esp_err_t prepare_codex_browser(CodexBrowser &browser, int64_t now_us, SceneContent &content)
{
    if (browser.route == Route::Closed) return ESP_ERR_INVALID_STATE;
    if (browser.refresh_at_us == 0 || now_us >= browser.refresh_at_us || browser.drawn_route != browser.route) {
        browser.refresh_at_us = now_us + kRefreshUs;
        auto &draft = browser.draft;
        draft.note.fill(0);
        draft.source = {};
        draft.resume = {};
        draft.next = {};
        draft.generation = 0;
        draft.count = 0;
        draft.page_index = 0;
        draft.known_page_count = 0;
        draft.previous = false;
        if (browser.route == Route::Hub) read_hub(browser);
        else read_page(browser);
        if (browser.route != browser.drawn_route || !same_page(browser.page, draft)) {
            browser.page = draft;
            const auto error = prepare_layout(browser);
            if (error != ESP_OK) {
                browser.drawn_route = Route::Closed;
                return error;
            }
            browser.drawn_route = browser.route;
            if (++browser.revision == 0) ++browser.revision;
        }
    }
    content = {};
    content.bounds = kBounds;
    content.state = &browser;
    content.revision = browser.revision;
    content.control_revision = browser.revision;
    content.draw_rows = draw_rows;
    return ESP_OK;
}

// browser: Captured panel used by the last successfully retired frame.
// point: Logical coordinate from the existing rotated touch owner.
CodexBrowserControl codex_browser_control_at(const CodexBrowser &browser, CODY2D::Point point)
{
    for (size_t index = 0; index < browser.controls.size(); ++index) {
        if (browser.actions[index].action != Action::None && CODY2D::capsule_contains(browser.controls[index], point)) {
            return browser.actions[index];
        }
    }
    return {};
}

// browser: Current page and stable selection owned by the display thread.
// control: Matched action copied from the visible control at touch start and end.
CodexBrowserDestination activate_codex_browser(CodexBrowser &browser, CodexBrowserControl control)
{
    browser.refresh_at_us = 0;
    browser.selection_error = ESP_OK;
    switch (control.action) {
    case Action::Chats:
        browser.route = Route::Chats;
        browser.chats.reset();
        break;
    case Action::ActiveCalls:
        browser.route = Route::ActiveCalls;
        browser.records.reset();
        break;
    case Action::PendingQuestions:
        browser.route = Route::PendingQuestions;
        browser.records.reset();
        break;
    case Action::Events:
        return Destination::Events;
    case Action::Statistics:
        return Destination::Statistics;
    case Action::Resume:
        if (browser.page.resume.value == control.id && browser.page.resume.valid()) {
            browser.turns_parent = Route::Hub;
            choose_chat(browser, browser.page.resume, false);
        }
        break;
    case Action::Chat:
        browser.turns_parent = Route::Chats;
        choose_chat(browser, {browser.page.source, control.id}, true);
        break;
    case Action::Turn: {
        Domain::StateQuery request{};
        request.kind = StateQueryKind::Turn;
        request.turn = {browser.page.source, control.id};
        const auto error = DeviceSnapshotService::instance().readCodexDomainObservation(request, browser.observation);
        const auto *page = std::get_if<Domain::StateTurnPage>(&browser.observation.page);
        if (error != ESP_OK || !page || page->count != 1 || page->rows[0].id != request.turn ||
            page->rows[0].chat != browser.chat) {
            browser.selection_error = error != ESP_OK ? error : ESP_ERR_INVALID_RESPONSE;
            break;
        }
        browser.turn = request.turn;
        browser.route = Route::Calls;
        browser.records.reset();
        break;
    }
    case Action::PreviousPage: {
        auto &position = trail(browser);
        if (position.count == 0) break;
        const auto after = position.previous[--position.count];
        if (after == 0) position.cursor = {};
        else position.cursor.after = after;
        break;
    }
    case Action::NextPage: {
        auto &position = trail(browser);
        if (browser.page.next.after == 0 || position.count == position.previous.size()) break;
        position.previous[position.count++] = position.cursor.after;
        position.cursor = browser.page.next;
        break;
    }
    default:
        break;
    }
    return Destination::None;
}

} // namespace CODY

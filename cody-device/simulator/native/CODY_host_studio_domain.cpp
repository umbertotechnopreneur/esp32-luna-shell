// Synthetic query fixture for the isolated CodexBrowser presenter. This never
// authenticates a peer, admits production StateStore data or sends a packet.
#include "cody_host_studio.h"
#include "CODY_state_query.hpp"
#include "esp_timer.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <limits>
#include <mutex>
#include <new>

namespace CODY::host_studio_domain {
namespace {
using namespace Domain;

struct FixtureOwner {
    std::mutex mutex;
    CodyHostStudioCase input{};
    uint64_t generation = 0;
    int64_t published_at_us = 0;
    bool available = false;
};
// Host workers have process lifetime; they cannot outlive this allocated owner.
FixtureOwner *const s_owner = new (std::nothrow) FixtureOwner{};

// value: Bounded native ABI string, including room for its terminator.
// empty_allowed: Whether known-empty text is meaningful for this field.
// Exception: No exceptions; false rejects unterminated or non-ASCII input.
template<size_t N> bool text_valid(const char (&value)[N], bool empty_allowed = false)
{
    const auto *end = std::find(value, value + N, '\0');
    if (end == value + N || (!empty_allowed && end == value)) return false;
    return std::all_of(value, end, [](char c) { return c >= 32 && c <= 126; });
}

// value: Explicit canonical execution spelling from the host scenario.
// output: Typed native value; unchanged for an unsupported spelling.
// Exception: No exceptions; false rejects an invalid enum spelling.
bool execution(const char *value, ExecutionState &output)
{
    const char *names[]{"unknown", "queued", "running", "waiting", "stopped", "interrupted"};
    const ExecutionState values[]{ExecutionState::Unknown, ExecutionState::Queued, ExecutionState::Running,
        ExecutionState::Waiting, ExecutionState::Stopped, ExecutionState::Interrupted};
    for (size_t index = 0; index < std::size(names); ++index)
        if (std::strcmp(value, names[index]) == 0) { output = values[index]; return true; }
    return false;
}

// value: Explicit canonical category spelling from the host scenario.
// output: Typed native value; unchanged for an unsupported spelling.
// Exception: No exceptions; false rejects an invalid enum spelling.
bool category(const char *value, ToolCategory &output)
{
    const char *names[]{"unknown", "command", "file_read", "file_edit", "search", "browser", "agent", "service"};
    const ToolCategory values[]{ToolCategory::Unknown, ToolCategory::Command, ToolCategory::FileRead,
        ToolCategory::FileEdit, ToolCategory::Search, ToolCategory::Browser, ToolCategory::Agent, ToolCategory::Service};
    for (size_t index = 0; index < std::size(names); ++index)
        if (std::strcmp(value, names[index]) == 0) { output = values[index]; return true; }
    return false;
}

// value: Explicit canonical call-state spelling from the host scenario.
// output: Typed native value; unchanged for an unsupported spelling.
// Exception: No exceptions; false rejects an invalid enum spelling.
bool call_state(const char *value, CallState &output)
{
    const char *names[]{"unknown", "queued", "running", "returned", "failed", "cancelled"};
    const CallState values[]{CallState::Unknown, CallState::Queued, CallState::Running,
        CallState::Returned, CallState::Failed, CallState::Cancelled};
    for (size_t index = 0; index < std::size(names); ++index)
        if (std::strcmp(value, names[index]) == 0) { output = values[index]; return true; }
    return false;
}

// value: Explicit canonical outcome spelling from the host scenario.
// output: Typed native value; unchanged for an unsupported spelling.
// Exception: No exceptions; false rejects an invalid enum spelling.
bool outcome(const char *value, Outcome &output)
{
    const char *names[]{"unknown", "succeeded", "failed", "cancelled"};
    const Outcome values[]{Outcome::Unknown, Outcome::Succeeded, Outcome::Failed, Outcome::Cancelled};
    for (size_t index = 0; index < std::size(names); ++index)
        if (std::strcmp(value, names[index]) == 0) { output = values[index]; return true; }
    return false;
}

// input: Already validated ABI record.
// Exception: No exceptions; reports only observed queued/running calls as active.
bool active(const CodyHostStudioCall &input)
{
    return std::strcmp(input.state, "queued") == 0 || std::strcmp(input.state, "running") == 0;
}

// known: Whether the synthetic source actually supplies this field.
// Exception: No exceptions; facts are explicitly synthetic, never authenticated.
StateFactView fact(bool known = true)
{
    StateFactView result{};
    result.availability = known ? Availability::Known : Availability::Unknown;
    result.freshness = Freshness::Current;
    result.provenance = Provenance::Synthetic;
    result.observation = {{s_owner->input.domain_source_id}, s_owner->generation};
    result.observed_at = {static_cast<uint64_t>(s_owner->published_at_us), {1}, TimeBasis::MonotonicMicroseconds};
    result.age_state = Availability::Known;
    result.age_us = static_cast<uint64_t>(esp_timer_get_time() - s_owner->published_at_us);
    return result;
}

// result: Caller-owned canonical page, already reset after query admission.
// Exception: No exceptions; all counters describe only this isolated fixture.
void qualify(StateQueryResult &result)
{
    const auto &input = s_owner->input;
    result.summary.source = {input.domain_source_id};
    result.summary.epoch = {1};
    result.summary.revision = input.domain_generation;
    result.summary.chats = result.summary.turns = 1;
    for (uint32_t index = 0; index < input.domain_call_count; ++index) {
        if (active(input.domain_calls[index])) ++result.summary.active_calls;
        else if (std::strcmp(input.domain_calls[index].state, "unknown") != 0) ++result.summary.terminal_calls;
    }
    result.health = StateViewHealth::Live;
    // Local admission generation retires cursors even when a scenario reuses
    // its declared synthetic source revision. No domain reducer is bypassed
    // as if this were authenticated data: this is a labelled query fixture.
    result.generation = s_owner->generation;
    for (uint8_t index = 0; index < static_cast<uint8_t>(EntityKind::Count); ++index) {
        auto &coverage = result.coverage[index];
        coverage.source = {input.domain_source_id};
        coverage.kind = static_cast<EntityKind>(index);
        coverage.state = CoverageState::Unsupported;
        coverage.omitted_state = Availability::Unknown;
    }
    for (const auto kind : {EntityKind::Source, EntityKind::Chat, EntityKind::Turn, EntityKind::ToolCall}) {
        auto &coverage = result.coverage[static_cast<uint8_t>(kind)];
        coverage.state = CoverageState::Complete;
        coverage.admitted = kind == EntityKind::ToolCall ? input.domain_call_count : 1;
        coverage.omitted = 0;
        coverage.omitted_state = Availability::Known;
    }
    result.attention.running_turns = std::strcmp(input.domain_execution, "running") == 0 ? 1 : 0;
    result.attention.waiting_turns = std::strcmp(input.domain_execution, "waiting") == 0 ? 1 : 0;
    result.attention.active_calls = result.summary.active_calls;
    result.attention.turns_complete = std::strcmp(input.domain_execution, "unknown") != 0;
    result.attention.calls_complete = true;
    for (uint32_t index = 0; index < input.domain_call_count; ++index)
        if (std::strcmp(input.domain_calls[index].state, "unknown") == 0) result.attention.calls_complete = false;
    result.attention.questions = Capability::Unsupported;
}
}

// request: Bounded synthetic page, with exact selection and coherent call scopes.
// Exception: Rejects invalid IDs, fields and enum spellings without publication.
esp_err_t validate(const CodyHostStudioCase &request)
{
    if (std::strncmp(request.identity.target, "cody.codex.calls", sizeof(request.identity.target)) != 0) return ESP_OK;
    if (!request.domain_source_id || !request.domain_chat_id || !request.domain_turn_id ||
        !request.domain_generation || request.domain_call_count > std::size(request.domain_calls) ||
        request.identity.source_id != request.domain_source_id || request.identity.chat_id != request.domain_chat_id ||
        request.identity.turn_id != request.domain_turn_id || !text_valid(request.domain_chat_title) ||
        !text_valid(request.domain_execution)) return ESP_ERR_INVALID_ARG;
    ExecutionState execution_value{};
    if (!execution(request.domain_execution, execution_value)) return ESP_ERR_INVALID_ARG;
    for (uint32_t index = 0; index < request.domain_call_count; ++index) {
        const auto &call = request.domain_calls[index];
        if (!call.id || call.turn_id != request.domain_turn_id || !text_valid(call.tool_name) ||
            !text_valid(call.label) || !text_valid(call.category) || !text_valid(call.state) || !text_valid(call.outcome))
            return ESP_ERR_INVALID_ARG;
        ToolCategory category_value{}; CallState state_value{}; Outcome outcome_value{};
        if (!category(call.category, category_value) || !call_state(call.state, state_value) || !outcome(call.outcome, outcome_value))
            return ESP_ERR_INVALID_ARG;
        if ((state_value == CallState::Failed && outcome_value != Outcome::Failed) ||
            (state_value == CallState::Cancelled && outcome_value != Outcome::Cancelled) ||
            ((state_value == CallState::Queued || state_value == CallState::Running || state_value == CallState::Unknown) &&
             outcome_value != Outcome::Unknown)) return ESP_ERR_INVALID_ARG;
        for (uint32_t previous = 0; previous < index; ++previous) {
            const auto &other = request.domain_calls[previous];
            if (other.id == call.id) return ESP_ERR_INVALID_ARG;
            if (std::strcmp(other.tool_name, call.tool_name) == 0 &&
                (std::strcmp(other.category, call.category) != 0 || std::strcmp(other.label, call.label) != 0))
                return ESP_ERR_INVALID_ARG;
        }
    }
    return ESP_OK;
}

// request: Validated native case; non-domain cases explicitly retire the fixture.
// Exception: Preserves validation, allocation and generation-overflow errors.
esp_err_t publish(const CodyHostStudioCase &request)
{
    const auto error = validate(request);
    if (error != ESP_OK) return error;
    if (!s_owner) return ESP_ERR_NO_MEM;
    std::lock_guard<std::mutex> lock(s_owner->mutex);
    if (std::strncmp(request.identity.target, "cody.codex.calls", sizeof(request.identity.target)) != 0) {
        s_owner->available = false;
        return ESP_OK;
    }
    if (s_owner->generation == std::numeric_limits<uint64_t>::max()) return ESP_ERR_INVALID_STATE;
    const auto now = esp_timer_get_time();
    if (now <= 0) return ESP_ERR_INVALID_STATE;
    s_owner->input = request;
    s_owner->published_at_us = now;
    ++s_owner->generation;
    s_owner->available = true;
    return ESP_OK;
}

// request: Canonical bounded query from the unchanged production presenter.
// result: Copied synthetic projection; unchanged on unsupported or invalid queries.
// Exception: Preserves unavailable, wrong-scope and stale-cursor failures.
esp_err_t query(const StateQuery &request, StateQueryResult &result)
{
    if (!s_owner) return ESP_ERR_NO_MEM;
    std::lock_guard<std::mutex> lock(s_owner->mutex);
    if (!s_owner->available) return ESP_ERR_NOT_SUPPORTED;
    const auto &input = s_owner->input;
    const ChatId chat{{input.domain_source_id}, input.domain_chat_id};
    const TurnId turn{{input.domain_source_id}, input.domain_turn_id};
    if ((request.chat.valid() && request.chat != chat) || (request.turn.valid() && request.turn != turn)) return ESP_ERR_NOT_FOUND;
    if ((request.kind == StateQueryKind::Chat || request.kind == StateQueryKind::Turns) && request.chat != chat) return ESP_ERR_NOT_FOUND;
    if ((request.kind == StateQueryKind::Turn || request.kind == StateQueryKind::Calls) && request.turn != turn) return ESP_ERR_NOT_FOUND;
    if (request.kind == StateQueryKind::PendingQuestions) return ESP_ERR_NOT_SUPPORTED;
    if (esp_timer_get_time() < s_owner->published_at_us) return ESP_ERR_INVALID_STATE;
    const auto &cursor = request.cursor;
    if (cursor.generation != 0 && (cursor.generation != s_owner->generation || cursor.source.value != input.domain_source_id ||
        cursor.epoch.value != 1 || cursor.revision != input.domain_generation || cursor.kind != request.kind ||
        cursor.chat != request.chat || cursor.turn != request.turn)) return ESP_ERR_INVALID_STATE;
    if (cursor.after != 0 && cursor.generation == 0) return ESP_ERR_INVALID_STATE;
    if (static_cast<uint8_t>(request.kind) > static_cast<uint8_t>(StateQueryKind::Turn)) return ESP_ERR_NOT_SUPPORTED;
    uint32_t begin = 0;
    if (cursor.after != 0) {
        if (request.kind != StateQueryKind::Calls && request.kind != StateQueryKind::ActiveCalls) return ESP_ERR_INVALID_STATE;
        bool found = false;
        for (uint32_t index = 0; index < input.domain_call_count; ++index) {
            if (input.domain_calls[index].id == cursor.after &&
                (request.kind != StateQueryKind::ActiveCalls || active(input.domain_calls[index]))) {
                begin = index + 1;
                found = true;
                break;
            }
        }
        if (!found) return ESP_ERR_INVALID_STATE;
    }
    result.reset();
    qualify(result);
    if (request.kind == StateQueryKind::Chat || request.kind == StateQueryKind::Chats) {
        auto &page = result.page.emplace<StateChatPage>();
        page.count = 1;
        auto &row = page.rows[0];
        row.id = chat;
        std::copy_n(input.domain_chat_title, std::strlen(input.domain_chat_title) + 1, row.title.value.begin());
        row.title.fact = row.lifecycle.fact = row.current_turn.fact = fact();
        row.lifecycle.value = ChatLifecycle::Open;
        row.current_turn.value = turn;
    } else if (request.kind == StateQueryKind::Turn || request.kind == StateQueryKind::Turns) {
        auto &page = result.page.emplace<StateTurnPage>();
        page.count = 1;
        auto &row = page.rows[0];
        row.id = turn;
        row.chat = chat;
        execution(input.domain_execution, row.execution.value);
        row.execution.fact = fact(row.execution.value != ExecutionState::Unknown);
        row.outcome.fact = row.reply.fact = fact(false);
    } else if (request.kind == StateQueryKind::Calls || request.kind == StateQueryKind::ActiveCalls) {
        auto &page = result.page.emplace<StateCallPage>();
        uint32_t index = begin;
        while (index < input.domain_call_count && page.count < kStateQueryPageSize) {
            const auto &call = input.domain_calls[index++];
            if (request.kind == StateQueryKind::ActiveCalls && !active(call)) continue;
            auto &row = page.rows[page.count++];
            row.id = {{input.domain_source_id}, call.id};
            row.turn = turn;
            uint64_t definition_id = call.id;
            for (uint32_t first = 0; first + 1 < index; ++first)
                if (std::strcmp(input.domain_calls[first].tool_name, call.tool_name) == 0) { definition_id = input.domain_calls[first].id; break; }
            row.definition = {{input.domain_source_id}, definition_id};
            std::copy_n(call.tool_name, std::strlen(call.tool_name) + 1, row.original_name.begin());
            std::copy_n(call.label, std::strlen(call.label) + 1, row.label.value.begin());
            category(call.category, row.category.value);
            call_state(call.state, row.state.value);
            outcome(call.outcome, row.outcome.value);
            row.label.fact = fact();
            row.category.fact = fact(row.category.value != ToolCategory::Unknown);
            row.state.fact = fact(row.state.value != CallState::Unknown);
            row.outcome.fact = fact(row.outcome.value != Outcome::Unknown);
        }
        while (index < input.domain_call_count && request.kind == StateQueryKind::ActiveCalls && !active(input.domain_calls[index])) ++index;
        if (index < input.domain_call_count && page.count != 0) {
            result.next = {{input.domain_source_id}, {1}, input.domain_generation, s_owner->generation,
                           page.rows[page.count - 1].id.value, request.kind, request.chat, request.turn};
        }
    }
    return ESP_OK;
}
} // namespace CODY::host_studio_domain

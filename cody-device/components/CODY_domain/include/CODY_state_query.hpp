/* Copyright (c) 2026 Umberto Giacobbi */
#pragma once

#include "CODY_domain_types.hpp"

#include <array>
#include <variant>

namespace CODY::Domain {

struct StateSummary {
    SourceId source{};
    EpochId epoch{};
    uint64_t revision = 0;
    uint32_t chats = 0;
    uint32_t turns = 0;
    // Retained rows, including records whose evidence may no longer be current.
    uint32_t active_calls = 0;
    uint32_t terminal_calls = 0;
};

enum class StateQueryKind : uint8_t {
    Summary, Chat, Chats, Turns, Calls, ActiveCalls, PendingQuestions, Turn,
};
enum class StateViewHealth : uint8_t { Unavailable, Historical, Live, Stale };

// Paging requires this exact committed bank. A token is a stable record ID
// within the requested kind and scope, never an index into a backing array.
struct StateCursor {
    SourceId source{};
    EpochId epoch{};
    uint64_t revision = 0;
    uint64_t generation = 0;
    uint64_t after = 0;
    StateQueryKind kind = StateQueryKind::Summary;
    ChatId chat{};
    TurnId turn{};
};

struct StateQuery {
    StateQueryKind kind = StateQueryKind::Summary;
    ChatId chat{};
    TurnId turn{};
    StateCursor cursor{};
};

// Scalar metadata is copied; the renderer never retains a live-bank pointer.
struct StateFactView {
    Availability availability = Availability::Unknown;
    Freshness freshness = Freshness::Unknown;
    Provenance provenance = Provenance::Unknown;
    ObservationId observation{};
    SourceTime observed_at{};
    uint64_t age_us = 0;
    Availability age_state = Availability::Unknown;
    ErrorKind error = ErrorKind::None;
    int64_t original_code = 0;
};

template<class T>
struct StateValueView {
    T value{};
    StateFactView fact{};
};

struct StateChatRow {
    ChatId id{};
    StateValueView<std::array<char, 256>> title{};
    StateValueView<ChatLifecycle> lifecycle{};
    StateValueView<TurnId> current_turn{};
};

struct StateTurnRow {
    TurnId id{};
    ChatId chat{};
    StateValueView<ExecutionState> execution{};
    StateValueView<Outcome> outcome{};
    StateValueView<ReplyState> reply{};
};

struct StateCallRow {
    ToolCallId id{};
    TurnId turn{};
    ToolDefinitionId definition{};
    std::array<char, 129> original_name{};
    StateValueView<std::array<char, 97>> label{};
    StateValueView<ToolCategory> category{};
    StateValueView<CallState> state{};
    StateValueView<Outcome> outcome{};
};

inline constexpr uint8_t kStateQueryPageSize = 3;

template<class T>
struct StatePage {
    std::array<T, kStateQueryPageSize> rows{};
    uint8_t count = 0;
};

using StateChatPage = StatePage<StateChatRow>;
using StateTurnPage = StatePage<StateTurnRow>;
using StateCallPage = StatePage<StateCallRow>;
using StateQueryPage = std::variant<std::monostate, StateChatPage, StateTurnPage, StateCallPage>;

// Counts include current evidence only. An incomplete result is a lower bound
// and cannot imply that no work exists. Questions have no owner in this profile.
struct StateAttention {
    uint32_t running_turns = 0;
    uint32_t waiting_turns = 0;
    uint32_t active_calls = 0;
    bool turns_complete = false;
    bool calls_complete = false;
    Capability questions = Capability::Unsupported;
};

struct StateQueryResult {
    StateSummary summary{};
    StateViewHealth health = StateViewHealth::Unavailable;
    uint64_t generation = 0;
    StateCursor next{}; // Empty after the last page.
    std::array<Coverage, static_cast<uint8_t>(EntityKind::Count)> coverage{};
    StateAttention attention{}; // Populated by Summary queries.
    StateQueryPage page{};

    // No parameters: reset the projection without a second page temporary
    // on the caller's stack.
    void reset()
    {
        summary = {};
        health = StateViewHealth::Unavailable;
        generation = 0;
        next = {};
        coverage = {};
        attention = {};
        page.emplace<std::monostate>();
    }
};

static_assert(sizeof(StateQueryResult) <= 2560, "Keep the caller-owned query copy bounded");

} // namespace CODY::Domain

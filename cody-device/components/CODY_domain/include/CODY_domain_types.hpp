/* Copyright (c) 2026 Umberto Giacobbi */
#pragma once

#include <cstdint>
#include <optional>
#include <variant>

namespace CODY::Domain {

using std::int64_t;
using std::uint8_t;
using std::uint16_t;
using std::uint32_t;
using std::uint64_t;

// Tokens are identities, never positions in an array. Zero is unassigned.
template <typename Tag>
struct Token {
    uint64_t value = 0;

    // No parameters: reports whether an identity was assigned.
    constexpr bool valid() const { return value != 0; }
};

// left: First token in this identity namespace.
// right: Second token in the same identity namespace.
template <typename Tag>
constexpr bool operator==(Token<Tag> left, Token<Tag> right)
{
    return left.value == right.value;
}

// left: First token in this identity namespace.
// right: Second token in the same identity namespace.
template <typename Tag>
constexpr bool operator!=(Token<Tag> left, Token<Tag> right)
{
    return !(left == right);
}

using SourceId = Token<struct SourceTag>;
using EpochId = Token<struct EpochTag>;
using ClockId = Token<struct ClockTag>;
using EvidenceId = Token<struct EvidenceTag>;

// The broker persists its source/native-ID mapping before publishing a new ID.
// Epoch, display order, selected chat and native focus are not identity inputs.
template <typename Tag>
struct EntityId {
    SourceId source{};
    uint64_t value = 0;

    // No parameters: both the registered source and its entity token are required.
    constexpr bool valid() const { return source.valid() && value != 0; }
};

// left: First source-qualified entity identity.
// right: Second identity of the same entity kind.
template <typename Tag>
constexpr bool operator==(EntityId<Tag> left, EntityId<Tag> right)
{
    return left.source == right.source && left.value == right.value;
}

// left: First source-qualified entity identity.
// right: Second identity of the same entity kind.
template <typename Tag>
constexpr bool operator!=(EntityId<Tag> left, EntityId<Tag> right)
{
    return !(left == right);
}

using ChatId = EntityId<struct ChatTag>;
using TurnId = EntityId<struct TurnTag>;
using ToolDefinitionId = EntityId<struct ToolDefinitionTag>;
using ToolCallId = EntityId<struct ToolCallTag>;
using AgentTaskId = EntityId<struct AgentTaskTag>;
using ExternalOperationId = EntityId<struct ExternalOperationTag>;
using QuestionId = EntityId<struct QuestionTag>;
using ChoiceId = EntityId<struct ChoiceTag>;
using AnswerAttemptId = EntityId<struct AnswerAttemptTag>;
using WaitId = EntityId<struct WaitTag>;
using ObservationId = EntityId<struct ObservationTag>;

// Text offsets are storage details, valid only within their owning generation.
// Text is length-delimited printable ASCII; an empty known string is not unknown.
struct TextRef {
    uint32_t offset = 0;
    uint16_t length = 0;
};

enum class Availability : uint8_t { Unknown, Known, Absent, Unsupported, Failed, Conflict };
enum class Freshness : uint8_t { Unknown, Current, Stale, Historical };
enum class Provenance : uint8_t { Unknown, NativeEvent, NativeRead, Broker, DeviceAction, Restored, Synthetic };
enum class TimeBasis : uint8_t { Unknown, UnixMicroseconds, MonotonicMicroseconds };
enum class ErrorKind : uint8_t {
    None, InvalidIdentity, DuplicateIdentity, InvalidValue, MissingReference,
    RevisionConflict, CapacityExceeded, Unsupported, Persistence, Transport, Provider,
};

struct Error {
    ErrorKind kind = ErrorKind::None;
    int64_t original_code = 0;
    TextRef detail{};
};

struct SourceTime {
    uint64_t microseconds = 0;
    ClockId clock{};
    TimeBasis basis = TimeBasis::Unknown;
};

struct Evidence {
    EvidenceId id{};
    // Stable broker observation identity; id above is the receiver-local handle.
    ObservationId observation{};
    SourceId source{};
    EpochId epoch{};
    uint64_t revision = 0;
    // Store-wide ordering for retention; source revisions from different epochs
    // or sources are not comparable. This is not a provider timestamp.
    uint64_t admitted_sequence = 0;
    SourceTime observed_at{};
    uint64_t age_at_receive_us = 0;
    Availability age_state = Availability::Unknown;
    uint32_t lease_ms = 0;
    Availability lease_state = Availability::Unknown;
    uint64_t received_at_us = 0;
    ClockId receiver_clock{};
    Freshness freshness = Freshness::Unknown;
    Provenance provenance = Provenance::Unknown;
    Error error{};
};

// Each field keeps its own evidence. A new title cannot refresh old turn state.
// Non-Known payloads must never be consumed as current facts.
template <typename Value>
struct Fact {
    Value value{};
    EvidenceId evidence{};
    Availability availability = Availability::Unknown;
};

enum class Capability : uint8_t { Unknown, Supported, Unsupported };
enum class LinkState : uint8_t { Unknown, Disconnected, Connecting, Authenticated, Failed };
enum class CoverageState : uint8_t { Unknown, Complete, Partial, Unsupported };
enum class EntityKind : uint8_t {
    Source, Chat, Turn, ToolDefinition, ToolCall, AgentTask, ExternalOperation,
    Question, Choice, AnswerAttempt, Wait,
    Count, // Storage sizing only; never an admitted entity kind.
};

struct Coverage {
    SourceId source{};
    EntityKind kind = EntityKind::Chat;
    CoverageState state = CoverageState::Unknown;
    uint32_t admitted = 0;
    uint32_t omitted = 0;
    Availability omitted_state = Availability::Unknown;
};

struct SemanticCapabilities {
    Capability chats = Capability::Unknown;
    Capability turns = Capability::Unknown;
    Capability tool_calls = Capability::Unknown;
    Capability agents = Capability::Unknown;
    Capability external_operations = Capability::Unknown;
    Capability questions = Capability::Unknown;
    Capability recipient_acknowledgement = Capability::Unknown;
    Capability resume_questions = Capability::Unknown;
};

struct Source {
    SourceId id{};
    TextRef provider{};
    TextRef native_namespace{};
    EpochId epoch{};
    uint64_t admitted_revision = 0;
    SemanticCapabilities capabilities{};
    Fact<LinkState> link{};
};

enum class ChatLifecycle : uint8_t { Unknown, Open, Closed, Archived };
enum class ExecutionState : uint8_t { Unknown, Queued, Running, Waiting, Stopped, Interrupted };
enum class Outcome : uint8_t { Unknown, Succeeded, Failed, Cancelled };
enum class ReplyState : uint8_t { Unknown, Unavailable, Available };
enum class ToolCategory : uint8_t { Unknown, Command, FileRead, FileEdit, Search, Browser, Agent, Service };
enum class CallState : uint8_t { Unknown, Queued, Running, Returned, Failed, Cancelled };
enum class AgentState : uint8_t { Unknown, Queued, Running, Waiting, Stopped, Failed, Cancelled };
enum class OperationState : uint8_t { Unknown, Pending, Running, Finished, Failed, Cancelled };
enum class QuestionKind : uint8_t { Unknown, Answerable, NativeApprovalNotice };
enum class QuestionState : uint8_t { Unknown, Open, Answered, Cancelled, Superseded };
enum class AnswerState : uint8_t {
    Unknown, Admitted, Persisted, Sending, BrokerReceived, RecipientDelivered,
    Rejected, Cancelled, Unconfirmed,
};
enum class TicketState : uint8_t { Unknown, Available, Expired, Unavailable };

struct Chat {
    ChatId id{};
    Fact<TextRef> title{};
    Fact<ChatLifecycle> lifecycle{};
    Fact<TurnId> current_turn{};
};

struct Turn {
    TurnId id{};
    ChatId chat{};
    Fact<ExecutionState> execution{};
    Fact<Outcome> outcome{};
    Fact<ReplyState> reply{};
};

struct ToolDefinition {
    ToolDefinitionId id{};
    TextRef original_name{};
    Fact<TextRef> display_label{};
    Fact<ToolCategory> category{};
};

struct ToolCall {
    ToolCallId id{};
    TurnId turn{};
    ToolDefinitionId definition{};
    Fact<AgentTaskId> executing_agent{};
    Fact<CallState> state{};
    Fact<Outcome> outcome{};
};

struct AgentTask {
    AgentTaskId id{};
    TurnId owning_turn{};
    Fact<AgentTaskId> parent{};
    Fact<AgentState> state{};
    Fact<Outcome> outcome{};
};

struct ExternalOperation {
    ExternalOperationId id{};
    TurnId owning_turn{};
    Fact<ToolCallId> originating_call{};
    Fact<TextRef> native_identity{};
    Fact<OperationState> state{};
    Fact<Outcome> outcome{};
};

// Unassociated questions remain explicit; they never borrow the selected chat.
struct Question {
    QuestionId id{};
    Fact<ChatId> chat{};
    Fact<TurnId> turn{};
    Fact<QuestionKind> kind{};
    Fact<QuestionState> state{};
    Fact<TextRef> prompt{};
    Fact<TicketState> ticket{};
};

struct Choice {
    ChoiceId id{};
    QuestionId question{};
    uint16_t display_order = 0;
    Fact<TextRef> label{};
    Fact<TextRef> detail{};
};

struct AnswerAttempt {
    AnswerAttemptId id{};
    QuestionId question{};
    ChoiceId choice{};
    // The attempt ID is also its immutable end-to-end delivery correlation.
    Fact<AnswerState> state{};
};

struct UnknownWait {};
struct UserWait { Fact<QuestionId> question{}; };
struct ToolWait { ToolCallId call{}; };
struct AgentWait { AgentTaskId agent{}; };
struct ServiceWait { Fact<ExternalOperationId> operation{}; };
using WaitTarget = std::variant<UnknownWait, UserWait, ToolWait, AgentWait, ServiceWait>;

struct WaitDependency {
    WaitId id{};
    TurnId turn{};
    WaitTarget target{};
    Fact<bool> blocking{};
};

using RemovedId = std::variant<SourceId, ChatId, TurnId, ToolDefinitionId, ToolCallId,
    AgentTaskId, ExternalOperationId, QuestionId, ChoiceId, AnswerAttemptId, WaitId>;

struct Tombstone {
    RemovedId id{};
    EpochId epoch{};
    uint64_t revision = 0;
};

enum class SelectionMode : uint8_t { FollowPrompt, Pinned };

// Local view context is outside the source registry and its mutation decisions.
// Route IDs and gesture state belong to the UI; a voice destination has its own owner.
struct SelectionState {
    SelectionMode mode = SelectionMode::FollowPrompt;
    // Local choices do not borrow evidence from a replaceable source generation.
    std::optional<ChatId> selected_chat{};
    std::optional<ChatId> return_chat{};
};

} // namespace CODY::Domain

/* Copyright (c) 2026 Umberto Giacobbi */
#pragma once

#include "CODY_domain_types.hpp"

namespace CODY::Domain {

// Design limits, not allocated arrays or a claim about available board memory.
// Active records and terminal history have separate admission allowances.
struct CapacityLimits {
    uint32_t sources = 4;
    uint32_t chats = 64;
    uint32_t turns = 192;
    uint32_t tool_definitions = 128;
    uint32_t active_calls = 256;
    uint32_t terminal_calls = 128;
    uint32_t agents = 64;
    uint32_t operations = 64;
    uint32_t open_questions = 64;
    uint32_t resolved_questions = 32;
    uint32_t choices = 288;
    uint32_t answer_attempts = 192;
    uint32_t waits = 256;
    uint32_t evidence = 4096;
    uint32_t tombstones = 512;
    uint32_t text_bytes = 128 * 1024;
    uint32_t decode_scratch_bytes = 64 * 1024;
    uint32_t persistence_scratch_bytes = 32 * 1024;
};

// Planned sorted secondary index: stable entity token -> owner-local slot.
// Entity tables are separate by kind; the index is not a semantic identifier.
struct IndexEntry {
    SourceId source{};
    uint64_t value = 0;
    uint32_t slot = 0;
};

struct StorageBudget {
    uint64_t records_bytes = 0;
    uint64_t index_bytes = 0;
    uint64_t evidence_bytes = 0;
    uint64_t text_bytes = 0;
    uint64_t tombstone_bytes = 0;
    uint64_t staging_bytes = 0;
    uint64_t scratch_bytes = 0;

    // No parameters: includes a full replacement generation and both scratch areas.
    constexpr uint64_t total_bytes() const
    {
        return records_bytes + index_bytes + evidence_bytes + text_bytes +
            tombstone_bytes + staging_bytes + scratch_bytes;
    }
};

// limits: Negotiated table bounds, including retained history and pooled text.
// Returns payload capacity using the consuming compiler's real type layouts.
// Allocator bookkeeping, alignment between allocations, stacks, driver buffers
// and existing application memory must be reserved separately by the adapter.
constexpr StorageBudget storage_budget(const CapacityLimits &limits)
{
    const uint64_t calls = uint64_t{limits.active_calls} + limits.terminal_calls;
    const uint64_t questions = uint64_t{limits.open_questions} + limits.resolved_questions;
    const uint64_t indexed_records = uint64_t{limits.sources} + limits.chats + limits.turns +
        limits.tool_definitions + calls + limits.agents + limits.operations + questions +
        limits.choices + limits.answer_attempts + limits.waits;
    // Keep intermediate arithmetic 64-bit even on 32-bit targets.
    StorageBudget result{};
    result.records_bytes = uint64_t{limits.sources} * sizeof(Source) +
        uint64_t{limits.chats} * sizeof(Chat) + uint64_t{limits.turns} * sizeof(Turn) +
        uint64_t{limits.tool_definitions} * sizeof(ToolDefinition) + calls * sizeof(ToolCall) +
        uint64_t{limits.agents} * sizeof(AgentTask) +
        uint64_t{limits.operations} * sizeof(ExternalOperation) + questions * sizeof(Question) +
        uint64_t{limits.choices} * sizeof(Choice) +
        uint64_t{limits.answer_attempts} * sizeof(AnswerAttempt) +
        uint64_t{limits.waits} * sizeof(WaitDependency) +
        uint64_t{limits.sources} * static_cast<uint8_t>(EntityKind::Count) * sizeof(Coverage);
    // Evidence references also need a bounded lookup; conservatively reserve
    // the same entry size as entity indexes, rather than assuming direct slots.
    result.index_bytes = (indexed_records + limits.evidence) * sizeof(IndexEntry);
    result.evidence_bytes = uint64_t{limits.evidence} * sizeof(Evidence);
    result.text_bytes = limits.text_bytes;
    result.tombstone_bytes = uint64_t{limits.tombstones} * sizeof(Tombstone);
    result.staging_bytes = result.records_bytes + result.index_bytes + result.evidence_bytes +
        result.text_bytes + result.tombstone_bytes;
    result.scratch_bytes = uint64_t{limits.decode_scratch_bytes} + limits.persistence_scratch_bytes;
    return result;
}

// No parameters: stress candidate for the existing 64 chats x 64 calls ceiling.
// It is never selected automatically; negotiation and measured memory must admit it.
constexpr CapacityLimits extended_call_candidate()
{
    CapacityLimits limits{};
    limits.active_calls = 4096;
    limits.evidence = 16384;
    limits.waits = 512;
    limits.text_bytes = 256 * 1024;
    return limits;
}

} // namespace CODY::Domain

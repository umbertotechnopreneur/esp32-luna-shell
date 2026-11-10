/* Copyright (c) 2026 Umberto Giacobbi */
#pragma once

#include "CODY_domain_types.hpp"

namespace CODY::Domain::Protocol {

inline constexpr char kSchema[] = "cody.state/1";
inline constexpr uint8_t kWireVersion = 1;
inline constexpr uint16_t kHeaderBytes = 144;
inline constexpr uint16_t kAuthenticationBytes = 32;
inline constexpr uint16_t kChunkBytes = 1024;
inline constexpr uint16_t kDatagramBytes = 1200;
inline constexpr uint32_t kDocumentBytes = 8 * 1024 * 1024;
inline constexpr uint32_t kControlDocumentBytes = 4096;
inline constexpr uint16_t kReceiveWindow = 16;
inline constexpr uint16_t kAckPayloadBytes = 12;
inline constexpr uint32_t kTransferTimeoutMs = 120000;
inline constexpr uint32_t kTransferIdleTimeoutMs = 5000;
inline constexpr uint32_t kRetransmitMs = 500;
inline constexpr uint8_t kRetryLimit = 5;
inline constexpr uint32_t kProbeIntervalMs = 2000;
inline constexpr uint32_t kProbeTimeoutMs = 2000;
inline constexpr uint32_t kLinkLeaseMs = 8000;
inline constexpr uint32_t kMaximumEvidenceLeaseMs = 120000;
inline constexpr uint32_t kMaximumCues = 32;

static_assert(kHeaderBytes + kChunkBytes + kAuthenticationBytes == kDatagramBytes);

enum class PacketKind : uint8_t { Data = 1, Ack = 2, Ping = 3, Pong = 4 };
enum class AckState : uint8_t { Receiving = 1, Committed = 2, Rejected = 3 };
enum class MessageKind : uint8_t { Snapshot, Delta };
enum class RejectCode : uint8_t {
    None = 0, Malformed = 1, UnsupportedVersion = 2, UnsupportedCapability = 3,
    WrongBinding = 4, RevisionConflict = 5, NeedSnapshot = 6, MissingReference = 7,
    CapacityExceeded = 8, TransferTimeout = 9, Integrity = 10, Busy = 11,
    Persistence = 12, InvalidTransition = 13,
};

// Decoded values only: never memcpy this struct onto the wire. Offsets and
// network byte order are specified in protocol/CODY_STATE_V1.md.
struct ChunkGeometry {
    uint32_t document_bytes = 0;
    uint32_t chunk_count = 0;
    uint32_t chunk_index = 0;
    uint16_t payload_bytes = 0;
};

// document_bytes: Exact immutable JSON byte count, not an allocation request.
// Returns zero for an empty or over-budget document.
constexpr uint32_t chunk_count(uint32_t document_bytes)
{
    if (document_bytes == 0 || document_bytes > kDocumentBytes) return 0;
    return document_bytes / kChunkBytes + (document_bytes % kChunkBytes != 0 ? 1U : 0U);
}

// geometry: Authenticated data-header values, before staging any payload.
// Returns Malformed for inconsistent lengths, indices or document bounds.
constexpr RejectCode check_chunk_geometry(const ChunkGeometry &geometry)
{
    const auto count = chunk_count(geometry.document_bytes);
    if (count == 0 || geometry.chunk_count != count || geometry.chunk_index >= count) {
        return RejectCode::Malformed;
    }
    const uint32_t offset = geometry.chunk_index * uint32_t{kChunkBytes};
    const auto remaining = geometry.document_bytes - offset;
    const auto expected = remaining < kChunkBytes ? remaining : kChunkBytes;
    return geometry.payload_bytes == expected ? RejectCode::None : RejectCode::Malformed;
}

// source/epoch are installed only by the authenticated negotiation owner.
// A gap changes needs_snapshot; it does not discard the committed generation.
struct SyncCursor {
    SourceId source{};
    EpochId epoch{};
    uint64_t revision = 0;
    bool has_snapshot = false;
    bool needs_snapshot = true;
};

struct Transaction {
    SourceId source{};
    EpochId epoch{};
    uint64_t revision = 0;
    uint64_t base_revision = 0;
    MessageKind kind = MessageKind::Snapshot;
    // True only after matching a locally pending request in this exact session.
    bool request_matched = false;
};

enum class RevisionDecision : uint8_t {
    StageSnapshot, StageDelta, CheckDuplicate, IgnoreStale, NeedSnapshot,
    RejectBinding, RejectEnvelope, RejectUnrequestedUpdate,
};

// cursor: Current authenticated binding and last atomically committed revision.
// transaction: Decoded, authenticated envelope; this function does not parse JSON.
// No mutation or acceptance occurs here. Staging still requires full entity,
// capacity, evidence, hash and lifecycle checks before one atomic commit.
constexpr RevisionDecision assess_revision(const SyncCursor &cursor, const Transaction &transaction)
{
    if (!cursor.source.valid() || !cursor.epoch.valid() ||
        transaction.source != cursor.source || transaction.epoch != cursor.epoch) {
        return RevisionDecision::RejectBinding;
    }
    if (transaction.revision == 0 ||
        (cursor.has_snapshot && cursor.revision == 0) ||
        (!cursor.has_snapshot && cursor.revision != 0)) {
        return RevisionDecision::RejectEnvelope;
    }
    if (!transaction.request_matched) return RevisionDecision::RejectUnrequestedUpdate;
    if (transaction.kind == MessageKind::Snapshot) {
        if (transaction.base_revision != 0) return RevisionDecision::RejectEnvelope;
        if (cursor.has_snapshot && transaction.revision < cursor.revision) {
            return RevisionDecision::IgnoreStale;
        }
        return RevisionDecision::StageSnapshot;
    }
    if (transaction.kind != MessageKind::Delta ||
        transaction.base_revision >= transaction.revision) {
        return RevisionDecision::RejectEnvelope;
    }
    if (!cursor.has_snapshot) return RevisionDecision::NeedSnapshot;
    if (transaction.revision < cursor.revision) return RevisionDecision::IgnoreStale;
    if (transaction.revision == cursor.revision) {
        // Compare the last committed delta's exact digest/base before ACKing.
        // A matching revision alone is not proof of identical content.
        return RevisionDecision::CheckDuplicate;
    }
    if (cursor.needs_snapshot || transaction.base_revision != cursor.revision) {
        return RevisionDecision::NeedSnapshot;
    }
    return RevisionDecision::StageDelta;
}

} // namespace CODY::Domain::Protocol

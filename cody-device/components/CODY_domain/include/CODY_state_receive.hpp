/* Copyright (c) 2026 Umberto Giacobbi */
#pragma once

#include "CODY_state_protocol.hpp"

#include <array>
#include <cstddef>

namespace CODY::Domain::Protocol {

enum class SequenceDecision : uint8_t { Accepted, Duplicate, TooOld, Invalid };

// Owned by one authenticated direction for the lifetime of its link session.
class ReplayWindow {
public:
    // sequence: Nonzero sequence from an already authenticated packet.
    SequenceDecision admit(uint64_t sequence);

private:
    uint64_t newest_ = 0;
    uint64_t seen_ = 0;
};

struct TransferIdentity {
    std::array<uint8_t, 16> transfer{};
    SourceId source{};
    EpochId epoch{};
    uint64_t revision = 0;
    uint32_t document_bytes = 0;
    uint32_t chunks = 0;
    std::array<uint8_t, 32> digest{};
};

struct DataChunk {
    TransferIdentity identity{};
    uint32_t index = 0;
    const uint8_t *bytes = nullptr;
    uint16_t size = 0;
};

struct DatagramView {
    PacketKind kind = PacketKind::Data;
    std::array<uint8_t, 6> device{};
    std::array<uint8_t, 16> peer{};
    std::array<uint8_t, 16> session{};
    uint64_t sequence = 0;
    DataChunk chunk{};
    const uint8_t *tag = nullptr;
};

struct AckPayload {
    uint16_t received_bitmap = 0;
    AckState state = AckState::Receiving;
    RejectCode error = RejectCode::None;
    uint64_t committed_revision = 0;
};

// datagram: Exact received UDP bytes, valid for the lifetime of the output view.
// length: Datagram byte count, including the authentication tag.
// output: Filled only after header/length validation; payload and tag borrow datagram.
// The result remains unauthenticated. Check endpoint, binding, role and tag
// before using any of these fields or changing replay or transfer state.
RejectCode parse_datagram(const uint8_t *datagram, std::size_t length, DatagramView &output);

// packet: Authenticated ACK whose transfer identity matches the send window.
// width: Negotiated send-window width used to bound the selective ACK bitmap.
// output: Filled only for a structurally valid ACK. The sender still checks it
// against its own sent window and committed revision before releasing bytes.
RejectCode decode_ack(const DatagramView &packet, uint16_t width, AckPayload &output);

struct ReceiveAck {
    uint32_t next_missing = 0;
    uint16_t received_bitmap = 0;
};

enum class ChunkDisposition : uint8_t { Buffered, Progress, Complete, Duplicate, OutsideWindow, Rejected };

struct ReceiveResult {
    ChunkDisposition disposition;
    RejectCode error = RejectCode::None;
};

// The sink consumes contiguous bytes into a staging generation and hash. It
// must not publish live state or retain the input pointer after returning.
using ConsumeBytes = RejectCode (*)(void *, const uint8_t *, uint16_t);

// Keep this fixed window in receive-owner storage, off callback/task stacks.
// One link owner serializes calls. Authentication, hash verification, decoder
// rollback, generation commit and final ACK retention belong to that owner.
class ReceiveWindow {
public:
    // identity: Authenticated transfer header matched to the pending request.
    // width: Negotiated number of simultaneously receivable chunks.
    // max_bytes: Negotiated document ceiling before the state/control limit.
    // request_us: Original request timestamp from the receiver monotonic clock.
    // now_us: Current reading of the same clock.
    RejectCode begin(const TransferIdentity &identity, uint16_t width, uint32_t max_bytes,
                     uint64_t request_us, uint64_t now_us);

    // chunk: Authenticated DATA packet with the identity repeated in its header.
    // now_us: Current receiver time in the same clock used by begin.
    // consume: Decoder/hash sink called synchronously for each contiguous chunk.
    // context: Opaque staging context passed through to the sink.
    ReceiveResult accept(const DataChunk &chunk, uint64_t now_us,
                         ConsumeBytes consume, void *context);

    // now_us: Current receiver time; backwards motion invalidates the transfer.
    // The owner calls this on idle ticks as well as at receipt.
    RejectCode check_deadline(uint64_t now_us);

    // No parameters: snapshot of the receiving ACK fields, never a commit ACK.
    ReceiveAck acknowledgement() const;

    // No parameters: release the window on cancel, failed admission or commit.
    void reset();

private:
    struct Slot {
        std::array<uint8_t, kChunkBytes> bytes;
        uint16_t size;
    };

    // error: Original reason to return to the transfer owner.
    ReceiveResult reject(RejectCode error);

    std::array<Slot, kReceiveWindow> slots_;
    TransferIdentity identity_{};
    uint64_t request_us_ = 0;
    uint64_t last_progress_us_ = 0;
    uint64_t last_clock_us_ = 0;
    uint32_t next_missing_ = 0;
    uint16_t width_ = 0;
    uint16_t buffered_ = 0;
};

} // namespace CODY::Domain::Protocol

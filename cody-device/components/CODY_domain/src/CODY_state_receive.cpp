/* Copyright (c) 2026 Umberto Giacobbi */
#include "CODY_state_receive.hpp"

#include <algorithm>
#include <cstring>

namespace CODY::Domain::Protocol {
namespace {

// bytes: Pointer to a validated range of network-order integer bytes.
// size: Number of bytes to read, at most eight.
uint64_t read_network(const uint8_t *bytes, std::size_t size)
{
    uint64_t value = 0;
    for (std::size_t i = 0; i < size; ++i) value = (value << 8U) | bytes[i];
    return value;
}

// identity: Decoded transfer metadata, before admitting a transfer.
bool valid_identity(const TransferIdentity &identity)
{
    const bool no_source = !identity.source.valid() && !identity.epoch.valid() && identity.revision == 0;
    const bool has_source = identity.source.valid() && identity.epoch.valid() && identity.revision != 0;
    const auto count = chunk_count(identity.document_bytes);
    return (no_source || has_source) && count != 0 && count == identity.chunks &&
        std::any_of(identity.transfer.begin(), identity.transfer.end(),
                    [](uint8_t byte) { return byte != 0; });
}

// left: The active transfer's immutable metadata.
// right: Metadata repeated by an authenticated data chunk.
bool same_transfer(const TransferIdentity &left, const TransferIdentity &right)
{
    return left.transfer == right.transfer && left.source == right.source &&
        left.epoch == right.epoch && left.revision == right.revision &&
        left.document_bytes == right.document_bytes && left.chunks == right.chunks &&
        left.digest == right.digest;
}

} // namespace

// datagram: Borrowed bytes from one UDP packet including tag.
// length: Exact received length; no trailing bytes are accepted.
// output: Decoded metadata and borrowed byte ranges on success.
RejectCode parse_datagram(const uint8_t *datagram, std::size_t length, DatagramView &output)
{
    if (!datagram || length < kHeaderBytes + kAuthenticationBytes || length > kDatagramBytes ||
        std::memcmp(datagram, "CDS1", 4) != 0) {
        return RejectCode::Malformed;
    }
    if (datagram[4] != kWireVersion) return RejectCode::UnsupportedVersion;
    if (datagram[5] < static_cast<uint8_t>(PacketKind::Data) ||
        datagram[5] > static_cast<uint8_t>(PacketKind::Pong) ||
        read_network(datagram + 6, 2) != 0 || read_network(datagram + 140, 4) != 0) {
        return RejectCode::Malformed;
    }

    DatagramView candidate{};
    candidate.kind = static_cast<PacketKind>(datagram[5]);
    std::copy_n(datagram + 8, candidate.device.size(), candidate.device.begin());
    std::copy_n(datagram + 14, candidate.peer.size(), candidate.peer.begin());
    std::copy_n(datagram + 30, candidate.session.size(), candidate.session.begin());
    candidate.sequence = read_network(datagram + 46, 8);
    auto &chunk = candidate.chunk;
    auto &identity = chunk.identity;
    std::copy_n(datagram + 54, identity.transfer.size(), identity.transfer.begin());
    identity.source.value = read_network(datagram + 70, 8);
    identity.epoch.value = read_network(datagram + 78, 8);
    identity.revision = read_network(datagram + 86, 8);
    chunk.index = static_cast<uint32_t>(read_network(datagram + 94, 4));
    identity.chunks = static_cast<uint32_t>(read_network(datagram + 98, 4));
    identity.document_bytes = static_cast<uint32_t>(read_network(datagram + 102, 4));
    chunk.size = static_cast<uint16_t>(read_network(datagram + 106, 2));
    std::copy_n(datagram + 108, identity.digest.size(), identity.digest.begin());
    if (candidate.sequence == 0 ||
        length != std::size_t{kHeaderBytes} + chunk.size + kAuthenticationBytes) {
        return RejectCode::Malformed;
    }

    if (candidate.kind == PacketKind::Ping || candidate.kind == PacketKind::Pong) {
        const bool has_nonce = std::any_of(identity.transfer.begin(), identity.transfer.end(),
                                          [](uint8_t byte) { return byte != 0; });
        const bool has_digest = std::any_of(identity.digest.begin(), identity.digest.end(),
                                            [](uint8_t byte) { return byte != 0; });
        if (!has_nonce || has_digest || identity.source.valid() || identity.epoch.valid() ||
            identity.revision != 0 || identity.chunks != 0 || identity.document_bytes != 0 ||
            chunk.index != 0 || chunk.size != 0) {
            return RejectCode::Malformed;
        }
    } else {
        if (!valid_identity(identity) ||
            (!identity.source.valid() && identity.document_bytes > kControlDocumentBytes)) {
            return RejectCode::Malformed;
        }
        if (candidate.kind == PacketKind::Data) {
            const ChunkGeometry geometry{identity.document_bytes, identity.chunks, chunk.index, chunk.size};
            if (check_chunk_geometry(geometry) != RejectCode::None) return RejectCode::Malformed;
        } else if (chunk.size != kAckPayloadBytes || chunk.index > identity.chunks) {
            return RejectCode::Malformed;
        }
    }

    chunk.bytes = datagram + kHeaderBytes;
    candidate.tag = chunk.bytes + chunk.size;
    output = candidate;
    return RejectCode::None;
}

// packet: Authenticated ACK with transfer identity checked by the sender.
// width: Window size negotiated for the transfer.
// output: Acknowledgement body, written only after every field is admitted.
RejectCode decode_ack(const DatagramView &packet, uint16_t width, AckPayload &output)
{
    const auto &chunk = packet.chunk;
    if (packet.kind != PacketKind::Ack || !chunk.bytes || chunk.size != kAckPayloadBytes ||
        width == 0 || width > kReceiveWindow || chunk.index > chunk.identity.chunks) {
        return RejectCode::Malformed;
    }

    const auto bitmap = static_cast<uint16_t>(read_network(chunk.bytes, 2));
    const auto state = static_cast<AckState>(chunk.bytes[2]);
    const auto error = static_cast<RejectCode>(chunk.bytes[3]);
    const auto revision = read_network(chunk.bytes + 4, 8);
    const auto remaining = chunk.identity.chunks - chunk.index;
    const auto valid_bits = std::min(uint32_t{width}, remaining);
    if ((bitmap & 1U) != 0 || (uint32_t{bitmap} >> valid_bits) != 0 ||
        (!chunk.identity.source.valid() && revision != 0)) {
        return RejectCode::Malformed;
    }
    switch (state) {
    case AckState::Receiving:
        if (error != RejectCode::None) return RejectCode::Malformed;
        break;
    case AckState::Committed:
        if (error != RejectCode::None || remaining != 0 || revision != chunk.identity.revision) {
            return RejectCode::Malformed;
        }
        break;
    case AckState::Rejected:
        if (error == RejectCode::None || error > RejectCode::InvalidTransition) {
            return RejectCode::Malformed;
        }
        break;
    default:
        return RejectCode::Malformed;
    }
    output = {bitmap, state, error, revision};
    return RejectCode::None;
}

// sequence: Nonzero sequence from an already authenticated packet.
SequenceDecision ReplayWindow::admit(uint64_t sequence)
{
    constexpr auto window = 64U;
    if (sequence == 0) return SequenceDecision::Invalid;
    if (sequence > newest_) {
        const auto distance = sequence - newest_;
        seen_ = distance < window ? (seen_ << distance) | 1U : 1U;
        newest_ = sequence;
        return SequenceDecision::Accepted;
    }

    const auto distance = newest_ - sequence;
    if (distance >= window) return SequenceDecision::TooOld;
    const auto bit = uint64_t{1} << distance;
    if ((seen_ & bit) != 0) return SequenceDecision::Duplicate;
    seen_ |= bit;
    return SequenceDecision::Accepted;
}

// identity: Authenticated transfer header matched to the pending request.
// width: Negotiated window size, between one and the protocol maximum.
// max_bytes: Negotiated document ceiling.
// request_us: Original monotonic request timestamp.
// now_us: Current time in the receiver's monotonic clock.
RejectCode ReceiveWindow::begin(const TransferIdentity &identity, uint16_t width,
                                uint32_t max_bytes, uint64_t request_us, uint64_t now_us)
{
    if (width_ != 0) return RejectCode::Busy;
    if (width == 0 || width > kReceiveWindow || max_bytes == 0 || max_bytes > kDocumentBytes) {
        return RejectCode::Malformed;
    }
    if (!valid_identity(identity)) return RejectCode::Malformed;
    const bool control = !identity.source.valid();
    if (identity.document_bytes > max_bytes ||
        (control && identity.document_bytes > kControlDocumentBytes)) {
        return RejectCode::CapacityExceeded;
    }
    if (now_us < request_us || now_us - request_us >= uint64_t{kTransferTimeoutMs} * 1000U) {
        return RejectCode::TransferTimeout;
    }
    identity_ = identity;
    request_us_ = request_us;
    last_progress_us_ = now_us;
    last_clock_us_ = now_us;
    width_ = width;
    return RejectCode::None;
}

// chunk: Authenticated DATA metadata and payload, valid only for this call.
// now_us: Current monotonic time in the receiver's clock.
// consume: Synchronous staging decoder/hash sink.
// context: Opaque staging decoder/hash context.
ReceiveResult ReceiveWindow::accept(const DataChunk &chunk, uint64_t now_us,
                                    ConsumeBytes consume, void *context)
{
    if (width_ == 0) return {ChunkDisposition::Rejected, RejectCode::WrongBinding};
    if (chunk.identity.transfer != identity_.transfer) {
        return {ChunkDisposition::Rejected, RejectCode::Busy};
    }
    if (!same_transfer(identity_, chunk.identity)) return reject(RejectCode::Integrity);
    const auto deadline = check_deadline(now_us);
    if (deadline != RejectCode::None) return {ChunkDisposition::Rejected, deadline};
    const ChunkGeometry geometry{identity_.document_bytes, identity_.chunks, chunk.index, chunk.size};
    if (!chunk.bytes || !consume || check_chunk_geometry(geometry) != RejectCode::None) {
        return reject(RejectCode::Malformed);
    }
    if (chunk.index < next_missing_) return {ChunkDisposition::Duplicate};
    if (chunk.index - next_missing_ >= width_) return {ChunkDisposition::OutsideWindow};

    const auto position = chunk.index % width_;
    const auto bit = uint16_t{1} << position;
    auto &slot = slots_[position];
    if ((buffered_ & bit) != 0) {
        if (slot.size != chunk.size || std::memcmp(slot.bytes.data(), chunk.bytes, chunk.size) != 0) {
            return reject(RejectCode::Integrity);
        }
        return {ChunkDisposition::Duplicate};
    }

    std::memcpy(slot.bytes.data(), chunk.bytes, chunk.size);
    slot.size = chunk.size;
    buffered_ |= bit;
    if (chunk.index != next_missing_) return {ChunkDisposition::Buffered};

    while (next_missing_ < identity_.chunks) {
        const auto next_position = next_missing_ % width_;
        const auto next_bit = uint16_t{1} << next_position;
        if ((buffered_ & next_bit) == 0) break;
        const auto &next = slots_[next_position];
        const auto error = consume(context, next.bytes.data(), next.size);
        if (error != RejectCode::None) return reject(error);
        buffered_ &= ~next_bit;
        ++next_missing_;
    }
    last_progress_us_ = now_us;
    return {next_missing_ == identity_.chunks ? ChunkDisposition::Complete : ChunkDisposition::Progress};
}

// now_us: Current receiver time in the same clock as request and progress.
RejectCode ReceiveWindow::check_deadline(uint64_t now_us)
{
    if (width_ == 0) return RejectCode::None;
    if (now_us < last_clock_us_ ||
        now_us - request_us_ >= uint64_t{kTransferTimeoutMs} * 1000U ||
        now_us - last_progress_us_ >= uint64_t{kTransferIdleTimeoutMs} * 1000U) {
        reset();
        return RejectCode::TransferTimeout;
    }
    last_clock_us_ = now_us;
    return RejectCode::None;
}

// No parameters: describe current window relative to the first missing chunk.
ReceiveAck ReceiveWindow::acknowledgement() const
{
    ReceiveAck ack{next_missing_, 0};
    for (uint16_t i = 0; i < width_ && next_missing_ + i < identity_.chunks; ++i) {
        const auto position = (next_missing_ + i) % width_;
        if ((buffered_ & (uint16_t{1} << position)) != 0) ack.received_bitmap |= uint16_t{1} << i;
    }
    return ack;
}

// No parameters: forget admission metadata without copying or clearing the
// whole buffer. Slot occupancy is reset and unoccupied slots cannot be read.
void ReceiveWindow::reset()
{
    identity_ = {};
    request_us_ = 0;
    last_progress_us_ = 0;
    last_clock_us_ = 0;
    next_missing_ = 0;
    width_ = 0;
    buffered_ = 0;
}

// error: Original failure to propagate after releasing the receive window.
ReceiveResult ReceiveWindow::reject(RejectCode error)
{
    reset();
    return {ChunkDisposition::Rejected, error};
}

} // namespace CODY::Domain::Protocol

/* Copyright (c) 2026 Umberto Giacobbi */
#include "LUNASHELL_codex_state_wire.hpp"

#include "CODY_state_store.hpp"
#include "LUNASHELL_codex_state_control.hpp"
#include "LUNASHELL_codex_state_saved.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <new>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "lwip/inet.h"
#include "psa/crypto.h"

namespace LUNASHELL::codex_state {
namespace {

namespace Domain = CODY::Domain;
namespace Protocol = Domain::Protocol;
using Protocol::AckState;
using Protocol::PacketKind;
using Protocol::RejectCode;
using Protocol::TransferIdentity;

constexpr char kTag[] = "CodyState";
constexpr char kMacDomain[] = "cody.state.datagram/1";
constexpr size_t kMacPrefixSize = sizeof(kMacDomain) + 1;
constexpr uint16_t kDevicePort = 42117;
constexpr uint16_t kPeerPort = 42118;
constexpr uint32_t kLocalDocumentBytes = 1024 * 1024;
constexpr uint64_t kRequestPeriodUs = 500000;
constexpr uint64_t kBootstrapTimeoutUs = 2000000;
constexpr uint64_t kCapacityRetryUs = 5000000;
constexpr unsigned kDecodeScratchBytes = 64 * 1024;

enum class Phase : uint8_t { Idle, Finding, Offer, Accepted, Live };

struct Incoming {
    TransferIdentity identity{};
    bool running = false;
    bool control = false;
    uint16_t control_bytes = 0;
};

struct Outgoing {
    std::array<char, Protocol::kControlDocumentBytes> bytes{};
    TransferIdentity identity{};
    uint64_t sent_at_us = 0;
    uint32_t next = 0;
    uint8_t retries = 0;
    bool active = false;
    bool sent = false;
};

struct FinalAck {
    TransferIdentity identity{};
    Protocol::ReceiveAck ack{};
    AckState state = AckState::Rejected;
    RejectCode error = RejectCode::None;
    uint64_t revision = 0;
    bool valid = false;
};

struct Wire {
    Domain::StateStore store;
    Domain::StateQueryResult bookmark_query{};
    Protocol::ReceiveWindow window;
    Protocol::ReplayWindow replay;
    ControlJson json;
    std::array<uint8_t, kMacPrefixSize + Protocol::kDatagramBytes> buffer{};
    std::array<char, Protocol::kControlDocumentBytes> control{};
    Outgoing outgoing;
    Incoming incoming;
    FinalAck final_ack;
    Domain::StateRequest request{};
    Domain::CapacityLimits selected{};
    std::array<char, 65> provider{};
    std::array<char, 129> native_namespace{};
    std::array<Domain::Capability, 8> capabilities{};
    std::array<uint8_t, 32> offer_digest{};
    std::array<uint8_t, 32> selection_digest{};
    std::array<uint8_t, 16> find_nonce{};
    std::array<uint8_t, 16> session{};
    std::array<uint8_t, 16> peer{};
    std::array<uint8_t, 16> probe{};
    std::array<uint8_t, 6> device{};
    sockaddr_in endpoint{};
    in_addr local{};
    psa_hash_operation_t hash = PSA_HASH_OPERATION_INIT;
    mbedtls_svc_key_id_t key = MBEDTLS_SVC_KEY_ID_INIT;
    Domain::SourceId source{};
    Domain::EpochId epoch{};
    uint64_t sequence = 0;
    uint64_t clock_us = 0;
    uint64_t offer_id = 0;
    uint64_t find_at_us = 0;
    uint64_t expires_at_us = 0;
    uint64_t request_at_us = 0;
    uint64_t next_request_at_us = 0;
    uint64_t next_probe_at_us = 0;
    uint64_t probe_deadline_us = 0;
    uint64_t request_counter = 0;
    uint32_t document_bytes = 0;
    uint16_t width = 0;
    const char *resync = "initial";
    Phase phase = Phase::Idle;
    RejectCode protocol_error = RejectCode::None;
    esp_err_t transport_error = ESP_ERR_INVALID_STATE;
    bool key_active = false;
    bool baseline = false;
    bool probe_confirmed = false;
};

static_assert(sizeof(Wire) <= 40 * 1024, "CCP state transport storage exceeds its reviewed bound");
Wire *s_wire = nullptr;
esp_err_t s_allocation_error = ESP_ERR_INVALID_STATE;
uint64_t s_next_allocation_us = 0;

// bytes: Number of bytes for owner-held decode/transport/store storage.
void *allocate_psram(size_t bytes)
{
    return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

// storage: Previously allocated PSRAM storage.
void release_psram(void *storage)
{
    heap_caps_free(storage);
}

// operation: Static non-secret operation name.
// error: Exact PSA error; no key material or packet text is logged.
esp_err_t crypto_error(const char *operation, psa_status_t error)
{
    ESP_LOGE(kTag, "CCP2 %s failed: PSA status %ld", operation, static_cast<long>(error));
    return ESP_FAIL;
}

// bytes: Fixed-size protocol identity.
template <size_t N>
bool nonzero(const std::array<uint8_t, N> &bytes)
{
    return std::any_of(bytes.begin(), bytes.end(), [](uint8_t byte) { return byte != 0; });
}

// bytes: Binary ID or digest.
// output: Exactly two lowercase hexadecimal characters per byte and a terminator.
template <size_t N>
void encode_hex(const std::array<uint8_t, N> &bytes, char (&output)[2 * N + 1])
{
    constexpr char alphabet[] = "0123456789abcdef";
    for (size_t i = 0; i < N; ++i) {
        output[i * 2] = alphabet[bytes[i] >> 4U];
        output[i * 2 + 1] = alphabet[bytes[i] & 15U];
    }
    output[2 * N] = 0;
}

// input: Exact lowercase hexadecimal text with no separators.
// bytes: Decoded identity or digest on success.
template <size_t N>
bool decode_hex(string_view input, std::array<uint8_t, N> &bytes)
{
    if (input.size() != N * 2U) return false;
    for (size_t i = 0; i < N; ++i) {
        unsigned value = 0;
        for (size_t j = 0; j < 2; ++j) {
            const char digit = input[i * 2 + j];
            unsigned nibble;
            if (digit >= '0' && digit <= '9') nibble = digit - '0';
            else if (digit >= 'a' && digit <= 'f') nibble = digit - 'a' + 10;
            else return false;
            value = (value << 4U) | nibble;
        }
        bytes[i] = static_cast<uint8_t>(value);
    }
    return true;
}

// output: Destination of size bytes.
// size: Network-order integer width, up to eight bytes.
// value: Value fitting exactly that width.
void write_be(uint8_t *output, size_t size, uint64_t value)
{
    for (size_t i = size; i != 0; --i) {
        output[i - 1] = static_cast<uint8_t>(value);
        value >>= 8U;
    }
}

// left: Expected transport identity.
// right: Incoming transport identity authenticated by this session.
bool same_identity(const TransferIdentity &left, const TransferIdentity &right)
{
    return left.transfer == right.transfer && left.source == right.source && left.epoch == right.epoch &&
        left.revision == right.revision && left.document_bytes == right.document_bytes &&
        left.chunks == right.chunks && left.digest == right.digest;
}

// socket: CCP worker-owned UDP socket.
// bytes: Exact outgoing packet bytes, including binary NULs when applicable.
// size: Length of that packet.
// destination: Current socket-observed endpoint, or broadcast for discovery.
esp_err_t send_bytes(int socket, const void *bytes, size_t size, const sockaddr_in &destination)
{
    const int result = sendto(socket, bytes, size, 0,
        reinterpret_cast<const sockaddr *>(&destination), sizeof(destination));
    if (result == static_cast<int>(size)) return ESP_OK;
    ESP_LOGE(kTag, "CCP2 UDP send failed: errno=%d", errno);
    return ESP_FAIL;
}

// wire: Receiver owner after transfer completion, error or cancellation.
void reset_incoming(Wire &wire)
{
    const auto error = psa_hash_abort(&wire.hash);
    if (error != PSA_SUCCESS) wire.transport_error = crypto_error("hash abort", error);
    wire.window.reset();
    wire.store.cancel();
    wire.incoming = {};
}

// wire: Link owner, retains the store while dropping the authenticated session.
void reset_session(Wire &wire)
{
    reset_incoming(wire);
    if (wire.key_active) {
        const auto status = psa_destroy_key(wire.key);
        if (status != PSA_SUCCESS) wire.transport_error = crypto_error("key destroy", status);
    }
    wire.key = MBEDTLS_SVC_KEY_ID_INIT;
    wire.key_active = false;
    wire.baseline = false;
    wire.probe_confirmed = false;
    wire.outgoing.active = false;
    wire.replay = {};
    wire.final_ack = {};
    wire.endpoint = {};
    wire.request = {};
    wire.source = {};
    wire.epoch = {};
    wire.sequence = 0;
    wire.offer_id = 0;
    wire.request_counter = 0;
    wire.probe_deadline_us = 0;
    wire.next_probe_at_us = 0;
    wire.expires_at_us = 0;
    wire.phase = Phase::Idle;
    wire.resync = "initial";
}

// wire: Idle receiver owner.
// key: Already persisted active link key, borrowed only for import.
esp_err_t import_key(Wire &wire, const uint8_t *key)
{
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_HMAC);
    psa_set_key_bits(&attributes, 256);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_MESSAGE | PSA_KEY_USAGE_VERIFY_MESSAGE);
    psa_set_key_algorithm(&attributes, PSA_ALG_HMAC(PSA_ALG_SHA_256));
    const auto status = psa_import_key(&attributes, key, 32, &wire.key);
    psa_reset_key_attributes(&attributes);
    wire.key_active = status == PSA_SUCCESS;
    return wire.key_active ? ESP_OK : crypto_error("key import", status);
}

// wire: Owner of the imported active key.
// input: Exact authenticated bytes including the domain prefix.
// length: Byte length of the authenticated input.
// tag: Thirty-two byte SHA-256 MAC destination.
esp_err_t sign(Wire &wire, const uint8_t *input, size_t length, uint8_t *tag)
{
    size_t tag_length = 0;
    const auto status = psa_mac_compute(wire.key, PSA_ALG_HMAC(PSA_ALG_SHA_256),
        input, length, tag, Protocol::kAuthenticationBytes, &tag_length);
    if (status != PSA_SUCCESS) return crypto_error("MAC compute", status);
    return tag_length == Protocol::kAuthenticationBytes ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

// wire: Owner of the imported active key.
// input: Exact authenticated bytes including the domain prefix.
// length: Byte length of the authenticated input.
// tag: Received authentication tag verified by the PSA constant-time primitive.
esp_err_t verify(Wire &wire, const uint8_t *input, size_t length, const uint8_t *tag)
{
    const auto status = psa_mac_verify(wire.key, PSA_ALG_HMAC(PSA_ALG_SHA_256),
        input, length, tag, Protocol::kAuthenticationBytes);
    if (status == PSA_ERROR_INVALID_SIGNATURE) return ESP_ERR_INVALID_CRC;
    return status == PSA_SUCCESS ? ESP_OK : crypto_error("MAC verify", status);
}

// socket: Bound CCP socket.
// wire: Owner of endpoint, session, packet sequence and bounded crypto scratch.
// kind: Binary packet type.
// identity: Transfer metadata, or probe nonce with every other field zero.
// index: DATA index or first missing ACK chunk.
// payload: Contiguous outgoing payload, null when size is zero.
// size: Exact number of outgoing payload bytes.
esp_err_t send_packet(int socket, Wire &wire, PacketKind kind, const TransferIdentity &identity,
                      uint32_t index, const uint8_t *payload, uint16_t size)
{
    if (wire.sequence == UINT64_MAX || size > Protocol::kChunkBytes) return ESP_ERR_INVALID_STATE;
    auto *input = wire.buffer.data();
    std::memcpy(input, kMacDomain, sizeof(kMacDomain));
    input[sizeof(kMacDomain)] = 0;
    auto *header = input + kMacPrefixSize;
    std::fill_n(header, Protocol::kHeaderBytes, uint8_t{0});
    std::memcpy(header, "CDS1", 4);
    header[4] = Protocol::kWireVersion;
    header[5] = static_cast<uint8_t>(kind);
    std::copy(wire.device.begin(), wire.device.end(), header + 8);
    std::copy(wire.peer.begin(), wire.peer.end(), header + 14);
    std::copy(wire.session.begin(), wire.session.end(), header + 30);
    write_be(header + 46, 8, ++wire.sequence);
    std::copy(identity.transfer.begin(), identity.transfer.end(), header + 54);
    write_be(header + 70, 8, identity.source.value);
    write_be(header + 78, 8, identity.epoch.value);
    write_be(header + 86, 8, identity.revision);
    write_be(header + 94, 4, index);
    write_be(header + 98, 4, identity.chunks);
    write_be(header + 102, 4, identity.document_bytes);
    write_be(header + 106, 2, size);
    std::copy(identity.digest.begin(), identity.digest.end(), header + 108);
    if (size != 0) std::memcpy(header + Protocol::kHeaderBytes, payload, size);
    const size_t signed_length = Protocol::kHeaderBytes + size;
    const auto error = sign(wire, input, kMacPrefixSize + signed_length, header + signed_length);
    return error == ESP_OK
        ? send_bytes(socket, header, signed_length + Protocol::kAuthenticationBytes, wire.endpoint)
        : error;
}

// socket: Bound CCP socket.
// wire: Owner of the current receiving session.
// ack: Authenticated transfer ACK, frozen when final.
esp_err_t send_ack(int socket, Wire &wire, const FinalAck &ack)
{
    uint8_t payload[Protocol::kAckPayloadBytes]{};
    write_be(payload, 2, ack.ack.received_bitmap);
    payload[2] = static_cast<uint8_t>(ack.state);
    payload[3] = static_cast<uint8_t>(ack.error);
    write_be(payload + 4, 8, ack.revision);
    return send_packet(socket, wire, PacketKind::Ack, ack.identity, ack.ack.next_missing,
        payload, sizeof(payload));
}

// socket: Bound CCP socket.
// wire: Active retained association and bootstrap nonce owner.
// now_us: Receiver monotonic time of FIND.
esp_err_t send_find(int socket, Wire &wire, uint64_t now_us)
{
    wire.find_at_us = now_us + kBootstrapTimeoutUs;
    esp_fill_random(wire.find_nonce.data(), wire.find_nonce.size());
    if (!nonzero(wire.find_nonce)) return ESP_ERR_INVALID_STATE;
    char device[13]{};
    char peer[33]{};
    char nonce[33]{};
    encode_hex(wire.device, device);
    encode_hex(wire.peer, peer);
    encode_hex(wire.find_nonce, nonce);
    char input[112]{};
    const int size = std::snprintf(input, sizeof(input), "find-state-v1|%s|%s|%s|1", device, peer, nonce);
    if (size <= 0 || static_cast<size_t>(size) >= sizeof(input)) return ESP_ERR_INVALID_SIZE;
    std::array<uint8_t, 32> tag{};
    auto error = sign(wire, reinterpret_cast<const uint8_t *>(input), size, tag.data());
    if (error != ESP_OK) return error;
    char proof[65]{};
    encode_hex(tag, proof);
    char packet[256]{};
    const int length = std::snprintf(packet, sizeof(packet),
        "CCP2|FIND|device=%s|pc=%s|nonce=%s|state=1|proof=%s", device, peer, nonce, proof);
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(packet)) return ESP_ERR_INVALID_SIZE;
    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port = htons(kPeerPort);
    target.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    error = send_bytes(socket, packet, length, target);
    if (error == ESP_OK) wire.phase = Phase::Finding;
    return error;
}

// output: Destination of exactly 32 bytes.
// bytes: Frozen complete control document.
// length: Byte count excluding any terminator.
esp_err_t digest(std::array<uint8_t, 32> &output, const char *bytes, size_t length)
{
    size_t written = 0;
    const auto status = psa_hash_compute(PSA_ALG_SHA_256,
        reinterpret_cast<const uint8_t *>(bytes), length, output.data(), output.size(), &written);
    if (status != PSA_SUCCESS) return crypto_error("control hash", status);
    return written == output.size() ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

// wire: Owner of the one outgoing control transfer.
// length: Count of immutable serialized bytes already written in outgoing.bytes.
esp_err_t queue_control(Wire &wire, size_t length)
{
    if (length == 0 || length > wire.outgoing.bytes.size()) return ESP_ERR_INVALID_SIZE;
    auto &outgoing = wire.outgoing;
    outgoing.identity = {};
    esp_fill_random(outgoing.identity.transfer.data(), outgoing.identity.transfer.size());
    if (!nonzero(outgoing.identity.transfer)) return ESP_ERR_INVALID_STATE;
    outgoing.identity.document_bytes = static_cast<uint32_t>(length);
    outgoing.identity.chunks = Protocol::chunk_count(outgoing.identity.document_bytes);
    const auto error = digest(outgoing.identity.digest, outgoing.bytes.data(), length);
    if (error != ESP_OK) return error;
    outgoing.next = 0;
    outgoing.sent = false;
    outgoing.active = true;
    outgoing.retries = 0;
    return ESP_OK;
}

// socket: Bound CCP socket.
// wire: Owner of the one outgoing control transfer.
// now_us: Monotonic retry clock. Every retry uses a new packet sequence.
esp_err_t tick_outgoing(int socket, Wire &wire, uint64_t now_us)
{
    auto &outgoing = wire.outgoing;
    if (!outgoing.active) return ESP_OK;
    if (outgoing.sent && now_us - outgoing.sent_at_us < Protocol::kRetransmitMs * 1000ULL) return ESP_OK;
    if (outgoing.sent && outgoing.retries == Protocol::kRetryLimit) return ESP_ERR_TIMEOUT;
    const auto index = std::min(outgoing.next, outgoing.identity.chunks - 1);
    const auto offset = index * uint32_t{Protocol::kChunkBytes};
    const auto length = std::min(uint32_t{Protocol::kChunkBytes}, outgoing.identity.document_bytes - offset);
    const auto error = send_packet(socket, wire, PacketKind::Data, outgoing.identity, index,
        reinterpret_cast<const uint8_t *>(outgoing.bytes.data() + offset), static_cast<uint16_t>(length));
    if (outgoing.sent) ++outgoing.retries;
    outgoing.sent = true;
    outgoing.sent_at_us = now_us;
    return error;
}

// json: Parsed negotiation document.
// transport: Exact limits object from offer or accepted.
// document: Maximum complete state-document byte count.
// width: Selective-repeat window in chunks.
bool read_transport(const ControlJson &json, const JsonNode *transport, uint32_t &document, uint16_t &width)
{
    if (!json.exact(transport, {"document_bytes", "window_chunks"})) return false;
    const auto *bytes = json.get(transport, "document_bytes");
    const auto *window = json.get(transport, "window_chunks");
    if (bytes->kind != ControlJson::Kind::Number || bytes->number < Protocol::kChunkBytes ||
        bytes->number > Protocol::kDocumentBytes || window->kind != ControlJson::Kind::Number ||
        window->number == 0 || window->number > Protocol::kReceiveWindow) return false;
    document = bytes->number;
    width = static_cast<uint16_t>(window->number);
    return true;
}

struct Binding {
    Domain::SourceId source{};
    Domain::EpochId epoch{};
    string_view provider{};
    string_view native_namespace{};
    std::array<Domain::Capability, 8> capabilities{};
    bool supports_core = false;
};

// json: Parsed broker offer.
// node: Candidate source binding.
// binding: Decoded identity and capability information, valid with the parsed offer.
bool read_binding(const ControlJson &json, const JsonNode *node, Binding &binding)
{
    if (!json.exact(node, {"source_id", "epoch", "provider", "native_namespace", "capabilities"})) {
        return false;
    }
    const auto *provider = json.get(node, "provider");
    const auto *native_namespace = json.get(node, "native_namespace");
    const auto *caps = json.get(node, "capabilities");
    if (!hex_u64(json.get(node, "source_id"), binding.source.value) || !binding.source.valid() ||
        !hex_u64(json.get(node, "epoch"), binding.epoch.value) || !binding.epoch.valid() ||
        !string_range(provider, 1, 64) || !string_range(native_namespace, 1, 128) ||
        !json.exact(caps, {"chats", "turns", "tool_calls", "agents", "external_operations",
                           "questions", "recipient_acknowledgement", "resume_questions"})) {
        return false;
    }
    size_t index = 0;
    for (const auto name : {"chats", "turns", "tool_calls", "agents", "external_operations",
                            "questions", "recipient_acknowledgement", "resume_questions"}) {
        const auto *capability = json.get(caps, name);
        auto &value = binding.capabilities[index++];
        if (string_is(capability, "supported")) value = Domain::Capability::Supported;
        else if (string_is(capability, "unsupported")) value = Domain::Capability::Unsupported;
        else if (!string_is(capability, "unknown")) return false;
    }
    binding.provider = provider->text;
    binding.native_namespace = native_namespace->text;
    binding.supports_core = std::all_of(binding.capabilities.begin(), binding.capabilities.begin() + 3,
        [](auto value) { return value == Domain::Capability::Supported; });
    return true;
}

// wire: Selection owner with the exact verified offer digest and chosen limits.
esp_err_t queue_selection(Wire &wire)
{
    auto &output = wire.outgoing.bytes;
    char digest_hex[65]{};
    encode_hex(wire.offer_digest, digest_hex);
    const int prefix = std::snprintf(output.data(), output.size(),
        "{\"schema\":\"cody.state/1\",\"kind\":\"select\",\"request_id\":\"%016" PRIx64
        "\",\"offer_digest\":\"%s\",\"sources\":[\"%016" PRIx64 "\"],\"limits\":{",
        wire.offer_id, digest_hex, wire.source.value);
    if (prefix <= 0 || static_cast<size_t>(prefix) >= output.size()) return ESP_ERR_INVALID_SIZE;
    size_t used = static_cast<size_t>(prefix);
    for (const auto &field : kLimitFields) {
        const int written = std::snprintf(output.data() + used, output.size() - used,
            "%s\"%s\":%" PRIu32, &field == &kLimitFields[0] ? "" : ",",
            field.name, wire.selected.*field.value);
        if (written <= 0 || static_cast<size_t>(written) >= output.size() - used) return ESP_ERR_INVALID_SIZE;
        used += static_cast<size_t>(written);
    }
    const int suffix = std::snprintf(output.data() + used, output.size() - used,
        "},\"transport\":{\"document_bytes\":%" PRIu32 ",\"window_chunks\":%u}}",
        wire.document_bytes, static_cast<unsigned>(wire.width));
    if (suffix <= 0 || static_cast<size_t>(suffix) >= output.size() - used) return ESP_ERR_INVALID_SIZE;
    const auto error = queue_control(wire, used + static_cast<size_t>(suffix));
    if (error == ESP_OK) wire.selection_digest = wire.outgoing.identity.digest;
    return error;
}

// wire: Negotiating state session; PSRAM-backed store is provisioned before select.
// document: Parsed authenticated offer.
// request_id: Broker-created negotiation ID reused only for this negotiation.
RejectCode admit_offer(Wire &wire, const JsonNode *document, uint64_t request_id)
{
    const auto &json = wire.json;
    if (wire.phase != Phase::Offer || !json.exact(document,
        {"schema", "kind", "request_id", "bindings", "limits", "transport"})) {
        return RejectCode::Malformed;
    }
    const auto *bindings = json.get(document, "bindings");
    if (bindings->kind != ControlJson::Kind::Array) return RejectCode::Malformed;
    const size_t count = json.children(bindings);
    if (count == 0 || count > 4) return RejectCode::Malformed;
    std::array<Binding, 4> manifest{};
    const Binding *chosen = nullptr;
    for (size_t i = 0; i < count; ++i) {
        auto &binding = manifest[i];
        if (!read_binding(json, json.item(bindings, i), binding)) return RejectCode::Malformed;
        for (size_t j = 0; j < i; ++j) {
            const auto &previous = manifest[j];
            if (previous.source == binding.source || (previous.provider == binding.provider &&
                previous.native_namespace == binding.native_namespace)) return RejectCode::Malformed;
        }
        // This firmware profile admits only the currently supported Codex adapter.
        if (!chosen && binding.supports_core && binding.provider == "codex" &&
            binding.native_namespace == "codex.command-hooks") chosen = &binding;
    }
    if (!chosen) return RejectCode::UnsupportedCapability;

    CapacityLimits offered{};
    uint32_t document_bytes = 0;
    uint16_t width = 0;
    if (!read_limits(json, json.get(document, "limits"), offered) ||
        !read_transport(json, json.get(document, "transport"), document_bytes, width)) return RejectCode::Malformed;

    CapacityLimits local{};
    local.sources = 1;
    local.evidence = 1024;
    local.tombstones = 128;
    local.agents = local.operations = 0;
    local.open_questions = local.resolved_questions = local.choices = local.answer_attempts = local.waits = 0;
    local.persistence_scratch_bytes = 0;
    for (const auto &field : kLimitFields) {
        wire.selected.*field.value = std::min(local.*field.value, offered.*field.value);
    }
    const auto &limits = wire.selected;
    if (limits.sources != 1 || limits.chats != 64 || limits.turns == 0 ||
        limits.tool_definitions == 0 || limits.active_calls == 0 || limits.evidence == 0 ||
        limits.text_bytes == 0 || limits.decode_scratch_bytes < kDecodeScratchBytes) {
        return RejectCode::UnsupportedCapability;
    }
    const auto provisioned = wire.store.configure(limits, allocate_psram, release_psram);
    if (provisioned != RejectCode::None) return provisioned;

    wire.source = chosen->source;
    wire.epoch = chosen->epoch;
    wire.provider.fill(0);
    wire.native_namespace.fill(0);
    std::copy(chosen->provider.begin(), chosen->provider.end(), wire.provider.begin());
    std::copy(chosen->native_namespace.begin(), chosen->native_namespace.end(), wire.native_namespace.begin());
    wire.capabilities = chosen->capabilities;
    wire.document_bytes = std::min(document_bytes, kLocalDocumentBytes);
    wire.width = width;
    wire.offer_id = request_id;
    wire.offer_digest = wire.incoming.identity.digest;
    wire.transport_error = queue_selection(wire);
    if (wire.transport_error != ESP_OK) return RejectCode::Persistence;
    wire.phase = Phase::Accepted;
    return RejectCode::None;
}

// wire: Session awaiting acceptance of its exact selected parameters.
// document: Verified parsed acceptance control.
// request_id: Echoed original negotiation ID.
// now_us: Receiver monotonic clock at semantic admission.
RejectCode admit_accepted(Wire &wire, const JsonNode *document, uint64_t request_id, uint64_t now_us)
{
    const auto &json = wire.json;
    if (wire.phase != Phase::Accepted || request_id != wire.offer_id || !json.exact(document,
        {"schema", "kind", "request_id", "offer_digest", "selection_digest", "sources",
         "limits", "transport"})) return RejectCode::WrongBinding;
    const auto *sources = json.get(document, "sources");
    uint64_t source = 0;
    char offer[65]{};
    char selection[65]{};
    encode_hex(wire.offer_digest, offer);
    encode_hex(wire.selection_digest, selection);
    CapacityLimits limits{};
    uint32_t document_bytes = 0;
    uint16_t width = 0;
    if (!string_is(json.get(document, "offer_digest"), offer) ||
        !string_is(json.get(document, "selection_digest"), selection) ||
        sources->kind != ControlJson::Kind::Array || json.children(sources) != 1 ||
        !hex_u64(json.item(sources, 0), source) || source != wire.source.value ||
        !read_limits(json, json.get(document, "limits"), limits) || !same_limits(wire.selected, limits) ||
        !read_transport(json, json.get(document, "transport"), document_bytes, width) ||
        document_bytes != wire.document_bytes || width != wire.width) return RejectCode::WrongBinding;

    // Acceptance is sufficient proof that the selection arrived even when
    // its transport ACK was lost. Do not renew evidence on negotiation alone.
    wire.outgoing.active = false;
    wire.phase = Phase::Live;
    wire.next_request_at_us = now_us;
    wire.next_probe_at_us = now_us;
    const auto previous = wire.store.summary();
    wire.resync = previous.revision != 0 && previous.epoch != wire.epoch ? "epoch_changed" : "initial";
    return RejectCode::None;
}

// name: Wire name of a protocol rejection, mapped without trusting numeric input.
RejectCode read_error_code(const JsonNode *name)
{
    constexpr string_view names[] = {
        "malformed", "unsupported_version", "unsupported_capability", "wrong_binding",
        "revision_conflict", "need_snapshot", "missing_reference", "capacity_exceeded",
        "transfer_timeout", "integrity", "busy", "persistence", "invalid_transition",
    };
    for (size_t i = 0; i < std::size(names); ++i) {
        if (string_is(name, names[i])) return static_cast<RejectCode>(i + 1);
    }
    return RejectCode::None;
}

// wire: Active source session.
// now_us: Receiver monotonic clock for request backoff.
// error: Terminal request result, or None for coherent commit/no-change.
void finish_request(Wire &wire, uint64_t now_us, RejectCode error)
{
    wire.outgoing.active = false;
    wire.request = {};
    wire.protocol_error = error;
    wire.next_request_at_us = std::max(wire.next_request_at_us, now_us + kRequestPeriodUs);
    switch (error) {
    case RejectCode::None: wire.resync = nullptr; break;
    case RejectCode::CapacityExceeded:
        wire.resync = "capacity";
        wire.next_request_at_us = now_us + kCapacityRetryUs;
        break;
    case RejectCode::Integrity: wire.resync = "integrity"; break;
    case RejectCode::NeedSnapshot: wire.resync = "gap"; break;
    case RejectCode::Busy: break;
    default: wire.resync = "reconciliation"; break;
    }
}

// wire: Owner of verified complete control bytes and the pending request.
// now_us: Receiver monotonic time at admission.
RejectCode admit_control(Wire &wire, uint64_t now_us)
{
    const auto *document = wire.json.parse(wire.control.data(), wire.incoming.control_bytes);
    const auto &json = wire.json;
    uint64_t request_id = 0;
    if (!string_is(json.get(document, "schema"), "cody.state/1") ||
        !hex_u64(json.get(document, "request_id"), request_id) || request_id == 0) {
        return RejectCode::Malformed;
    }
    const auto *kind = json.get(document, "kind");
    if (string_is(kind, "offer")) return admit_offer(wire, document, request_id);
    if (string_is(kind, "accepted")) return admit_accepted(wire, document, request_id, now_us);
    if (wire.phase != Phase::Live || wire.request.request_id != request_id) return RejectCode::WrongBinding;

    if (string_is(kind, "no_change")) {
        uint64_t source = 0;
        uint64_t epoch = 0;
        uint64_t revision = 0;
        if (wire.request.snapshot || !json.exact(document, {"schema", "kind", "request_id", "source_id", "epoch", "revision"}) ||
            !hex_u64(json.get(document, "source_id"), source) || source != wire.source.value ||
            !hex_u64(json.get(document, "epoch"), epoch) || epoch != wire.epoch.value ||
            !hex_u64(json.get(document, "revision"), revision) || revision != wire.request.base_revision) {
            return RejectCode::WrongBinding;
        }
        finish_request(wire, now_us, RejectCode::None);
        return RejectCode::None;
    }
    if (string_is(kind, "error") && json.exact(document, {"schema", "kind", "request_id", "code", "pool", "detail", "revision"})) {
        const auto error = read_error_code(json.get(document, "code"));
        const auto *pool = json.get(document, "pool");
        const auto *revision = json.get(document, "revision");
        uint64_t revision_value = 0;
        if (error == RejectCode::None || (pool->kind != ControlJson::Kind::Null && !string_range(pool, 1, 64)) ||
            (revision->kind != ControlJson::Kind::Null && !hex_u64(revision, revision_value)) ||
            !string_range(json.get(document, "detail"), 0, 512)) return RejectCode::Malformed;
        finish_request(wire, now_us, error);
        return RejectCode::None;
    }
    return RejectCode::Malformed;
}

// wire: Live session with no outstanding state request.
// now_us: Receiver monotonic time associated with the original request send.
esp_err_t queue_request(Wire &wire, uint64_t now_us)
{
    if (wire.request_counter >= UINT64_MAX - 1) return ESP_ERR_INVALID_STATE;
    ++wire.request_counter;
    if (wire.request_counter == wire.offer_id) ++wire.request_counter;
    const auto base = wire.store.revision(wire.source, wire.epoch);
    const bool snapshot = wire.resync != nullptr || base == 0;
    const auto id = wire.request_counter;
    auto &bytes = wire.outgoing.bytes;
    const int length = snapshot
        ? std::snprintf(bytes.data(), bytes.size(),
            "{\"schema\":\"cody.state/1\",\"kind\":\"resync\",\"request_id\":\"%016" PRIx64
            "\",\"source_id\":\"%016" PRIx64 "\",\"epoch\":\"%016" PRIx64
            "\",\"revision\":\"%016" PRIx64 "\",\"reason\":\"%s\"}",
            id, wire.source.value, wire.epoch.value, base, wire.resync ? wire.resync : "initial")
        : std::snprintf(bytes.data(), bytes.size(),
            "{\"schema\":\"cody.state/1\",\"kind\":\"next\",\"request_id\":\"%016" PRIx64
            "\",\"source_id\":\"%016" PRIx64 "\",\"epoch\":\"%016" PRIx64
            "\",\"base_revision\":\"%016" PRIx64 "\"}",
            id, wire.source.value, wire.epoch.value, base);
    if (length <= 0 || static_cast<size_t>(length) >= bytes.size()) return ESP_ERR_INVALID_SIZE;
    const auto error = queue_control(wire, static_cast<size_t>(length));
    if (error != ESP_OK) return error;
    wire.request = {wire.source, wire.epoch, id, base, snapshot,
        wire.provider, wire.native_namespace, wire.capabilities};
    wire.request_at_us = now_us;
    wire.next_request_at_us = now_us + kRequestPeriodUs;
    return ESP_OK;
}

// context: Serialized Wire owner passed by the portable receive window.
// bytes: Contiguous verified chunk borrowed until this call returns.
// length: Exact number of bytes in this chunk.
RejectCode consume_chunk(void *context, const uint8_t *bytes, uint16_t length)
{
    auto &wire = *static_cast<Wire *>(context);
    const auto status = psa_hash_update(&wire.hash, bytes, length);
    if (status != PSA_SUCCESS) {
        wire.transport_error = crypto_error("hash update", status);
        return RejectCode::Integrity;
    }
    if (!wire.incoming.control) return wire.store.consume(bytes, length);
    auto &used = wire.incoming.control_bytes;
    if (length > wire.control.size() - used) return RejectCode::CapacityExceeded;
    std::memcpy(wire.control.data() + used, bytes, length);
    used += length;
    return RejectCode::None;
}

// wire: Serialized receive owner with no current transfer.
// identity: Authenticated data transfer header.
// now_us: Receiver monotonic admission time.
RejectCode begin_incoming(Wire &wire, const TransferIdentity &identity, uint64_t now_us)
{
    const bool control = !identity.source.valid();
    const bool negotiating = wire.phase == Phase::Offer || wire.phase == Phase::Accepted;
    if (!negotiating && wire.request.request_id == 0) return RejectCode::WrongBinding;
    if (!control && (wire.phase != Phase::Live || identity.source != wire.source ||
                     identity.epoch != wire.epoch ||
                     (!wire.request.snapshot && identity.revision <= wire.request.base_revision))) {
        return RejectCode::WrongBinding;
    }

    const auto request_time = negotiating ? now_us : wire.request_at_us;
    auto error = wire.window.begin(identity, control ? 1 : wire.width,
        control ? Protocol::kControlDocumentBytes : wire.document_bytes, request_time, now_us);
    if (error != RejectCode::None) return error;
    if (!control) {
        error = wire.store.begin(wire.request);
        if (error != RejectCode::None) {
            reset_incoming(wire);
            return error;
        }
    }
    const auto status = psa_hash_setup(&wire.hash, PSA_ALG_SHA_256);
    if (status != PSA_SUCCESS) {
        wire.transport_error = crypto_error("hash setup", status);
        reset_incoming(wire);
        return RejectCode::Integrity;
    }
    wire.incoming = {identity, true, control, 0};
    wire.final_ack = {};
    return RejectCode::None;
}

// socket: Bound CCP socket.
// wire: Current state session.
// chunk: One authenticated DATA packet.
// now_us: Receiver monotonic time.
// duplicate_sequence: True if this packet sequence was already admitted.
esp_err_t receive_data(int socket, Wire &wire, const Protocol::DataChunk &chunk, uint64_t now_us,
                       bool duplicate_sequence)
{
    const auto &identity = chunk.identity;
    if (wire.final_ack.valid && wire.final_ack.identity.transfer == identity.transfer) {
        if (same_identity(wire.final_ack.identity, identity)) return send_ack(socket, wire, wire.final_ack);
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (duplicate_sequence) {
        if (wire.incoming.running && same_identity(wire.incoming.identity, identity)) {
            const auto revision = identity.source.valid() ? wire.store.revision(identity.source, identity.epoch) : 0;
            return send_ack(socket, wire, {identity, wire.window.acknowledgement(), AckState::Receiving,
                RejectCode::None, revision, false});
        }
        return ESP_OK;
    }
    const auto revision = identity.source.valid() ? wire.store.revision(identity.source, identity.epoch) : 0;
    if (!wire.incoming.running) {
        const auto error = begin_incoming(wire, identity, now_us);
        if (error != RejectCode::None) {
            if (error != RejectCode::WrongBinding && wire.request.request_id != 0) {
                finish_request(wire, now_us, error);
            }
            wire.protocol_error = error;
            return send_ack(socket, wire, {identity, {}, AckState::Rejected, error, revision, false});
        }
    }

    const auto result = wire.window.accept(chunk, now_us, consume_chunk, &wire);
    if (result.disposition == Protocol::ChunkDisposition::Rejected && result.error == RejectCode::Busy) {
        return send_ack(socket, wire, {identity, {}, AckState::Rejected, result.error, revision, false});
    }

    auto ack = wire.window.acknowledgement();
    if (result.disposition != Protocol::ChunkDisposition::Complete &&
        result.disposition != Protocol::ChunkDisposition::Rejected) {
        return send_ack(socket, wire, {identity, ack, AckState::Receiving, RejectCode::None, revision, false});
    }

    auto error = result.error;
    uint64_t committed = revision;
    if (error == RejectCode::None) {
        const auto digest_status = psa_hash_verify(&wire.hash, identity.digest.data(), identity.digest.size());
        if (digest_status != PSA_SUCCESS) {
            if (digest_status != PSA_ERROR_INVALID_SIGNATURE) {
                wire.transport_error = crypto_error("hash verify", digest_status);
            }
            error = RejectCode::Integrity;
        } else if (wire.incoming.control) {
            error = admit_control(wire, now_us);
        } else {
            error = wire.store.commit(identity, wire.request_at_us, now_us);
            if (error == RejectCode::None) {
                committed = wire.store.revision(identity.source, identity.epoch);
                if (committed != identity.revision) {
                    // An ACK may only identify the actual generation.
                    error = RejectCode::RevisionConflict;
                } else {
                    wire.baseline = true;
                    codex_state_saved::remember(wire.store.summary(), now_us);
                    finish_request(wire, now_us, RejectCode::None);
                }
            }
        }
    }

    if (error != RejectCode::None) {
        wire.protocol_error = error;
        // A delayed response bound to another request cannot consume this one.
        if (error != RejectCode::WrongBinding && wire.request.request_id != 0) {
            finish_request(wire, now_us, error);
        }
        // A rejected digest/decoder result may have consumed all bytes, but
        // never marks a generation committed. The peer receives the real error.
    }
    wire.final_ack = {identity, ack, error == RejectCode::None ? AckState::Committed : AckState::Rejected,
        error, committed, true};
    reset_incoming(wire);
    return send_ack(socket, wire, wire.final_ack);
}

// wire: Current outgoing control owner.
// packet: Parsed authenticated ACK.
// now_us: Receiver monotonic time.
esp_err_t receive_ack(Wire &wire, const Protocol::DatagramView &packet, uint64_t now_us)
{
    auto &outgoing = wire.outgoing;
    if (!outgoing.active || !same_identity(outgoing.identity, packet.chunk.identity)) return ESP_OK;
    Protocol::AckPayload ack{};
    if (Protocol::decode_ack(packet, 1, ack) != RejectCode::None ||
        !outgoing.sent || packet.chunk.index > outgoing.next + 1) return ESP_ERR_INVALID_RESPONSE;
    if (ack.state == AckState::Rejected) {
        wire.protocol_error = ack.error;
        outgoing.active = false;
        if (wire.phase == Phase::Live) finish_request(wire, now_us, ack.error);
        else reset_session(wire);
    } else if (ack.state == AckState::Committed) {
        outgoing.active = false;
    } else if (packet.chunk.index > outgoing.next) {
        outgoing.next = packet.chunk.index;
        if (outgoing.next < outgoing.identity.chunks) {
            outgoing.retries = 0;
            outgoing.sent = false;
        }
    }
    return ESP_OK;
}

// socket: Bound CCP socket.
// wire: Authenticated link owner.
// now_us: Receiver monotonic probe clock.
esp_err_t tick_probe(int socket, Wire &wire, uint64_t now_us)
{
    if (now_us < wire.next_probe_at_us) return ESP_OK;
    esp_fill_random(wire.probe.data(), wire.probe.size());
    if (!nonzero(wire.probe)) return ESP_ERR_INVALID_STATE;
    TransferIdentity identity{};
    identity.transfer = wire.probe;
    wire.next_probe_at_us = now_us + Protocol::kProbeIntervalMs * 1000ULL;
    wire.probe_deadline_us = now_us + Protocol::kProbeTimeoutMs * 1000ULL;
    return send_packet(socket, wire, PacketKind::Ping, identity, 0, nullptr, 0);
}

// wire: State receiver owner for the current canonical device/peer association.
// now_us: Nonnegative clock value for this worker slice.
void expire_transfers(Wire &wire, uint64_t now_us)
{
    if (wire.incoming.running && wire.window.check_deadline(now_us) != RejectCode::None) {
        const auto &identity = wire.incoming.identity;
        const auto revision = identity.source.valid() ? wire.store.revision(identity.source, identity.epoch) : 0;
        wire.final_ack = {identity, {}, AckState::Rejected, RejectCode::TransferTimeout, revision, true};
        reset_incoming(wire);
        wire.protocol_error = RejectCode::TransferTimeout;
        if (wire.request.request_id != 0) finish_request(wire, now_us, RejectCode::TransferTimeout);
    }
    if (wire.request.request_id != 0 && now_us - wire.request_at_us >= Protocol::kTransferTimeoutMs * 1000ULL) {
        reset_incoming(wire);
        finish_request(wire, now_us, RejectCode::TransferTimeout);
    }
}

} // namespace

// socket: Bound CCP UDP descriptor.
// device: Canonical twelve-character lowercase station ID.
// peer: Exact active retained PC ID.
// key: Already persisted and read back active link key.
// local: Canonical local IPv4 address.
// now_us: Nonnegative receiver monotonic time.
void maintain(int socket, const char *device, const uint8_t *peer, const uint8_t *key,
              const char *local, uint64_t now_us)
{
    if (!s_wire) {
        if (now_us < s_next_allocation_us) return;
        void *storage = allocate_psram(sizeof(Wire));
        if (!storage) {
            s_allocation_error = ESP_ERR_NO_MEM;
            s_next_allocation_us = now_us + kCapacityRetryUs;
            ESP_LOGE(kTag, "CCP2 state owner needs %u bytes of PSRAM", static_cast<unsigned>(sizeof(Wire)));
            return;
        }
        s_wire = new (storage) Wire;
    }
    auto &wire = *s_wire;
    in_addr current{};
    if (inet_pton(AF_INET, local, &current) != 1) {
        disconnect(false);
        wire.transport_error = ESP_ERR_INVALID_ARG;
        return;
    }
    if (wire.phase != Phase::Idle && (wire.local.s_addr != current.s_addr ||
        !std::equal(wire.peer.begin(), wire.peer.end(), peer) || now_us < wire.clock_us)) {
        reset_session(wire);
        wire.find_at_us = 0;
    }
    wire.clock_us = now_us;
    if (wire.phase == Phase::Finding && now_us >= wire.find_at_us) {
        reset_session(wire);
        wire.transport_error = ESP_ERR_TIMEOUT;
    }
    if (wire.phase >= Phase::Offer &&
        (now_us >= wire.expires_at_us || wire.sequence == UINT64_MAX)) {
        reset_session(wire);
        wire.transport_error = ESP_ERR_TIMEOUT;
    }
    if (wire.phase == Phase::Idle) {
        if (now_us < wire.find_at_us) return;
        wire.local = current;
        if (!decode_hex(string_view(device), wire.device)) {
            wire.transport_error = ESP_ERR_INVALID_ARG;
            wire.find_at_us = now_us + kBootstrapTimeoutUs;
            return;
        }
        std::copy_n(peer, wire.peer.size(), wire.peer.begin());
        auto error = import_key(wire, key);
        if (error == ESP_OK) error = send_find(socket, wire, now_us);
        if (error != ESP_OK) {
            reset_session(wire);
            wire.transport_error = error;
            wire.find_at_us = now_us + kBootstrapTimeoutUs;
        }
        return;
    }
    if (wire.phase < Phase::Offer) return;

    expire_transfers(wire, now_us);
    if (wire.phase == Phase::Live) {
        const auto probe_error = tick_probe(socket, wire, now_us);
        if (probe_error != ESP_OK) wire.transport_error = probe_error;
        if (!wire.outgoing.active && wire.request.request_id == 0 && now_us >= wire.next_request_at_us) {
            const auto error = queue_request(wire, now_us);
            if (error != ESP_OK) {
                wire.transport_error = error;
                wire.next_request_at_us = now_us + kRequestPeriodUs;
            }
        }
    }
    const auto error = tick_outgoing(socket, wire, now_us);
    if (error != ESP_OK) {
        if (error == ESP_ERR_TIMEOUT || error == ESP_ERR_INVALID_STATE) {
            reset_session(wire);
            wire.find_at_us = now_us + kBootstrapTimeoutUs;
        }
        wire.transport_error = error;
    }
}

// socket: Bound CCP UDP descriptor.
// payload: Exact NUL-terminated CCP2 READY.
// sender: Socket-observed source endpoint.
// local: Current canonical station address.
// now_us: Receiver monotonic time.
esp_err_t accept_ready(int socket, const char *payload, const sockaddr_in &sender,
                       const char *local, uint64_t now_us)
{
    if (!s_wire || s_wire->phase != Phase::Finding || now_us >= s_wire->find_at_us ||
        sender.sin_family != AF_INET || sender.sin_port != htons(kPeerPort)) return ESP_ERR_INVALID_STATE;
    auto &wire = *s_wire;
    in_addr local_address{};
    if (inet_pton(AF_INET, local, &local_address) != 1 ||
        wire.local.s_addr != local_address.s_addr) return ESP_ERR_INVALID_STATE;

    char device[13]{};
    char peer[33]{};
    char nonce[33]{};
    char challenge[33]{};
    char address[16]{};
    char proof[65]{};
    int consumed = 0;
    const int fields = std::sscanf(payload,
        "CCP2|READY|device=%12[0-9a-f]|pc=%32[0-9a-f]|nonce=%32[0-9a-f]|challenge=%32[0-9a-f]"
        "|address=%15[0-9.]|port=42117|state=1|proof=%64[0-9a-f]%n",
        device, peer, nonce, challenge, address, proof, &consumed);
    std::array<uint8_t, 6> decoded_device{};
    std::array<uint8_t, 16> decoded_peer{};
    std::array<uint8_t, 16> decoded_nonce{};
    std::array<uint8_t, 16> decoded_session{};
    std::array<uint8_t, 32> tag{};
    if (fields != 6 || consumed != static_cast<int>(std::strlen(payload)) ||
        !decode_hex(device, decoded_device) || decoded_device != wire.device ||
        !decode_hex(peer, decoded_peer) || decoded_peer != wire.peer ||
        !decode_hex(nonce, decoded_nonce) || decoded_nonce != wire.find_nonce ||
        !decode_hex(challenge, decoded_session) || !nonzero(decoded_session) ||
        !decode_hex(proof, tag) || std::strcmp(address, local) != 0) return ESP_ERR_INVALID_RESPONSE;

    char input[176]{};
    int length = std::snprintf(input, sizeof(input), "ready-state-v1|%s|%s|%s|%s|%s|42117|1",
        device, peer, nonce, challenge, address);
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(input)) return ESP_ERR_INVALID_SIZE;
    auto error = verify(wire, reinterpret_cast<const uint8_t *>(input), length, tag.data());
    if (error != ESP_OK) return error;

    length = std::snprintf(input, sizeof(input), "committed-state-v1|%s|%s|%s|%s|%s|42117|1",
        device, peer, nonce, challenge, address);
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(input)) return ESP_ERR_INVALID_SIZE;
    error = sign(wire, reinterpret_cast<const uint8_t *>(input), length, tag.data());
    if (error != ESP_OK) return error;
    encode_hex(tag, proof);
    char packet[288]{};
    length = std::snprintf(packet, sizeof(packet),
        "CCP2|COMMITTED|device=%s|pc=%s|nonce=%s|challenge=%s|address=%s|port=42117|state=1|proof=%s",
        device, peer, nonce, challenge, address, proof);
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(packet)) return ESP_ERR_INVALID_SIZE;
    error = send_bytes(socket, packet, length, sender);
    if (error != ESP_OK) return error;

    wire.endpoint = sender;
    wire.session = decoded_session;
    wire.phase = Phase::Offer;
    wire.expires_at_us = now_us + Protocol::kLinkLeaseMs * 1000ULL;
    return ESP_OK;
}

// socket: Bound CCP UDP descriptor.
// packet: Exact binary datagram after ingress token admission.
// length: Byte length of the original UDP payload.
// sender: Socket-observed broker endpoint.
// now_us: Receiver monotonic clock.
esp_err_t accept_packet(int socket, const uint8_t *packet, size_t length,
                        const sockaddr_in &sender, uint64_t now_us)
{
    if (!s_wire || s_wire->phase < Phase::Offer || now_us >= s_wire->expires_at_us) return ESP_ERR_INVALID_STATE;
    auto &wire = *s_wire;
    if (sender.sin_family != AF_INET || sender.sin_port != wire.endpoint.sin_port ||
        sender.sin_addr.s_addr != wire.endpoint.sin_addr.s_addr || now_us < wire.clock_us) {
        return ESP_ERR_INVALID_STATE;
    }

    Protocol::DatagramView view{};
    if (Protocol::parse_datagram(packet, length, view) != RejectCode::None ||
        view.device != wire.device || view.peer != wire.peer || view.session != wire.session) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    auto *input = wire.buffer.data();
    std::memcpy(input, kMacDomain, sizeof(kMacDomain));
    input[sizeof(kMacDomain)] = 1;
    const auto signed_length = length - Protocol::kAuthenticationBytes;
    std::memcpy(input + kMacPrefixSize, packet, signed_length);
    auto error = verify(wire, input, kMacPrefixSize + signed_length, view.tag);
    if (error != ESP_OK) return error;

    const auto sequence = wire.replay.admit(view.sequence);
    if (sequence == Protocol::SequenceDecision::TooOld || sequence == Protocol::SequenceDecision::Invalid) {
        return ESP_ERR_INVALID_STATE;
    }
    if (view.kind == PacketKind::Data) {
        return receive_data(socket, wire, view.chunk, now_us, sequence == Protocol::SequenceDecision::Duplicate);
    }
    if (sequence == Protocol::SequenceDecision::Duplicate) return ESP_OK;
    if (view.kind == PacketKind::Ack) return receive_ack(wire, view, now_us);
    if (view.kind == PacketKind::Ping) {
        return send_packet(socket, wire, PacketKind::Pong, view.chunk.identity, 0, nullptr, 0);
    }
    if (view.kind == PacketKind::Pong && wire.probe_deadline_us != 0 &&
        now_us < wire.probe_deadline_us && view.chunk.identity.transfer == wire.probe) {
        wire.probe_deadline_us = 0;
        wire.expires_at_us = now_us + Protocol::kLinkLeaseMs * 1000ULL;
        wire.probe_confirmed = true;
        wire.transport_error = ESP_OK;
    }
    return ESP_OK;
}

// clear_generation: Explicit Forget or replacement of the persisted association.
void disconnect(bool clear_generation)
{
    if (!s_wire) return;
    auto &wire = *s_wire;
    if (wire.phase != Phase::Idle) reset_session(wire);
    wire.find_at_us = 0;
    wire.transport_error = ESP_ERR_INVALID_STATE;
    if (clear_generation) wire.store.clear();
}

namespace {

// wire: Current serialized state session.
// summary: Coherent generation from this exact store.
// now_us: Receiver clock used to check the peer lease.
bool link_ready(const Wire &wire, const Domain::StateSummary &summary, uint64_t now_us)
{
    return wire.phase == Phase::Live && wire.baseline && wire.probe_confirmed &&
        wire.resync == nullptr && now_us >= wire.clock_us && now_us < wire.expires_at_us &&
        summary.source == wire.source && summary.epoch == wire.epoch;
}

// error: Portable query rejection to expose at the platform API.
esp_err_t query_error(RejectCode error)
{
    switch (error) {
    case RejectCode::None: return ESP_OK;
    case RejectCode::Malformed: return ESP_ERR_INVALID_ARG;
    case RejectCode::UnsupportedVersion: return ESP_ERR_INVALID_VERSION;
    case RejectCode::UnsupportedCapability: return ESP_ERR_NOT_SUPPORTED;
    case RejectCode::MissingReference: return ESP_ERR_NOT_FOUND;
    case RejectCode::CapacityExceeded: return ESP_ERR_NO_MEM;
    case RejectCode::TransferTimeout:
    case RejectCode::Busy: return ESP_ERR_TIMEOUT;
    case RejectCode::Integrity: return ESP_ERR_INVALID_CRC;
    default: return ESP_ERR_INVALID_STATE;
    }
}

} // namespace

// status: Bounded copied summary of the last committed generation and link.
// now_us: Current monotonic clock read by the caller.
// link_eligible: Current persisted association may still claim live status.
esp_err_t copy_status(CodexDomainStatus &status, uint64_t now_us, bool link_eligible)
{
    Domain::StateSummary saved{};
    esp_err_t storage_error = ESP_OK;
    if (!codex_state_saved::try_read(saved, storage_error)) return ESP_ERR_TIMEOUT;
    status = {};
    status.storage_error = storage_error;
    auto summary = s_wire ? s_wire->store.summary() : Domain::StateSummary{};
    if (summary.revision == 0 && saved.revision != 0) {
        summary = saved;
        status.historical = true;
    }
    status.source = summary.source.value;
    status.epoch = summary.epoch.value;
    status.revision = summary.revision;
    status.chats = summary.chats;
    status.turns = summary.turns;
    status.active_calls = summary.active_calls;
    status.terminal_calls = summary.terminal_calls;
    if (!s_wire) return s_allocation_error;
    status.protocol_error = static_cast<uint8_t>(s_wire->protocol_error);
    status.ready = link_eligible && !status.historical && link_ready(*s_wire, summary, now_us);
    return s_wire->transport_error;
}

// query: Exact bounded projection request or paging cursor.
// result: Caller-owned output, never a borrowed generation reference.
// now_us: Receiver clock used to evaluate evidence and link freshness.
// link_eligible: The current retained association still permits live use.
esp_err_t copy_query(const Domain::StateQuery &query, Domain::StateQueryResult &result,
                     uint64_t now_us, bool link_eligible)
{
    if (s_wire) {
        const auto summary = s_wire->store.summary();
        if (summary.revision != 0 || query.kind != Domain::StateQueryKind::Summary) {
            const bool ready = link_eligible && link_ready(*s_wire, summary, now_us);
            return query_error(s_wire->store.query(query, now_us, ready, result));
        }
    } else if (query.kind != Domain::StateQueryKind::Summary) {
        result.reset();
        return query.kind == Domain::StateQueryKind::PendingQuestions
            ? ESP_ERR_NOT_SUPPORTED : ESP_ERR_INVALID_STATE;
    }
    // The checkpoint is diagnostic data only. It cannot supply a live bank,
    // answer a page query, or provide a base revision for a fresh session.
    const auto &cursor = query.cursor;
    if (query.chat != Domain::ChatId{} || query.turn != Domain::TurnId{} ||
        cursor.source.valid() || cursor.epoch.valid() || cursor.revision != 0 ||
        cursor.generation != 0 || cursor.after != 0 || cursor.chat != Domain::ChatId{} ||
        cursor.turn != Domain::TurnId{} || cursor.kind != Domain::StateQueryKind::Summary) {
        return ESP_ERR_INVALID_ARG;
    }
    Domain::StateSummary saved{};
    esp_err_t storage_error = ESP_OK;
    if (!codex_state_saved::try_read(saved, storage_error)) return ESP_ERR_TIMEOUT;
    result.reset();
    result.summary = saved;
    if (saved.revision != 0) result.health = Domain::StateViewHealth::Historical;
    if (storage_error != ESP_OK) return storage_error;
    return result.summary.revision != 0 ? ESP_OK : ESP_ERR_INVALID_STATE;
}

// chat: Exact chat identity supplied by an explicit local selection.
// now_us: Receiver clock used to reject stale authenticated leases.
esp_err_t validate_bookmark(const Domain::ChatId &chat, uint64_t now_us)
{
    if (!chat.valid()) return ESP_ERR_INVALID_ARG;
    if (!s_wire || !link_ready(*s_wire, s_wire->store.summary(), now_us)) return ESP_ERR_INVALID_STATE;
    Domain::StateQuery query{};
    query.kind = Domain::StateQueryKind::Chat;
    query.chat = chat;
    const auto error = s_wire->store.query(query, now_us, true, s_wire->bookmark_query);
    return query_error(error);
}

} // namespace LUNASHELL::codex_state

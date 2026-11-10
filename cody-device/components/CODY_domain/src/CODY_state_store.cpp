
/* Copyright (c) 2026 Umberto Giacobbi */
#include "CODY_state_store.hpp"
#include "CODY_state_json.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>

namespace CODY::Domain {
namespace {

using Protocol::RejectCode;
using StateJson::Record;

constexpr uint8_t kKinds = static_cast<uint8_t>(EntityKind::Count);
constexpr size_t kQuestionCapability = 5;
constexpr size_t kRecipientAcknowledgementCapability = 6;

enum QuestionField : size_t {
    QuestionChat, QuestionTurn, QuestionType, QuestionStatus, QuestionPrompt, QuestionTicket, QuestionFieldCount,
};

// kind: Exact decoded record kind from the schema.
// Returns whether this firmware has an admission owner for the kind.
constexpr bool supported_kind(EntityKind kind)
{
    return kind <= EntityKind::ToolCall ||
        (kind >= EntityKind::Question && kind <= EntityKind::AnswerAttempt);
}
constexpr std::size_t kRecordBytes = 4096;

struct Key {
    EntityKind kind{};
    uint64_t id = 0;

    // other: Key in the same source generation.
    bool operator<(Key other) const
    {
        return kind != other.kind ? kind < other.kind : id < other.id;
    }

    // other: Key in the same source generation.
    bool operator==(Key other) const { return kind == other.kind && id == other.id; }
};

// Slots carry schema-checked fields. References always use the declared kind and source.
struct StoredFact {
    Availability state{};
    uint64_t value = 0;
    uint64_t observation = 0;
    TextRef text{};
};

struct Entity {
    Key key{};
    uint64_t parent = 0;
    uint64_t definition = 0;
    std::array<TextRef, 2> text{};
    std::array<Capability, 8> caps{};
    std::array<StoredFact, QuestionFieldCount> facts{};
    bool upserted = false;
};

struct StoredEvidence {
    Evidence value{};
    bool supplied = false;
    bool referenced = false;
};

struct Removal {
    Key key{};
    uint64_t revision = 0;
};

struct Bank {
    StateSummary summary{};
    uint64_t committed_at_us = 0;
    std::array<Coverage, kKinds> coverage{};
    Entity *rows = nullptr;
    StoredEvidence *evidence = nullptr;
    Removal *removals = nullptr;
    char *text = nullptr;
    uint32_t used = 0;
    uint32_t evidence_used = 0;
    uint32_t removal_used = 0;
    uint32_t text_used = 0;

    // ref: A validated text span within this bank.
    std::string_view view(TextRef ref) const { return {text + ref.offset, ref.length}; }
};

constexpr std::array<std::string_view, kKinds> kKindNames{
    "source", "chat", "turn", "tool_definition", "tool_call", "agent_task",
    "external_operation", "question", "choice", "answer_attempt", "wait",
};

constexpr std::array<std::string_view, 12> kMembers{
    "schema", "kind", "request_id", "source_id", "epoch", "revision", "base_revision",
    "cue_watermark", "coverage", "evidence", "upserts", "remove",
};
constexpr uint8_t kCues = kMembers.size();
constexpr uint16_t kAllMembers = (uint16_t{1} << (kCues + 1)) - 1;

enum class Lex : uint8_t {
    Open, FirstKey, Key, Colon, Value, FirstItem, Item, ItemTail, MemberTail, End,
};

struct Reader {
    StateRequest request{};
    uint64_t revision = 0;
    uint64_t base = 0;
    uint32_t bytes = 0;
    uint16_t members = 0;
    uint16_t coverage = 0;
    uint16_t length = 0;
    uint8_t depth = 0;
    uint8_t member = 0;
    Lex next = Lex::Open;
    bool quoted = false;
    bool escaped = false;
    bool collecting = false;
    std::array<char, kRecordBytes> raw{};
    Record parsed{};
};

// names: Schema enum values in domain enum order.
// name: Exact decoded JSON string. Returns -1 for an unknown spelling.
int enumerate(std::initializer_list<std::string_view> names, std::string_view name)
{
    int i = 0;
    for (const auto value : names) {
        if (name == value) return i;
        ++i;
    }
    return -1;
}

// name: Exact entity type spelling from the wire.
int kind_of(std::string_view name)
{
    const auto found = std::find(kKindNames.begin(), kKindNames.end(), name);
    return found == kKindNames.end() ? -1 : static_cast<int>(found - kKindNames.begin());
}

// record: Parsed row.
// node: String node that must contain a nonzero token.
// out: Parsed numeric value.
bool nonzero(const Record &record, uint16_t node, uint64_t &out)
{
    return StateJson::hex64(record.string(node), out) && out != 0;
}

// row: A stored domain entity. Unknown lifecycle state is conservatively live.
bool terminal(const Entity &row)
{
    switch (row.key.kind) {
    case EntityKind::ToolCall: {
        const auto &fact = row.facts[1];
        return fact.state == Availability::Known && fact.value >= static_cast<uint64_t>(CallState::Returned);
    }
    case EntityKind::Turn: {
        const auto &fact = row.facts[0];
        return fact.state == Availability::Known && fact.value >= static_cast<uint64_t>(ExecutionState::Stopped);
    }
    case EntityKind::Chat: {
        const auto &fact = row.facts[1];
        return fact.state == Availability::Known && fact.value >= static_cast<uint64_t>(ChatLifecycle::Closed);
    }
    case EntityKind::Question: {
        const auto &fact = row.facts[QuestionStatus];
        return fact.state == Availability::Known && fact.value >= static_cast<uint64_t>(QuestionState::Answered);
    }
    case EntityKind::AnswerAttempt: {
        const auto &fact = row.facts[0];
        return fact.state == Availability::Known && fact.value >= static_cast<uint64_t>(AnswerState::RecipientDelivered);
    }
    case EntityKind::ToolDefinition:
        return true; // Final graph validation still requires every referenced definition.
    default:
        return false;
    }
}

// bank: Sorted entity rows.
// key: Exact source-local entity key.
Entity *find_row(const Bank &bank, Key key)
{
    auto *end = bank.rows + bank.used;
    auto *row = std::lower_bound(bank.rows, end, key, [](const Entity &entry, Key search) {
        return entry.key < search;
    });
    return row != end && row->key == key ? row : nullptr;
}

// bank: Sorted observation rows.
// id: Stable source observation token.
StoredEvidence *find_evidence(const Bank &bank, uint64_t id)
{
    auto *end = bank.evidence + bank.evidence_used;
    auto *row = std::lower_bound(bank.evidence, end, id, [](const StoredEvidence &entry, uint64_t search) {
        return entry.value.observation.value < search;
    });
    return row != end && row->value.observation.value == id ? row : nullptr;
}

// row: Newly received evidence with the original provider metadata.
// previous: Retained row with the same observation token.
// stage: Staging generation owning row's error text.
// live: Retained generation owning previous's error text.
bool same_origin(const Evidence &row, const Evidence &previous, const Bank &stage, const Bank &live)
{
    return row.epoch == previous.epoch && row.revision == previous.revision &&
        row.observed_at.clock == previous.observed_at.clock &&
        row.observed_at.basis == previous.observed_at.basis &&
        row.observed_at.microseconds == previous.observed_at.microseconds &&
        row.lease_ms == previous.lease_ms && row.lease_state == previous.lease_state &&
        row.provenance == previous.provenance && row.error.kind == previous.error.kind &&
        row.error.original_code == previous.error.original_code &&
        stage.view(row.error.detail) == live.view(previous.error.detail);
}

// row: New complete row.
// previous: Row from the previous generation with the same key.
// stage: Owner of new text.
// live: Owner of old text.
bool same_values(const Entity &row, const Entity &previous, const Bank &stage, const Bank &live)
{
    if (row.parent != previous.parent || row.definition != previous.definition || row.caps != previous.caps) {
        return false;
    }
    for (std::size_t i = 0; i < row.text.size(); ++i) {
        if (stage.view(row.text[i]) != live.view(previous.text[i])) return false;
    }
    for (std::size_t i = 0; i < row.facts.size(); ++i) {
        const auto &next = row.facts[i];
        const auto &old = previous.facts[i];
        if (next.state != old.state || next.value != old.value ||
            stage.view(next.text) != live.view(old.text)) return false;
    }
    return true;
}

// row: Entity whose removal is being validated.
// bank: Graph containing the row's parent when applicable.
bool removable(const Entity &row, const Bank &bank)
{
    if (row.key.kind != EntityKind::Choice) return terminal(row);
    const auto *question = find_row(bank, {EntityKind::Question, row.parent});
    return question && terminal(*question);
}

// bank: Complete candidate graph.
// question: One retained answerable question, which owns one through three unique choices.
bool valid_question_choices(const Bank &bank, const Entity &question)
{
    const auto *end = bank.rows + bank.used;
    const auto *choice = std::lower_bound(static_cast<const Entity *>(bank.rows), end,
        Key{EntityKind::Choice, 0}, [](const Entity &row, Key key) { return row.key < key; });
    std::array<uint64_t, 3> positions{};
    size_t count = 0;
    for (; choice != end && choice->key.kind == EntityKind::Choice; ++choice) {
        if (choice->parent != question.key.id) continue;
        const auto used = positions.begin() + count;
        if (count == positions.size() || std::find(positions.begin(), used, choice->definition) != used) {
            return false;
        }
        positions[count++] = choice->definition;
    }
    return count != 0;
}

// row: Staged replacement.
// previous: Previous record with the same immutable identity.
// stage: Owns replacement text.
// live: Owns previous text.
bool allowed_transition(const Entity &row, const Entity &previous, const Bank &stage, const Bank &live)
{
    if (row.parent != previous.parent || row.definition != previous.definition) return false;
    for (std::size_t i = 0; i < row.text.size(); ++i) {
        if (stage.view(row.text[i]) != live.view(previous.text[i])) return false;
    }
    if (row.caps != previous.caps) return false;
    const auto unchanged_terminal = [&](std::size_t slot) {
        const auto &old = previous.facts[slot];
        const auto &next = row.facts[slot];
        return next.state == old.state && next.value == old.value;
    };
    if (row.key.kind == EntityKind::Question) {
        if (!unchanged_terminal(QuestionType) ||
            (terminal(previous) && !unchanged_terminal(QuestionStatus)) ||
            row.facts[QuestionPrompt].state != previous.facts[QuestionPrompt].state ||
            stage.view(row.facts[QuestionPrompt].text) != live.view(previous.facts[QuestionPrompt].text)) return false;
        // A matching authenticated hook can fill an unassociated pair once.
        if (previous.facts[QuestionChat].state == Availability::Known &&
            (!unchanged_terminal(QuestionChat) || !unchanged_terminal(QuestionTurn))) return false;
    }
    if (row.key.kind == EntityKind::Choice) {
        for (size_t i = 0; i < 2; ++i) {
            if (row.facts[i].state != previous.facts[i].state ||
                stage.view(row.facts[i].text) != live.view(previous.facts[i].text)) return false;
        }
    }
    if (row.key.kind == EntityKind::AnswerAttempt && terminal(previous) && !unchanged_terminal(0)) {
        const auto old = static_cast<AnswerState>(previous.facts[0].value);
        const auto next = static_cast<AnswerState>(row.facts[0].value);
        if (old != AnswerState::Unconfirmed || next != AnswerState::Cancelled) return false;
    }
    if (row.key.kind == EntityKind::ToolCall && terminal(previous) && !unchanged_terminal(1)) return false;
    if (row.key.kind == EntityKind::Turn && previous.facts[0].state == Availability::Known &&
        previous.facts[0].value == static_cast<uint64_t>(ExecutionState::Interrupted) &&
        !unchanged_terminal(0)) return false;
    if (row.key.kind == EntityKind::ToolCall || row.key.kind == EntityKind::Turn) {
        const auto &outcome = previous.facts[row.key.kind == EntityKind::Turn ? 1 : 2];
        if (outcome.state == Availability::Known && outcome.value != static_cast<uint64_t>(Outcome::Unknown) &&
            !unchanged_terminal(row.key.kind == EntityKind::Turn ? 1 : 2)) return false;
    }
    return true;
}

// limits: Selected source/profile capacities. Keep admission bounded even if a peer overoffers.
bool supported_limits(const CapacityLimits &limits)
{
    return limits.sources == 1 && limits.chats == 64 &&
        limits.turns > 0 && limits.turns <= 192 && limits.tool_definitions <= 128 &&
        limits.active_calls <= 256 && limits.terminal_calls <= 128 &&
        limits.agents == 0 && limits.operations == 0 &&
        limits.open_questions <= 64 && limits.resolved_questions <= 32 &&
        limits.choices <= 288 && limits.answer_attempts <= 192 &&
        ((limits.open_questions == 0 && limits.resolved_questions == 0 &&
          limits.choices == 0 && limits.answer_attempts == 0) ||
         (limits.open_questions > 0 && limits.choices > 0 && limits.answer_attempts > 0)) &&
        limits.waits == 0 && limits.evidence > 0 && limits.evidence <= 4096 &&
        limits.text_bytes > 0 && limits.text_bytes <= 128 * 1024 &&
        limits.tombstones <= 512 && limits.decode_scratch_bytes >= sizeof(Reader);
}

// left: Configured profile capacities.
// right: Candidate negotiation limits.
bool same_limits(const CapacityLimits &left, const CapacityLimits &right)
{
    return left.sources == right.sources && left.chats == right.chats &&
        left.turns == right.turns && left.tool_definitions == right.tool_definitions &&
        left.active_calls == right.active_calls && left.terminal_calls == right.terminal_calls &&
        left.open_questions == right.open_questions && left.resolved_questions == right.resolved_questions &&
        left.choices == right.choices && left.answer_attempts == right.answer_attempts &&
        left.evidence == right.evidence && left.tombstones == right.tombstones &&
        left.text_bytes == right.text_bytes && left.decode_scratch_bytes == right.decode_scratch_bytes &&
        left.persistence_scratch_bytes == right.persistence_scratch_bytes;
}

} // namespace

struct StateStore::Storage {
    CapacityLimits limits{};
    StateRelease release = nullptr;
    StateAllocate allocate = nullptr;
    Bank banks[2]{};
    Reader reader{};
    uint8_t current = 0;
    uint64_t admission = 0;
    bool pending = false;
    RejectCode error = RejectCode::None;
    uint32_t retained_removals = 0;

    // No parameters: maximum combined rows admitted across the five supported kinds.
    uint32_t row_limit() const
    {
        return limits.sources + limits.chats + limits.turns + limits.tool_definitions +
            limits.active_calls + limits.terminal_calls + limits.open_questions +
            limits.resolved_questions + limits.choices + limits.answer_attempts;
    }

    // No parameters: current committed generation.
    Bank &live() { return banks[current]; }

    // No parameters: transaction scratch, inaccessible to external readers.
    Bank &stage() { return banks[1U - current]; }

    // bank: Empty metadata with backing pointers populated individually.
    bool allocate_bank(Bank &bank);

    // bank: Backing allocation and metadata to release.
    void release_bank(Bank &bank);

    // text: Validated decoded ASCII bytes from the bounded decoder.
    // out: New staging-generation text span.
    bool append(std::string_view text, TextRef &out);

    // from: Text span in the committed bank.
    // to: Text span in the staged bank.
    bool copy_text(TextRef from, TextRef &to);

    // No parameters: clone the live bank into staging with compacted text.
    bool clone();

    // node: Decoded field node.
    // value: Destination text reference.
    // minimum: Minimum admitted decoded length.
    // maximum: Maximum admitted decoded length.
    bool text_value(uint16_t node, TextRef &value, std::size_t minimum, std::size_t maximum);

    // node: Exact fact object.
    // value: Destination normalized availability, identity and scalar value.
    // names: Enum vocabulary, or empty for a source-local ID or bounded text.
    // max_text: Nonzero for a text fact, zero for an enum or ID.
    bool fact(uint16_t node, StoredFact &value, std::initializer_list<std::string_view> names = {},
              std::size_t max_text = 0);

    // No parameters: admit a bounded coverage row.
    RejectCode parse_coverage();

    // No parameters: admit a bounded observation record.
    RejectCode parse_evidence();

    // No parameters: admit one bounded entity upsert.
    RejectCode parse_entity();

    // No parameters: stage one explicit removal, including its exact target revision.
    RejectCode parse_removal();

    // No parameters: validate and store one root member name.
    RejectCode read_key();

    // No parameters: check the scalar header value for the current member.
    RejectCode read_header();

    // byte: Exact authenticated contiguous JSON byte.
    RejectCode feed(char byte);

    // byte: Byte in the current string/object capture.
    RejectCode collect(char byte);

    // request_us: Original request clock reading.
    // now_us: Current receiver clock reading.
    RejectCode validate(uint64_t request_us, uint64_t now_us);
};

namespace {

// memory: Backing allocation guaranteed aligned by the owner.
// count: Number of trivial objects in the fixed arena.
template<class T>
T *construct_array(void *memory, uint32_t count)
{
    if (!memory) return nullptr;
    auto *objects = static_cast<T *>(memory);
    for (uint32_t i = 0; i < count; ++i) new (objects + i) T{};
    return objects;
}

} // namespace

// bank: Scratch or live bank allocated once during negotiated configuration.
bool StateStore::Storage::allocate_bank(Bank &bank)
{
    bank.rows = construct_array<Entity>(allocate(sizeof(Entity) * row_limit()), row_limit());
    bank.evidence = construct_array<StoredEvidence>(allocate(sizeof(StoredEvidence) * limits.evidence), limits.evidence);
    const uint32_t removals = std::max(limits.tombstones, uint32_t{1});
    bank.removals = construct_array<Removal>(allocate(sizeof(Removal) * removals), removals);
    bank.text = static_cast<char *>(allocate(limits.text_bytes));
    return bank.rows && bank.evidence && bank.removals && bank.text;
}

// bank: Partly or fully allocated generation.
void StateStore::Storage::release_bank(Bank &bank)
{
    if (bank.rows) release(bank.rows);
    if (bank.evidence) release(bank.evidence);
    if (bank.removals) release(bank.removals);
    if (bank.text) release(bank.text);
}

// text: Validated ASCII bytes, borrowed only for this copy.
// out: Staged span, updated after admission.
bool StateStore::Storage::append(std::string_view text, TextRef &out)
{
    auto &bank = stage();
    if (text.size() > limits.text_bytes - bank.text_used) {
        error = RejectCode::CapacityExceeded;
        return false;
    }
    out = {bank.text_used, static_cast<uint16_t>(text.size())};
    if (!text.empty()) std::memcpy(bank.text + bank.text_used, text.data(), text.size());
    bank.text_used += out.length;
    return true;
}

// from: Immutable live text span.
// to: Staging text span.
bool StateStore::Storage::copy_text(TextRef from, TextRef &to)
{
    return append(live().view(from), to);
}

// No parameters: stage a delta from the coherent live generation.
bool StateStore::Storage::clone()
{
    const auto &old = live();
    auto &next = stage();
    next.used = old.used;
    next.evidence_used = old.evidence_used;
    next.removal_used = old.removal_used;
    std::copy_n(old.rows, old.used, next.rows);
    std::copy_n(old.evidence, old.evidence_used, next.evidence);
    std::copy_n(old.removals, old.removal_used, next.removals);
    for (uint32_t i = 0; i < next.used; ++i) {
        auto &row = next.rows[i];
        row.upserted = false;
        for (auto &value : row.text) if (!copy_text(value, value)) return false;
        for (auto &value : row.facts) if (!copy_text(value.text, value.text)) return false;
    }
    for (uint32_t i = 0; i < next.evidence_used; ++i) {
        auto &row = next.evidence[i];
        row.supplied = false;
        row.referenced = false;
        if (!copy_text(row.value.error.detail, row.value.error.detail)) return false;
    }
    retained_removals = old.removal_used;
    return true;
}

// node: String node from the decoded record.
// value: Span allocated in the staging text pool.
// minimum: Inclusive minimum decoded string length.
// maximum: Inclusive maximum decoded string length.
bool StateStore::Storage::text_value(uint16_t node, TextRef &value, std::size_t minimum,
                                      std::size_t maximum)
{
    const auto &json = reader.parsed;
    const auto text = json.string(node);
    return json.is_string(node) && text.size() >= minimum && text.size() <= maximum && append(text, value);
}

// node: Exact fact object with one availability/value/evidence tuple.
// value: Typed fact fields encoded in normalized storage slots.
// names: Nonempty for an enum fact.
// max_text: Nonzero for a bounded text fact.
bool StateStore::Storage::fact(uint16_t node, StoredFact &value,
    std::initializer_list<std::string_view> names, std::size_t max_text)
{
    const auto &json = reader.parsed;
    if (!json.fields(node, {"state", "value", "evidence"})) return false;
    const auto state = enumerate({"unknown", "known", "absent", "unsupported", "failed", "conflict"},
                                 json.string(json.field(node, "state")));
    if (state < 0) return false;
    value.state = static_cast<Availability>(state);
    const auto evidence = json.field(node, "evidence");
    if (value.state != Availability::Unknown || !json.is_null(evidence)) {
        if (!nonzero(json, evidence, value.observation)) return false;
    }
    const auto raw = json.field(node, "value");
    if (value.state != Availability::Known) return json.is_null(raw);
    if (max_text != 0) return text_value(raw, value.text, 0, max_text);
    if (names.size() == 0) return nonzero(json, raw, value.value);
    const auto found = enumerate(names, json.string(raw));
    if (found < 0) return false;
    value.value = static_cast<uint64_t>(found);
    return true;
}


// No parameters: consumes the parsed coverage row from the current transfer.
RejectCode StateStore::Storage::parse_coverage()
{
    const auto &json = reader.parsed;
    if (!json.fields(1, {"kind", "state", "admitted", "omitted"})) return RejectCode::Malformed;
    const auto kind = kind_of(json.string(json.field(1, "kind")));
    const auto state = enumerate({"unknown", "complete", "partial", "unsupported"},
                                 json.string(json.field(1, "state")));
    const auto admitted = json.field(1, "admitted");
    const auto omitted = json.field(1, "omitted");
    if (kind < 0 || state < 0 || !json.is_number(admitted) || json[admitted].number > 65536 ||
        (!json.is_number(omitted) && !json.is_null(omitted))) return RejectCode::Malformed;
    const auto bit = uint16_t{1} << kind;
    if ((reader.coverage & bit) != 0) return RejectCode::Malformed;
    reader.coverage |= bit;
    auto &row = stage().coverage[kind];
    row.source = reader.request.source;
    row.kind = static_cast<EntityKind>(kind);
    row.state = static_cast<CoverageState>(state);
    row.admitted = json[admitted].number;
    row.omitted_state = json.is_null(omitted) ? Availability::Unknown : Availability::Known;
    row.omitted = json[omitted].number;
    if (row.state == CoverageState::Complete &&
        (row.omitted_state != Availability::Known || row.omitted != 0)) return RejectCode::Malformed;
    if (row.state == CoverageState::Unsupported && row.admitted != 0) return RejectCode::Malformed;
    if ((!supported_kind(row.kind) || (row.kind >= EntityKind::Question && limits.open_questions == 0)) &&
        row.state != CoverageState::Unsupported) {
        return RejectCode::UnsupportedCapability;
    }
    return RejectCode::None;
}

// No parameters: normalizes one observation with validated source provenance.
RejectCode StateStore::Storage::parse_evidence()
{
    const auto &json = reader.parsed;
    if (!json.fields(1, {"id", "origin_epoch", "origin_revision", "observed_at", "source_age_ms",
                         "lease_ms", "freshness", "provenance", "error"})) return RejectCode::Malformed;
    StoredEvidence candidate{};
    auto &row = candidate.value;
    row.source = reader.request.source;
    row.observation.source = row.source;
    if (!nonzero(json, json.field(1, "id"), row.observation.value) ||
        !nonzero(json, json.field(1, "origin_epoch"), row.epoch.value) ||
        !nonzero(json, json.field(1, "origin_revision"), row.revision)) return RejectCode::Malformed;

    const auto freshness = enumerate({"unknown", "current", "stale", "historical"},
                                     json.string(json.field(1, "freshness")));
    const auto provenance = enumerate({"", "native_event", "native_read", "broker", "device_action",
                                       "restored", "synthetic"}, json.string(json.field(1, "provenance")));
    if (freshness < 0 || provenance <= 0) return RejectCode::Malformed;
    row.freshness = static_cast<Freshness>(freshness);
    row.provenance = static_cast<Provenance>(provenance);
    if (row.provenance == Provenance::Synthetic || row.provenance == Provenance::DeviceAction) {
        return RejectCode::UnsupportedCapability;
    }
    const auto age = json.field(1, "source_age_ms");
    const auto lease = json.field(1, "lease_ms");
    if (!json.is_null(age)) {
        if (!json.is_number(age)) return RejectCode::Malformed;
        row.age_state = Availability::Known;
        row.age_at_receive_us = uint64_t{json[age].number} * 1000U;
    }
    if (!json.is_null(lease)) {
        if (!json.is_number(lease) || json[lease].number == 0 ||
            json[lease].number > Protocol::kMaximumEvidenceLeaseMs) return RejectCode::Malformed;
        row.lease_state = Availability::Known;
        row.lease_ms = json[lease].number;
    }
    if (row.freshness == Freshness::Current &&
        (row.age_state != Availability::Known || row.lease_state != Availability::Known ||
         row.age_at_receive_us >= uint64_t{row.lease_ms} * 1000U)) return RejectCode::Malformed;

    const auto observed = json.field(1, "observed_at");
    if (!json.is_null(observed)) {
        if (!json.fields(observed, {"basis", "clock_id", "microseconds"}) ||
            !nonzero(json, json.field(observed, "clock_id"), row.observed_at.clock.value) ||
            !StateJson::hex64(json.string(json.field(observed, "microseconds")), row.observed_at.microseconds)) {
            return RejectCode::Malformed;
        }
        const auto basis = enumerate({"", "unix_microseconds", "monotonic_microseconds"},
                                     json.string(json.field(observed, "basis")));
        if (basis <= 0) return RejectCode::Malformed;
        row.observed_at.basis = static_cast<TimeBasis>(basis);
    }

    const auto failure = json.field(1, "error");
    if (!json.is_null(failure)) {
        if (!json.fields(failure, {"kind", "original_code", "detail"})) return RejectCode::Malformed;
        const auto kind = enumerate({"", "invalid_identity", "duplicate_identity", "invalid_value",
                                     "missing_reference", "revision_conflict", "capacity_exceeded",
                                     "unsupported", "persistence", "transport", "provider"},
                                    json.string(json.field(failure, "kind")));
        if (kind <= 0 || !StateJson::int64_string(json.string(json.field(failure, "original_code")),
                                                 row.error.original_code)) return RejectCode::Malformed;
        row.error.kind = static_cast<ErrorKind>(kind);
        if (!text_value(json.field(failure, "detail"), row.error.detail, 0, 512)) {
            return error == RejectCode::None ? RejectCode::Malformed : error;
        }
    }

    auto &bank = stage();
    auto *end = bank.evidence + bank.evidence_used;
    auto *at = std::lower_bound(bank.evidence, end, row.observation.value,
        [](const StoredEvidence &entry, uint64_t id) { return entry.value.observation.value < id; });
    if (at != end && at->value.observation == row.observation) {
        if (at->supplied) return RejectCode::RevisionConflict;
    } else {
        if (bank.evidence_used == limits.evidence) return RejectCode::CapacityExceeded;
        std::move_backward(at, end, end + 1);
        ++bank.evidence_used;
    }
    candidate.supplied = true;
    *at = candidate;
    return RejectCode::None;
}

// No parameters: decodes one schema-valid row for the selected profile.
RejectCode StateStore::Storage::parse_entity()
{
    const auto &json = reader.parsed;
    const auto field = [&](std::string_view name) { return json.field(1, name); };
    const auto kind = kind_of(json.string(field("type")));
    Entity row{};
    if (kind < 0 || !nonzero(json, field("id"), row.key.id)) return RejectCode::Malformed;
    row.key.kind = static_cast<EntityKind>(kind);
    if (!supported_kind(row.key.kind)) return RejectCode::UnsupportedCapability;
    if (row.key.kind >= EntityKind::Question && limits.open_questions == 0) {
        return RejectCode::UnsupportedCapability;
    }
    bool valid = false;
    switch (row.key.kind) {
    case EntityKind::Source: {
        if (!json.fields(1, {"type", "id", "provider", "native_namespace", "capabilities", "link"}) ||
            row.key.id != reader.request.source.value) return RejectCode::WrongBinding;
        constexpr std::array<std::string_view, 8> names{
            "chats", "turns", "tool_calls", "agents", "external_operations", "questions",
            "recipient_acknowledgement", "resume_questions",
        };
        const auto capabilities = field("capabilities");
        if (!json.fields(capabilities, {names[0], names[1], names[2], names[3],
                                        names[4], names[5], names[6], names[7]})) {
            return RejectCode::Malformed;
        }
        for (std::size_t i = 0; i < names.size(); ++i) {
            const auto capability = enumerate({"unknown", "supported", "unsupported"},
                json.string(json.field(capabilities, names[i])));
            if (capability < 0) return RejectCode::Malformed;
            row.caps[i] = static_cast<Capability>(capability);
            if (i >= 3 && i != kQuestionCapability && i != kRecipientAcknowledgementCapability &&
                row.caps[i] != Capability::Unsupported) return RejectCode::UnsupportedCapability;
        }
        const bool questions = row.caps[kQuestionCapability] == Capability::Supported;
        if (questions != (limits.open_questions != 0) ||
            (row.caps[kRecipientAcknowledgementCapability] == Capability::Supported && !questions)) {
            return RejectCode::UnsupportedCapability;
        }
        const auto &binding = reader.request;
        if (binding.provider[0] != '\0' &&
            (json.string(field("provider")) != binding.provider.data() ||
             json.string(field("native_namespace")) != binding.native_namespace.data() ||
             row.caps != binding.capabilities)) return RejectCode::WrongBinding;
        valid = text_value(field("provider"), row.text[0], 1, 64) &&
            text_value(field("native_namespace"), row.text[1], 1, 128) &&
            fact(field("link"), row.facts[0], {"unknown", "disconnected", "connecting", "authenticated", "failed"});
        break;
    }
    case EntityKind::Chat:
        valid = json.fields(1, {"type", "id", "title", "lifecycle", "current_turn"}) &&
            fact(field("title"), row.facts[0], {}, 255) &&
            fact(field("lifecycle"), row.facts[1], {"unknown", "open", "closed", "archived"}) &&
            fact(field("current_turn"), row.facts[2]);
        break;
    case EntityKind::Turn:
        valid = json.fields(1, {"type", "id", "chat", "execution", "outcome", "reply"}) &&
            nonzero(json, field("chat"), row.parent) &&
            fact(field("execution"), row.facts[0], {"unknown", "queued", "running", "waiting", "stopped", "interrupted"}) &&
            fact(field("outcome"), row.facts[1], {"unknown", "succeeded", "failed", "cancelled"}) &&
            fact(field("reply"), row.facts[2], {"unknown", "unavailable", "available"});
        break;
    case EntityKind::ToolDefinition:
        valid = json.fields(1, {"type", "id", "original_name", "display_label", "category"}) &&
            text_value(field("original_name"), row.text[0], 1, 128) &&
            fact(field("display_label"), row.facts[0], {}, 96) &&
            fact(field("category"), row.facts[1], {"unknown", "command", "file_read", "file_edit",
                                                  "search", "browser", "agent", "service"});
        break;
    case EntityKind::ToolCall:
        valid = json.fields(1, {"type", "id", "turn", "definition", "executing_agent", "state", "outcome"}) &&
            nonzero(json, field("turn"), row.parent) && nonzero(json, field("definition"), row.definition) &&
            fact(field("executing_agent"), row.facts[0]) && row.facts[0].state != Availability::Known &&
            fact(field("state"), row.facts[1], {"unknown", "queued", "running", "returned", "failed", "cancelled"}) &&
            fact(field("outcome"), row.facts[2], {"unknown", "succeeded", "failed", "cancelled"});
        break;
    case EntityKind::Question:
        valid = json.fields(1, {"type", "id", "chat", "turn", "kind", "state", "prompt", "ticket"}) &&
            fact(field("chat"), row.facts[QuestionChat]) &&
            fact(field("turn"), row.facts[QuestionTurn]) &&
            fact(field("kind"), row.facts[QuestionType], {"unknown", "answerable", "native_approval_notice"}) &&
            fact(field("state"), row.facts[QuestionStatus], {"unknown", "open", "answered", "cancelled", "superseded"}) &&
            fact(field("prompt"), row.facts[QuestionPrompt], {}, 512) &&
            fact(field("ticket"), row.facts[QuestionTicket], {"unknown", "available", "expired", "unavailable"}) &&
            row.facts[QuestionType].state == Availability::Known &&
            row.facts[QuestionType].value == static_cast<uint64_t>(QuestionKind::Answerable) &&
            row.facts[QuestionStatus].state == Availability::Known &&
            row.facts[QuestionStatus].value != static_cast<uint64_t>(QuestionState::Unknown) &&
            row.facts[QuestionPrompt].state == Availability::Known && row.facts[QuestionPrompt].text.length != 0;
        break;
    case EntityKind::Choice: {
        const auto order = field("display_order");
        valid = json.fields(1, {"type", "id", "question", "display_order", "label", "detail"}) &&
            nonzero(json, field("question"), row.parent) && json.is_number(order) &&
            json[order].number <= UINT16_MAX &&
            fact(field("label"), row.facts[0], {}, 96) && row.facts[0].text.length != 0 &&
            fact(field("detail"), row.facts[1], {}, 1024) && row.facts[1].text.length != 0;
        if (valid) row.definition = json[order].number;
        break;
    }
    case EntityKind::AnswerAttempt:
        valid = json.fields(1, {"type", "id", "question", "choice", "state"}) &&
            nonzero(json, field("question"), row.parent) && nonzero(json, field("choice"), row.definition) &&
            fact(field("state"), row.facts[0], {"unknown", "admitted", "persisted", "sending",
                "broker_received", "recipient_delivered", "rejected", "cancelled", "unconfirmed"}) &&
            row.facts[0].state == Availability::Known &&
            row.facts[0].value != static_cast<uint64_t>(AnswerState::Unknown);
        break;
    default:
        return RejectCode::UnsupportedCapability;
    }
    if (!valid) return error == RejectCode::None ? RejectCode::Malformed : error;

    auto &bank = stage();
    auto *end = bank.rows + bank.used;
    auto *at = std::lower_bound(bank.rows, end, row.key, [](const Entity &entry, Key key) {
        return entry.key < key;
    });
    if (at != end && at->key == row.key) {
        if (at->upserted) return RejectCode::RevisionConflict;
    } else {
        if (bank.used == row_limit()) return RejectCode::CapacityExceeded;
        std::move_backward(at, end, end + 1);
        ++bank.used;
    }
    row.upserted = true;
    *at = row;
    return RejectCode::None;
}

// No parameters: holds an exact entity removal until the whole graph can be checked.
RejectCode StateStore::Storage::parse_removal()
{
    const auto &json = reader.parsed;
    if (!json.fields(1, {"type", "id", "revision"})) return RejectCode::Malformed;
    const auto kind = kind_of(json.string(json.field(1, "type")));
    Removal row{};
    if (kind < 0 || !nonzero(json, json.field(1, "id"), row.key.id) ||
        !nonzero(json, json.field(1, "revision"), row.revision)) return RejectCode::Malformed;
    row.key.kind = static_cast<EntityKind>(kind);
    if (!supported_kind(row.key.kind) || row.key.kind == EntityKind::Source ||
        (row.key.kind >= EntityKind::Question && limits.open_questions == 0)) {
        return RejectCode::UnsupportedCapability;
    }
    auto &bank = stage();
    if (bank.removal_used == limits.tombstones) return RejectCode::CapacityExceeded;
    for (uint32_t i = 0; i < bank.removal_used; ++i) {
        if (bank.removals[i].key == row.key) return RejectCode::RevisionConflict;
    }
    bank.removals[bank.removal_used++] = row;
    return RejectCode::None;
}


// No parameters: admits each exact envelope member once, in any order.
RejectCode StateStore::Storage::read_key()
{
    const auto key = reader.parsed.string(1);
    const auto at = std::find(kMembers.begin(), kMembers.end(), key);
    const auto member = key == "cues" ? kCues : static_cast<uint8_t>(at - kMembers.begin());
    if (key != "cues" && at == kMembers.end()) return RejectCode::Malformed;
    const auto bit = uint16_t{1} << member;
    if ((reader.members & bit) != 0) return RejectCode::Malformed;
    reader.members |= bit;
    reader.member = member;
    return RejectCode::None;
}

// No parameters: compares an envelope scalar to the locally pending request.
RejectCode StateStore::Storage::read_header()
{
    const auto value = reader.parsed.string(1);
    if (reader.member == 0) {
        return value == Protocol::kSchema ? RejectCode::None : RejectCode::UnsupportedVersion;
    }
    if (reader.member == 1) {
        return value == (reader.request.snapshot ? "snapshot" : "delta")
            ? RejectCode::None : RejectCode::WrongBinding;
    }
    uint64_t id = 0;
    if (!StateJson::hex64(value, id)) return RejectCode::Malformed;
    switch (reader.member) {
    case 2: return id == reader.request.request_id ? RejectCode::None : RejectCode::WrongBinding;
    case 3: return id == reader.request.source.value ? RejectCode::None : RejectCode::WrongBinding;
    case 4: return id == reader.request.epoch.value ? RejectCode::None : RejectCode::WrongBinding;
    case 5: reader.revision = id; break;
    case 6: reader.base = id; break;
    case 7: if (id != 0) return RejectCode::UnsupportedCapability; break;
    default: return RejectCode::Malformed;
    }
    return RejectCode::None;
}

// byte: One byte of the current bounded record or header string.
RejectCode StateStore::Storage::collect(char byte)
{
    if (reader.length == reader.raw.size()) return RejectCode::CapacityExceeded;
    const bool first = reader.length == 0;
    reader.raw[reader.length++] = byte;
    if (reader.quoted) {
        if (reader.escaped) {
            reader.escaped = false;
        } else if (byte == '\\') {
            reader.escaped = true;
        } else if (byte == '"') {
            reader.quoted = false;
        }
    } else if (byte == '"') {
        reader.quoted = true;
    } else if (byte == '{' || byte == '[') {
        if (++reader.depth > 6) return RejectCode::Malformed;
    } else if (byte == '}' || byte == ']') {
        if (reader.depth == 0) return RejectCode::Malformed;
        --reader.depth;
    }
    if (first || reader.quoted || reader.depth != 0) return RejectCode::None;

    reader.collecting = false;
    if (!reader.parsed.parse(reader.raw.data(), reader.length)) return RejectCode::Malformed;
    if (reader.next == Lex::FirstKey || reader.next == Lex::Key) {
        reader.next = Lex::Colon;
        return read_key();
    }
    if (reader.next == Lex::Value) {
        reader.next = Lex::MemberTail;
        return read_header();
    }
    reader.next = Lex::ItemTail;
    switch (reader.member) {
    case 8: return parse_coverage();
    case 9: return parse_evidence();
    case 10: return parse_entity();
    case 11: return parse_removal();
    default: return RejectCode::UnsupportedCapability;
    }
}

// byte: One exact byte of the authenticated contiguous document.
RejectCode StateStore::Storage::feed(char byte)
{
    if (reader.collecting) return collect(byte);
    if (StateJson::space(byte)) return RejectCode::None;

    switch (reader.next) {
    case Lex::Open:
        if (byte != '{') return RejectCode::Malformed;
        reader.next = Lex::FirstKey;
        return RejectCode::None;
    case Lex::FirstKey:
    case Lex::Key:
        if (byte != '"') return RejectCode::Malformed;
        break;
    case Lex::Colon:
        if (byte != ':') return RejectCode::Malformed;
        reader.next = Lex::Value;
        return RejectCode::None;
    case Lex::Value:
        if (reader.member >= 8) {
            if (byte != '[') return RejectCode::Malformed;
            reader.next = Lex::FirstItem;
            return RejectCode::None;
        }
        if (byte != '"') return RejectCode::Malformed;
        break;
    case Lex::FirstItem:
        if (byte == ']') {
            reader.next = Lex::MemberTail;
            return RejectCode::None;
        }
        [[fallthrough]];
    case Lex::Item:
        if (byte != '{') return RejectCode::Malformed;
        if (reader.member == kCues) return RejectCode::UnsupportedCapability;
        break;
    case Lex::ItemTail:
        if (byte == ',') {
            reader.next = Lex::Item;
        } else if (byte == ']') {
            reader.next = Lex::MemberTail;
        } else {
            return RejectCode::Malformed;
        }
        return RejectCode::None;
    case Lex::MemberTail:
        if (byte == ',') {
            reader.next = Lex::Key;
        } else if (byte == '}') {
            reader.next = Lex::End;
        } else {
            return RejectCode::Malformed;
        }
        return RejectCode::None;
    case Lex::End:
        return RejectCode::Malformed;
    }
    reader.length = 0;
    reader.collecting = true;
    return collect(byte);
}

// request_us: Original locally stamped request time, unchanged on retries.
// now_us: Current time on the same receiver clock.
RejectCode StateStore::Storage::validate(uint64_t request_us, uint64_t now_us)
{
    auto &bank = stage();
    const auto &old = live();
    const bool same_source = old.summary.source == reader.request.source;
    const bool same_epoch = same_source && old.summary.epoch == reader.request.epoch;
    const bool equal_revision = same_epoch && old.summary.revision == reader.revision;
    if (reader.revision == 0 || (reader.request.snapshot && reader.base != 0) ||
        (same_epoch && reader.revision < old.summary.revision)) return RejectCode::RevisionConflict;
    if (!reader.request.snapshot &&
        (!same_epoch || reader.base != old.summary.revision || reader.revision <= reader.base)) {
        return RejectCode::NeedSnapshot;
    }
    if (now_us < request_us || now_us - request_us >= uint64_t{Protocol::kTransferTimeoutMs} * 1000U) {
        return RejectCode::TransferTimeout;
    }

    for (uint32_t i = retained_removals; i < bank.removal_used; ++i) {
        const auto &removal = bank.removals[i];
        if (removal.revision != reader.revision || equal_revision) return RejectCode::RevisionConflict;
        if (auto *row = find_row(bank, removal.key)) {
            if (row->upserted || !removable(*row, old)) return RejectCode::InvalidTransition;
            std::move(row + 1, bank.rows + bank.used, row);
            --bank.used;
        } else if (same_source) {
            const auto *previous = find_row(old, removal.key);
            if (previous && !removable(*previous, old)) return RejectCode::InvalidTransition;
        }
    }

    if (same_source) {
        for (uint32_t i = 0; i < old.used; ++i) {
            const auto &previous = old.rows[i];
            const auto *current = find_row(bank, previous.key);
            if (current) {
                if (!allowed_transition(*current, previous, bank, old) ||
                    (equal_revision && !same_values(*current, previous, bank, old))) {
                    return RejectCode::RevisionConflict;
                }
            } else {
                const bool explicitly_removed = std::any_of(bank.removals + retained_removals,
                    bank.removals + bank.removal_used, [&](const Removal &entry) {
                        return entry.key == previous.key;
                    });
                if (equal_revision || !removable(previous, old) || (reader.request.snapshot &&
                    !explicitly_removed && bank.coverage[static_cast<uint8_t>(previous.key.kind)].state !=
                        CoverageState::Complete)) return RejectCode::NeedSnapshot;
            }
        }
        if (same_epoch && reader.request.snapshot) {
            for (uint32_t i = 0; i < old.removal_used; ++i) {
                if (find_row(bank, old.removals[i].key)) return RejectCode::RevisionConflict;
            }
        }
    }
    for (uint32_t i = 0; i < bank.removal_used; ++i) {
        if (find_row(bank, bank.removals[i].key)) return RejectCode::RevisionConflict;
    }

    std::array<uint32_t, kKinds> counts{};
    uint32_t completed = 0;
    uint32_t resolved_questions = 0;
    for (uint32_t i = 0; i < bank.used; ++i) {
        const auto &row = bank.rows[i];
        ++counts[static_cast<uint8_t>(row.key.kind)];
        if (row.key.kind == EntityKind::Chat && row.facts[2].state == Availability::Known) {
            const auto *turn = find_row(bank, {EntityKind::Turn, row.facts[2].value});
            if (!turn || turn->parent != row.key.id) return RejectCode::MissingReference;
        }
        if (row.key.kind == EntityKind::Turn && !find_row(bank, {EntityKind::Chat, row.parent})) {
            return RejectCode::MissingReference;
        }
        if (row.key.kind == EntityKind::ToolCall) {
            if (!find_row(bank, {EntityKind::Turn, row.parent}) ||
                !find_row(bank, {EntityKind::ToolDefinition, row.definition})) return RejectCode::MissingReference;
            if (terminal(row)) ++completed;
        }
        if (row.key.kind == EntityKind::Question) {
            const auto &chat = row.facts[QuestionChat];
            const auto &turn = row.facts[QuestionTurn];
            const bool associated = chat.state == Availability::Known;
            if (associated != (turn.state == Availability::Known)) return RejectCode::MissingReference;
            if (associated) {
                const auto *owner = find_row(bank, {EntityKind::Turn, turn.value});
                if (!owner || owner->parent != chat.value) return RejectCode::MissingReference;
            }
            if (!valid_question_choices(bank, row)) return RejectCode::MissingReference;
            if (terminal(row)) ++resolved_questions;
        }
        if (row.key.kind == EntityKind::Choice) {
            const Key parent{EntityKind::Question, row.parent};
            if (!find_row(bank, parent)) return RejectCode::MissingReference;
            // The choices supplied with an existing question are immutable.
            if (same_source && find_row(old, parent) && !find_row(old, row.key)) {
                return RejectCode::InvalidTransition;
            }
        }
        if (row.key.kind == EntityKind::AnswerAttempt) {
            const auto *choice = find_row(bank, {EntityKind::Choice, row.definition});
            if (!choice || choice->parent != row.parent) return RejectCode::MissingReference;
        }
        for (const auto &fact : row.facts) {
            if (fact.observation == 0) continue;
            auto *evidence = find_evidence(bank, fact.observation);
            if (!evidence || (row.upserted && !evidence->supplied)) return RejectCode::MissingReference;
            if (fact.state == Availability::Failed && evidence->value.error.kind == ErrorKind::None) {
                return RejectCode::Malformed;
            }
            if (row.upserted) evidence->referenced = true;
        }
    }

    const auto active = counts[4] - completed;
    if (counts[0] != 1 || counts[1] > limits.chats || counts[2] > limits.turns ||
        counts[3] > limits.tool_definitions || active > limits.active_calls ||
        completed > limits.terminal_calls) return RejectCode::CapacityExceeded;
    const auto question_count = counts[static_cast<uint8_t>(EntityKind::Question)];
    if (question_count - resolved_questions > limits.open_questions ||
        resolved_questions > limits.resolved_questions ||
        counts[static_cast<uint8_t>(EntityKind::Choice)] > limits.choices ||
        counts[static_cast<uint8_t>(EntityKind::AnswerAttempt)] > limits.answer_attempts) {
        return RejectCode::CapacityExceeded;
    }
    for (uint8_t i = 0; i < kKinds; ++i) {
        if (bank.coverage[i].admitted != counts[i]) return RejectCode::Malformed;
    }

    const uint64_t elapsed_us = ((now_us - request_us + 999U) / 1000U) * 1000U;
    for (uint32_t i = 0; i < bank.evidence_used; ++i) {
        auto &evidence = bank.evidence[i];
        auto &value = evidence.value;
        if (!evidence.supplied) continue;
        if (!evidence.referenced || (value.epoch == reader.request.epoch && value.revision > reader.revision)) {
            return RejectCode::Malformed;
        }
        const auto *previous = same_source ? find_evidence(old, value.observation.value) : nullptr;
        if (previous && !same_origin(value, previous->value, bank, old)) return RejectCode::RevisionConflict;
        if (previous) {
            value.id = previous->value.id;
            value.admitted_sequence = previous->value.admitted_sequence;
        } else {
            if (admission == std::numeric_limits<uint64_t>::max()) return RejectCode::CapacityExceeded;
            value.id.value = value.admitted_sequence = ++admission;
        }
        if (value.age_state == Availability::Known) value.age_at_receive_us += elapsed_us;
        if (previous && previous->value.age_state == Availability::Known &&
            now_us >= previous->value.received_at_us) {
            const auto prior_age = previous->value.age_at_receive_us + (now_us - previous->value.received_at_us);
            value.age_at_receive_us = std::max(value.age_at_receive_us, prior_age);
            value.age_state = Availability::Known;
        }
        if (value.provenance == Provenance::Restored ||
            (previous && previous->value.freshness == Freshness::Historical)) {
            value.freshness = Freshness::Historical;
        } else if (previous && previous->value.freshness == Freshness::Stale &&
                   value.freshness == Freshness::Current) {
            value.freshness = Freshness::Stale;
        }
        if (value.freshness == Freshness::Current &&
            value.age_at_receive_us >= uint64_t{value.lease_ms} * 1000U) value.freshness = Freshness::Stale;
        value.received_at_us = now_us;
    }

    // Keep only evidence still referenced by the committed graph. The sorted
    // order survives compaction, so lookup cost remains bounded on later deltas.
    for (uint32_t i = 0; i < bank.evidence_used; ++i) bank.evidence[i].referenced = false;
    for (uint32_t i = 0; i < bank.used; ++i) {
        for (const auto &fact : bank.rows[i].facts) {
            if (fact.observation != 0) find_evidence(bank, fact.observation)->referenced = true;
        }
    }
    auto *end = std::remove_if(bank.evidence, bank.evidence + bank.evidence_used,
        [](const StoredEvidence &row) { return !row.referenced; });
    bank.evidence_used = static_cast<uint32_t>(end - bank.evidence);
    bank.summary = {reader.request.source, reader.request.epoch, reader.revision,
                    counts[1], counts[2], active, completed};
    bank.committed_at_us = now_us;
    return RejectCode::None;
}


// No parameters: the associated network owner is done with both banks.
StateStore::~StateStore() { clear(); }

// limits: Explicit selected capacity for one source and its two generations.
// allocate: Allocator returning aligned, owner-approved memory or null.
// release: Matching deallocator.
// upgrade_questions: The authenticated negotiation permits replacing a core-only bank.
RejectCode StateStore::configure(const CapacityLimits &limits, StateAllocate allocate,
                                 StateRelease release, bool upgrade_questions)
{
    if (!allocate || !release || !supported_limits(limits)) return RejectCode::UnsupportedCapability;
    if (storage_ && same_limits(storage_->limits, limits) && storage_->allocate == allocate &&
        storage_->release == release) return RejectCode::None;
    if (storage_ && storage_->live().summary.revision != 0 &&
        !(upgrade_questions && storage_->limits.open_questions == 0 && limits.open_questions != 0)) {
        return RejectCode::CapacityExceeded;
    }
    auto *memory = allocate(sizeof(Storage));
    if (!memory) return RejectCode::CapacityExceeded;
    auto *candidate = new (memory) Storage{};
    candidate->limits = limits;
    candidate->allocate = allocate;
    candidate->release = release;
    if (!candidate->allocate_bank(candidate->banks[0]) || !candidate->allocate_bank(candidate->banks[1])) {
        candidate->release_bank(candidate->banks[0]);
        candidate->release_bank(candidate->banks[1]);
        candidate->~Storage();
        release(memory);
        return RejectCode::CapacityExceeded;
    }
    clear();
    storage_ = candidate;
    return RejectCode::None;
}

// request: Exact request admitted by the owner before accepting any state data.
RejectCode StateStore::begin(const StateRequest &request)
{
    if (!storage_) return RejectCode::UnsupportedCapability;
    auto &store = *storage_;
    if (store.pending) return RejectCode::Busy;
    if (!request.source.valid() || !request.epoch.valid() || request.request_id == 0 ||
        request.provider.back() != '\0' || request.native_namespace.back() != '\0') {
        return RejectCode::WrongBinding;
    }
    if (!request.snapshot && (request.base_revision == 0 ||
        request.base_revision != revision(request.source, request.epoch))) return RejectCode::NeedSnapshot;
    auto &bank = store.stage();
    bank.summary = {};
    bank.coverage = {};
    bank.used = 0;
    bank.evidence_used = 0;
    bank.removal_used = 0;
    bank.text_used = 0;
    store.retained_removals = 0;
    store.error = RejectCode::None;
    auto &reader = store.reader;
    reader.request = request;
    reader.revision = reader.base = reader.bytes = reader.members = reader.coverage = 0;
    reader.length = reader.depth = reader.member = 0;
    reader.next = Lex::Open;
    reader.quoted = reader.escaped = reader.collecting = false;
    if (!request.snapshot && !store.clone()) return store.error;
    store.pending = true;
    return RejectCode::None;
}

// bytes: Borrowed authenticated bytes, already in sequence.
// length: Size of this borrowed slice.
RejectCode StateStore::consume(const uint8_t *bytes, uint16_t length)
{
    if (!storage_ || !storage_->pending) return RejectCode::WrongBinding;
    auto &store = *storage_;
    if (store.error != RejectCode::None) return store.error;
    if (!bytes || length == 0 || length > Protocol::kDocumentBytes - store.reader.bytes) {
        return store.error = RejectCode::Malformed;
    }
    for (uint16_t i = 0; i < length; ++i) {
        const auto error = store.feed(static_cast<char>(bytes[i]));
        if (error != RejectCode::None) return store.error = error;
    }
    store.reader.bytes += length;
    return RejectCode::None;
}

// identity: Header metadata from the authenticated, digest-verified transfer.
// request_us: Original send time for the pending request.
// now_us: Same clock's commit-time observation.
RejectCode StateStore::commit(const Protocol::TransferIdentity &identity, uint64_t request_us,
                              uint64_t now_us)
{
    if (!storage_ || !storage_->pending) return RejectCode::WrongBinding;
    auto &store = *storage_;
    const auto &reader = store.reader;
    store.pending = false;
    if (store.error != RejectCode::None) return store.error;
    if (identity.source != reader.request.source || identity.epoch != reader.request.epoch ||
        identity.revision != reader.revision) return RejectCode::WrongBinding;
    if (reader.next != Lex::End || reader.collecting || reader.members != kAllMembers ||
        reader.coverage != (uint16_t{1} << kKinds) - 1 || identity.document_bytes != reader.bytes) {
        return RejectCode::Malformed;
    }
    if (generation_ == std::numeric_limits<uint64_t>::max()) return RejectCode::CapacityExceeded;
    const auto error = store.validate(request_us, now_us);
    if (error == RejectCode::None) {
        store.current = 1U - store.current;
        ++generation_;
    }
    return error;
}

// No parameters: forget the incomplete transfer and leave live readers intact.
void StateStore::cancel()
{
    if (storage_) storage_->pending = false;
}

// No parameters: drop both generations on explicit association replacement.
void StateStore::clear()
{
    if (!storage_) return;
    auto *store = storage_;
    storage_ = nullptr;
    store->release_bank(store->banks[0]);
    store->release_bank(store->banks[1]);
    const auto release = store->release;
    store->~Storage();
    release(store);
}

// source: Exact source from the authenticated session.
// epoch: Its bound epoch.
uint64_t StateStore::revision(SourceId source, EpochId epoch) const
{
    const auto current = summary();
    return current.source == source && current.epoch == epoch ? current.revision : 0;
}

// No parameters: copy the last coherent count and revision summary.
StateSummary StateStore::summary() const
{
    return storage_ ? storage_->live().summary : StateSummary{};
}


namespace {

struct QueryClock {
    uint64_t now_us = 0;
    bool ready = false;
};

// evidence: Exact admitted observation of the field.
// clock: Current receiver clock and link readiness for this bank.
Freshness query_freshness(const Evidence &evidence, QueryClock clock)
{
    if (evidence.freshness != Freshness::Current) return evidence.freshness;
    if (!clock.ready || clock.now_us < evidence.received_at_us) return Freshness::Stale;
    if (evidence.age_state != Availability::Known || evidence.lease_state != Availability::Known) {
        return Freshness::Unknown;
    }
    const auto lease_us = uint64_t{evidence.lease_ms} * 1000U;
    if (evidence.age_at_receive_us >= lease_us) return Freshness::Stale;
    const auto elapsed_us = clock.now_us - evidence.received_at_us;
    return elapsed_us < lease_us - evidence.age_at_receive_us ? Freshness::Current : Freshness::Stale;
}

// bank: One committed generation.
// stored: Field whose availability and original evidence are copied.
// clock: Receiver clock and live binding status.
StateFactView fact_view(const Bank &bank, const StoredFact &stored, QueryClock clock)
{
    StateFactView view{};
    view.availability = stored.state;
    const auto *found = stored.observation ? find_evidence(bank, stored.observation) : nullptr;
    if (!found) return view;
    const auto &evidence = found->value;
    view.freshness = query_freshness(evidence, clock);
    view.provenance = evidence.provenance;
    view.observation = evidence.observation;
    view.observed_at = evidence.observed_at;
    if (evidence.age_state == Availability::Known && clock.now_us >= evidence.received_at_us) {
        const auto elapsed_us = clock.now_us - evidence.received_at_us;
        const auto remaining = std::numeric_limits<uint64_t>::max() - evidence.age_at_receive_us;
        view.age_us = evidence.age_at_receive_us + std::min(elapsed_us, remaining);
        view.age_state = Availability::Known;
    }
    view.error = evidence.error.kind;
    view.original_code = evidence.error.original_code;
    return view;
}

// bank: Committed generation.
// stored: Previously validated scalar field.
// clock: Receiver clock and binding status.
// value: Typed source value. A non-known field exposes its default payload.
template<class T>
StateValueView<T> scalar_view(const Bank &bank, const StoredFact &stored, QueryClock clock, T value)
{
    return {stored.state == Availability::Known ? value : T{}, fact_view(bank, stored, clock)};
}

// bank: Committed text pool.
// ref: Admitted field span, possibly larger than the bounded projection.
// out: Zero-terminated bounded output, initially zeroed by the page owner.
template<std::size_t Size>
void copy_view_text(const Bank &bank, TextRef ref, std::array<char, Size> &out)
{
    const auto text = bank.view(ref);
    const auto count = std::min(text.size(), out.size() - 1);
    std::copy_n(text.begin(), count, out.begin());
    out[count] = '\0';
}

// bank: Committed entity graph.
// row: Exact chat record.
// clock: Query clock and link readiness.
// out: Destination row in the caller-owned page.
void project_chat(const Bank &bank, const Entity &row, QueryClock clock, StateChatRow &out)
{
    const auto source = bank.summary.source;
    out.id = {source, row.key.id};
    out.title.fact = fact_view(bank, row.facts[0], clock);
    if (out.title.fact.availability == Availability::Known) {
        copy_view_text(bank, row.facts[0].text, out.title.value);
    }
    out.lifecycle = scalar_view(bank, row.facts[1], clock, static_cast<ChatLifecycle>(row.facts[1].value));
    out.current_turn = scalar_view(bank, row.facts[2], clock, TurnId{source, row.facts[2].value});
}

// bank: Committed entity graph.
// row: Exact turn record.
// clock: Query clock and link readiness.
// out: Destination row in the caller-owned page.
void project_turn(const Bank &bank, const Entity &row, QueryClock clock, StateTurnRow &out)
{
    const auto source = bank.summary.source;
    out.id = {source, row.key.id};
    out.chat = {source, row.parent};
    out.execution = scalar_view(bank, row.facts[0], clock, static_cast<ExecutionState>(row.facts[0].value));
    out.outcome = scalar_view(bank, row.facts[1], clock, static_cast<Outcome>(row.facts[1].value));
    out.reply = scalar_view(bank, row.facts[2], clock, static_cast<ReplyState>(row.facts[2].value));
}

// bank: Committed entity graph.
// row: Exact tool call record.
// clock: Query clock and link readiness.
// out: Destination row in the caller-owned page.
void project_call(const Bank &bank, const Entity &row, QueryClock clock, StateCallRow &out)
{
    const auto source = bank.summary.source;
    out.id = {source, row.key.id};
    out.turn = {source, row.parent};
    out.definition = {source, row.definition};
    const auto *definition = find_row(bank, {EntityKind::ToolDefinition, row.definition});
    if (definition) {
        copy_view_text(bank, definition->text[0], out.original_name);
        out.label.fact = fact_view(bank, definition->facts[0], clock);
        if (out.label.fact.availability == Availability::Known) {
            copy_view_text(bank, definition->facts[0].text, out.label.value);
        }
        out.category = scalar_view(bank, definition->facts[1], clock,
                                   static_cast<ToolCategory>(definition->facts[1].value));
    }
    out.state = scalar_view(bank, row.facts[1], clock, static_cast<CallState>(row.facts[1].value));
    out.outcome = scalar_view(bank, row.facts[2], clock, static_cast<Outcome>(row.facts[2].value));
}

// bank: Committed entity graph.
// row: Exact semantic question, independent of its signed reply ticket.
// clock: Query clock and link readiness.
// out: Destination row in the caller-owned pending-question page.
void project_question(const Bank &bank, const Entity &row, QueryClock clock, StateQuestionRow &out)
{
    const auto source = bank.summary.source;
    out.id = {source, row.key.id};
    out.chat = scalar_view(bank, row.facts[QuestionChat], clock,
                           ChatId{source, row.facts[QuestionChat].value});
    out.turn = scalar_view(bank, row.facts[QuestionTurn], clock,
                           TurnId{source, row.facts[QuestionTurn].value});
    const auto &prompt = row.facts[QuestionPrompt];
    out.prompt.fact = fact_view(bank, prompt, clock);
    if (prompt.state == Availability::Known) {
        copy_view_text(bank, prompt.text, out.prompt.value);
    }
    if (out.chat.value.valid()) {
        const auto *chat = find_row(bank, {EntityKind::Chat, out.chat.value.value});
        if (chat && chat->facts[0].state == Availability::Known &&
            fact_view(bank, chat->facts[0], clock).freshness == Freshness::Current) {
            copy_view_text(bank, chat->facts[0].text, out.chat_title);
        }
    }
    out.state = scalar_view(bank, row.facts[QuestionStatus], clock,
                            static_cast<QuestionState>(row.facts[QuestionStatus].value));
}

// bank: Committed generation. Coverage counts alone do not establish freshness.
// clock: Query clock and link readiness.
StateAttention attention_view(const Bank &bank, QueryClock clock)
{
    StateAttention result{};
    const auto *source = find_row(bank, {EntityKind::Source, bank.summary.source.value});
    if (source) result.questions = source->caps[kQuestionCapability];
    if (!clock.ready) return result;
    const auto complete = [&](EntityKind kind) {
        return bank.coverage[static_cast<uint8_t>(kind)].state == CoverageState::Complete;
    };
    result.turns_complete = complete(EntityKind::Turn);
    result.calls_complete = complete(EntityKind::ToolCall);
    result.questions_complete = result.questions == Capability::Supported && complete(EntityKind::Question);
    for (uint32_t index = 0; index < bank.used; ++index) {
        const auto &row = bank.rows[index];
        if (row.key.kind == EntityKind::Turn) {
            const auto fact = fact_view(bank, row.facts[0], clock);
            const auto state = static_cast<ExecutionState>(row.facts[0].value);
            if (fact.availability != Availability::Known || fact.freshness != Freshness::Current ||
                state == ExecutionState::Unknown) {
                result.turns_complete = false;
            } else if (state == ExecutionState::Running) {
                ++result.running_turns;
            } else if (state == ExecutionState::Waiting) {
                ++result.waiting_turns;
            }
        } else if (row.key.kind == EntityKind::ToolCall) {
            // A terminal call ID cannot return to an active state. Its age
            // does not make the live active-call aggregate incomplete.
            if (terminal(row)) continue;
            const auto fact = fact_view(bank, row.facts[1], clock);
            const auto state = static_cast<CallState>(row.facts[1].value);
            if (fact.availability != Availability::Known || fact.freshness != Freshness::Current ||
                state == CallState::Unknown) {
                result.calls_complete = false;
            } else if (state == CallState::Running || state == CallState::Queued) {
                ++result.active_calls;
            }
        } else if (row.key.kind == EntityKind::Question && !terminal(row)) {
            const auto fact = fact_view(bank, row.facts[QuestionStatus], clock);
            if (fact.freshness != Freshness::Current) result.questions_complete = false;
            else ++result.pending_questions;
        }
    }
    return result;
}

// cursor: All zero identity fields denote a first page.
bool first_page(const StateCursor &cursor)
{
    return !cursor.source.valid() && !cursor.epoch.valid() && cursor.revision == 0 &&
        cursor.generation == 0 && cursor.after == 0 && cursor.kind == StateQueryKind::Summary &&
        cursor.chat == ChatId{} && cursor.turn == TurnId{};
}

// row: Candidate from the requested entity kind.
// request: Identity scope and optional live-state filter for the page.
bool in_scope(const Entity &row, const StateQuery &request)
{
    switch (request.kind) {
    case StateQueryKind::Chats:
        return true;
    case StateQueryKind::Turns:
        return row.parent == request.chat.value;
    case StateQueryKind::Calls:
        return row.parent == request.turn.value;
    case StateQueryKind::ActiveCalls: {
        const auto state = static_cast<CallState>(row.facts[1].value);
        return row.facts[1].state == Availability::Known &&
            (state == CallState::Queued || state == CallState::Running);
    }
    case StateQueryKind::PendingQuestions:
        return !terminal(row) &&
            (!request.chat.valid() || row.facts[QuestionChat].value == request.chat.value) &&
            (!request.turn.valid() || row.facts[QuestionTurn].value == request.turn.value);
    default:
        return false;
    }
}

// bank: Immutable committed generation.
// request: Exact kind and identity scope for the page.
// kind: Internal row type chosen for this query.
// clock: Query time and link status.
// out: Destination page already emplaced into the result.
// next: Receives the next cursor only when another matching row exists.
// generation: Local commit stamp, distinct from the broker's revision.
// project: Typed row copier.
template<class T, class Project>
void copy_page(const Bank &bank, const StateQuery &request, EntityKind kind, QueryClock clock,
               StatePage<T> &out, StateCursor &next, uint64_t generation, Project project)
{
    const auto *end = bank.rows + bank.used;
    const auto *row = std::upper_bound(static_cast<const Entity *>(bank.rows), end,
        Key{kind, request.cursor.after}, [](Key key, const Entity &candidate) {
            return key < candidate.key;
        });
    for (; row != end && row->key.kind == kind; ++row) {
        if (!in_scope(*row, request)) continue;
        if (out.count == kStateQueryPageSize) {
            next = {bank.summary.source, bank.summary.epoch, bank.summary.revision, generation,
                    out.rows[out.count - 1].id.value, request.kind, request.chat, request.turn};
            return;
        }
        project(bank, *row, clock, out.rows[out.count]);
        ++out.count;
    }
}

} // namespace

// request: Exact single entity or bounded page for a stable identity scope.
// now_us: Current receiver clock reading. A regressed clock cannot refresh facts.
// link_ready: Verified lease for the source and epoch of the committed generation.
// result: Owned projection. Callers copy it under the same lock as the CCP owner.
RejectCode StateStore::query(const StateQuery &request, uint64_t now_us, bool link_ready,
                             StateQueryResult &result) const
{
    result.reset();
    if (!storage_) return RejectCode::NeedSnapshot;
    const auto &bank = storage_->live();
    if (bank.summary.revision == 0) return RejectCode::NeedSnapshot;
    const QueryClock clock{now_us, link_ready && now_us >= bank.committed_at_us};
    result.summary = bank.summary;
    result.health = clock.ready ? StateViewHealth::Live : StateViewHealth::Stale;
    result.generation = generation_;
    result.coverage = bank.coverage;

    const auto source = bank.summary.source;
    const auto &cursor = request.cursor;
    if (request.kind != StateQueryKind::Summary && request.kind != StateQueryKind::Chat &&
        request.kind != StateQueryKind::Turn && !first_page(cursor) && (cursor.source != source || cursor.epoch != bank.summary.epoch ||
        cursor.revision != bank.summary.revision || cursor.generation != generation_)) {
        return RejectCode::NeedSnapshot;
    }
    EntityKind kind{};
    switch (request.kind) {
    case StateQueryKind::Summary:
        if (!first_page(request.cursor) || request.chat != ChatId{} || request.turn != TurnId{}) {
            return RejectCode::Malformed;
        }
        result.attention = attention_view(bank, clock);
        return RejectCode::None;
    case StateQueryKind::Chat: {
        if (!first_page(request.cursor) || request.turn != TurnId{} || !request.chat.valid()) {
            return RejectCode::Malformed;
        }
        if (request.chat.source != source) return RejectCode::WrongBinding;
        const auto *row = find_row(bank, {EntityKind::Chat, request.chat.value});
        if (!row) return RejectCode::MissingReference;
        auto &page = result.page.emplace<StateChatPage>();
        project_chat(bank, *row, clock, page.rows[0]);
        page.count = 1;
        return RejectCode::None;
    }
    case StateQueryKind::Turn: {
        if (!first_page(cursor) || request.chat != ChatId{} || !request.turn.valid()) {
            return RejectCode::Malformed;
        }
        if (request.turn.source != source) return RejectCode::WrongBinding;
        const auto *row = find_row(bank, {EntityKind::Turn, request.turn.value});
        if (!row) return RejectCode::MissingReference;
        auto &page = result.page.emplace<StateTurnPage>();
        project_turn(bank, *row, clock, page.rows[0]);
        page.count = 1;
        return RejectCode::None;
    }
    case StateQueryKind::Chats:
        if (request.chat != ChatId{} || request.turn != TurnId{}) return RejectCode::Malformed;
        kind = EntityKind::Chat;
        break;
    case StateQueryKind::Turns:
        if (!request.chat.valid() || request.turn != TurnId{}) return RejectCode::Malformed;
        if (request.chat.source != source) return RejectCode::WrongBinding;
        if (!find_row(bank, {EntityKind::Chat, request.chat.value})) return RejectCode::MissingReference;
        kind = EntityKind::Turn;
        break;
    case StateQueryKind::Calls:
        if (!request.turn.valid() || request.chat != ChatId{}) return RejectCode::Malformed;
        if (request.turn.source != source) return RejectCode::WrongBinding;
        if (!find_row(bank, {EntityKind::Turn, request.turn.value})) return RejectCode::MissingReference;
        kind = EntityKind::ToolCall;
        break;
    case StateQueryKind::ActiveCalls:
        if (request.chat != ChatId{} || request.turn != TurnId{}) return RejectCode::Malformed;
        kind = EntityKind::ToolCall;
        break;
    case StateQueryKind::PendingQuestions: {
        kind = EntityKind::Question;
        if (bank.coverage[static_cast<uint8_t>(kind)].state == CoverageState::Unsupported) {
            return RejectCode::UnsupportedCapability;
        }
        if ((request.chat != ChatId{} && !request.chat.valid()) ||
            (request.turn != TurnId{} && !request.turn.valid())) return RejectCode::Malformed;
        if ((request.chat.valid() && request.chat.source != source) ||
            (request.turn.valid() && request.turn.source != source)) return RejectCode::WrongBinding;
        if (request.chat.valid() && !find_row(bank, {EntityKind::Chat, request.chat.value})) {
            return RejectCode::MissingReference;
        }
        if (request.turn.valid()) {
            const auto *turn = find_row(bank, {EntityKind::Turn, request.turn.value});
            if (!turn || (request.chat.valid() && turn->parent != request.chat.value)) {
                return RejectCode::MissingReference;
            }
        }
        break;
    }
    default:
        return RejectCode::UnsupportedCapability;
    }

    if (!first_page(cursor)) {
        if (cursor.kind != request.kind || cursor.chat != request.chat || cursor.turn != request.turn ||
            cursor.after == 0) return RejectCode::Malformed;
        const auto *anchor = find_row(bank, {kind, cursor.after});
        if (!anchor || !in_scope(*anchor, request)) return RejectCode::MissingReference;
    }

    switch (kind) {
    case EntityKind::Chat:
        copy_page(bank, request, kind, clock, result.page.emplace<StateChatPage>(), result.next,
                  generation_, project_chat);
        return RejectCode::None;
    case EntityKind::Turn:
        copy_page(bank, request, kind, clock, result.page.emplace<StateTurnPage>(), result.next,
                  generation_, project_turn);
        return RejectCode::None;
    case EntityKind::ToolCall:
        copy_page(bank, request, kind, clock, result.page.emplace<StateCallPage>(), result.next,
                  generation_, project_call);
        return RejectCode::None;
    case EntityKind::Question:
        copy_page(bank, request, kind, clock, result.page.emplace<StateQuestionPage>(), result.next,
                  generation_, project_question);
        return RejectCode::None;
    default:
        return RejectCode::UnsupportedCapability;
    }
}

} // namespace CODY::Domain

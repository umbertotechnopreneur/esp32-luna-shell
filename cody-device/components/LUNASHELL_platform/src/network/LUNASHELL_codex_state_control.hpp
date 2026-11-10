/* Copyright (c) 2026 Umberto Giacobbi */
#pragma once

#include "CODY_domain_capacity.hpp"
#include "CODY_state_protocol.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <string_view>

namespace LUNASHELL::codex_state {

using CODY::Domain::CapacityLimits;
using CODY::Domain::Protocol::RejectCode;
using std::string_view;

class ControlJson {
public:
    enum class Kind : uint8_t { Object, Array, String, Number, Null };
    struct Node {
        string_view key{};
        string_view text{};
        uint32_t number = 0;
        uint16_t next = 0;
        Kind kind = Kind::Null;
    };

    // bytes: Mutable verified control document. String views borrow decoded text.
    // size: Exact document length; the parser never depends on a trailing NUL.
    const Node *parse(char *bytes, size_t size)
    {
        cursor_ = bytes;
        end_ = bytes + size;
        count_ = 0;
        if (!value({}, 0)) return nullptr;
        spaces();
        return cursor_ == end_ ? nodes_.data() : nullptr;
    }

    // object: Parsed object.
    // key: Exact unique member name.
    const Node *get(const Node *object, string_view key) const
    {
        if (!object || object->kind != Kind::Object) return nullptr;
        for (const Node *item = object + 1; item < nodes_.data() + object->next;
             item = nodes_.data() + item->next) {
            if (item->key == key) return item;
        }
        return nullptr;
    }

    // object: Object whose members must match the allowlist exactly.
    // keys: Required member names. Duplicate keys are already rejected.
    bool exact(const Node *object, std::initializer_list<string_view> keys) const
    {
        if (!object || object->kind != Kind::Object || children(object) != keys.size()) return false;
        for (const auto key : keys) if (!get(object, key)) return false;
        return true;
    }

    // parent: Parsed array or object.
    size_t children(const Node *parent) const
    {
        size_t count = 0;
        for (auto *item = parent + 1; item < nodes_.data() + parent->next;
             item = nodes_.data() + item->next) ++count;
        return count;
    }

    // parent: Parsed array.
    // index: Zero-based item index.
    const Node *item(const Node *parent, size_t index) const
    {
        if (!parent || parent->kind != Kind::Array) return nullptr;
        for (const auto *child = parent + 1; child < nodes_.data() + parent->next;
             child = nodes_.data() + child->next) {
            if (index-- == 0) return child;
        }
        return nullptr;
    }

private:
    // No parameters: skip only the whitespace allowed by JSON.
    void spaces()
    {
        while (cursor_ != end_ && (*cursor_ == ' ' || *cursor_ == '\t' ||
               *cursor_ == '\r' || *cursor_ == '\n')) ++cursor_;
    }

    // expected: Next punctuation byte to admit after whitespace.
    bool take(char expected)
    {
        spaces();
        if (cursor_ == end_ || *cursor_ != expected) return false;
        ++cursor_;
        return true;
    }

    // output: Decoded printable ASCII, using no more space than the original.
    bool string(string_view &output)
    {
        if (!take('"')) return false;
        char *const start = cursor_;
        char *decoded = start;
        while (cursor_ != end_) {
            char byte = *cursor_++;
            if (byte == '"') {
                output = {start, static_cast<size_t>(decoded - start)};
                return true;
            }
            if (byte == '\\') {
                if (cursor_ == end_) return false;
                byte = *cursor_++;
                if (byte == 'u') {
                    if (end_ - cursor_ < 4) return false;
                    uint32_t code = 0;
                    for (unsigned i = 0; i < 4; ++i) {
                        const char digit = *cursor_++;
                        unsigned nibble;
                        if (digit >= '0' && digit <= '9') nibble = digit - '0';
                        else if (digit >= 'a' && digit <= 'f') nibble = digit - 'a' + 10;
                        else if (digit >= 'A' && digit <= 'F') nibble = digit - 'A' + 10;
                        else return false;
                        code = (code << 4U) | nibble;
                    }
                    if (code < 32 || code > 126) return false;
                    byte = static_cast<char>(code);
                } else if (byte != '"' && byte != '\\' && byte != '/') {
                    return false;
                }
            }
            if (byte < 32 || byte > 126) return false;
            *decoded++ = byte;
        }
        return false;
    }

    // node: Current value to populate with a bounded uint32.
    bool number(Node &node)
    {
        const char *const first = cursor_;
        uint32_t result = 0;
        while (cursor_ != end_ && *cursor_ >= '0' && *cursor_ <= '9') {
            const uint32_t digit = *cursor_++ - '0';
            if (result > (UINT32_MAX - digit) / 10U) return false;
            result = result * 10U + digit;
        }
        if (cursor_ - first > 1 && *first == '0') return false;
        node.kind = Kind::Number;
        node.number = result;
        return true;
    }

    // index: Node reserved for this container.
    // opener: Object or array opening punctuation already admitted.
    // depth: Number of ancestor containers.
    bool container(uint16_t index, char opener, unsigned depth)
    {
        if (depth >= 8) return false;
        const bool object = opener == '{';
        const char closer = object ? '}' : ']';
        nodes_[index].kind = object ? Kind::Object : Kind::Array;
        ++cursor_;
        if (take(closer)) return true;
        do {
            string_view key;
            if (object) {
                if (!string(key) || !take(':')) return false;
                for (size_t i = index + 1; i < count_; i = nodes_[i].next) {
                    if (nodes_[i].key == key) return false;
                }
            }
            if (!value(key, depth + 1)) return false;
            if (take(closer)) return true;
        } while (take(','));
        return false;
    }

    // key: Parent object's unique decoded member name, or empty for arrays/root.
    // depth: Number of ancestor containers, bounded before each recursive descent.
    bool value(string_view key, unsigned depth)
    {
        spaces();
        if (cursor_ == end_ || count_ == nodes_.size()) return false;
        const uint16_t index = count_++;
        auto &node = nodes_[index];
        node = {};
        node.key = key;
        const char byte = *cursor_;
        if (byte == '{' || byte == '[') {
            if (!container(index, byte, depth)) return false;
        } else if (byte == '"') {
            node.kind = Kind::String;
            if (!string(node.text)) return false;
        } else if (byte >= '0' && byte <= '9') {
            if (!number(node)) return false;
        } else if (end_ - cursor_ >= 4 && string_view(cursor_, 4) == "null") {
            cursor_ += 4;
        } else {
            return false;
        }
        node.next = count_;
        return true;
    }

    // A four-binding offer uses fewer than 100 nodes. The fixed table bounds
    // work even for hostile authenticated input and stays in network-owner RAM.
    std::array<Node, 128> nodes_{};
    char *cursor_ = nullptr;
    char *end_ = nullptr;
    uint16_t count_ = 0;
};

using JsonNode = ControlJson::Node;

// node: Parsed control value.
// text: Exact string value expected.
inline bool string_is(const JsonNode *node, string_view text)
{
    return node && node->kind == ControlJson::Kind::String && node->text == text;
}

// node: Parsed control string.
// minimum: Minimum decoded byte count.
// maximum: Maximum decoded byte count.
inline bool string_range(const JsonNode *node, size_t minimum, size_t maximum)
{
    return node && node->kind == ControlJson::Kind::String &&
        node->text.size() >= minimum && node->text.size() <= maximum;
}

// node: Exact 16-digit lowercase hexadecimal ID or revision.
// value: Destination on success; caller applies zero/nonzero semantics.
inline bool hex_u64(const JsonNode *node, uint64_t &value)
{
    if (!string_range(node, 16, 16)) return false;
    uint64_t result = 0;
    for (const char digit : node->text) {
        uint64_t nibble;
        if (digit >= '0' && digit <= '9') nibble = digit - '0';
        else if (digit >= 'a' && digit <= 'f') nibble = digit - 'a' + 10;
        else return false;
        result = (result << 4U) | nibble;
    }
    value = result;
    return true;
}

struct LimitField {
    const char *name;
    uint32_t CapacityLimits::*value;
    uint32_t maximum;
};

inline constexpr LimitField kLimitFields[] = {
    {"sources", &CapacityLimits::sources, 4}, {"chats", &CapacityLimits::chats, 64},
    {"turns", &CapacityLimits::turns, 4096}, {"tool_definitions", &CapacityLimits::tool_definitions, 512},
    {"active_calls", &CapacityLimits::active_calls, 4096},
    {"terminal_calls", &CapacityLimits::terminal_calls, 4096},
    {"agents", &CapacityLimits::agents, 4096}, {"operations", &CapacityLimits::operations, 4096},
    {"open_questions", &CapacityLimits::open_questions, 1024},
    {"resolved_questions", &CapacityLimits::resolved_questions, 1024},
    {"choices", &CapacityLimits::choices, 6144}, {"answer_attempts", &CapacityLimits::answer_attempts, 4096},
    {"waits", &CapacityLimits::waits, 8192}, {"evidence", &CapacityLimits::evidence, 65536},
    {"tombstones", &CapacityLimits::tombstones, 8192}, {"text_bytes", &CapacityLimits::text_bytes, 2097152},
    {"decode_scratch_bytes", &CapacityLimits::decode_scratch_bytes, 1048576},
    {"persistence_scratch_bytes", &CapacityLimits::persistence_scratch_bytes, 1048576},
};

// parser: Owner of the parsed JSON document.
// node: Candidate exact limit object.
// limits: Decoded limits on success; never used after an error.
inline bool read_limits(const ControlJson &parser, const JsonNode *node, CapacityLimits &limits)
{
    if (!node || node->kind != ControlJson::Kind::Object ||
        parser.children(node) != std::size(kLimitFields)) return false;
    for (const auto &field : kLimitFields) {
        const auto *entry = parser.get(node, field.name);
        if (!entry || entry->kind != ControlJson::Kind::Number || entry->number > field.maximum) return false;
        limits.*field.value = entry->number;
    }
    return true;
}

// left: Negotiated aggregate limits.
// right: Exact limits echoed by the acceptance document.
inline bool same_limits(const CapacityLimits &left, const CapacityLimits &right)
{
    return std::all_of(std::begin(kLimitFields), std::end(kLimitFields), [&](const auto &field) {
        return left.*field.value == right.*field.value;
    });
}

} // namespace LUNASHELL::codex_state

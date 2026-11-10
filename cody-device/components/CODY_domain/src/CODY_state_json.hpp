
/* Copyright (c) 2026 Umberto Giacobbi */
#pragma once

#include <array>
#include <cstdint>
#include <initializer_list>
#include <string_view>

namespace CODY::Domain::StateJson {

enum class Kind : uint8_t { Object, Array, String, Number, Null, Boolean };

struct Node {
    Kind kind{};
    uint16_t start = 0;
    uint16_t size = 0;
    uint16_t next = 0;
    uint32_t number = 0;
};

// One complete bounded record. Decoded strings borrow its scratch bytes.
class Record {
public:
    // bytes: Mutable scratch containing exactly one JSON value.
    // size: Number of used bytes, no terminator required.
    bool parse(char *bytes, uint16_t size);

    // node: Object whose exact keys are listed in names.
    // names: Required names. Every key must occur once, with no unknown keys.
    bool fields(uint16_t node, std::initializer_list<std::string_view> names) const;

    // object: Object node containing alternating name/value children.
    // name: Decoded field name; returns zero if absent.
    uint16_t field(uint16_t object, std::string_view name) const;

    // node: String node. Call is_string first when empty strings are valid.
    std::string_view string(uint16_t node) const;

    // node: Parsed node index, zero is the missing sentinel.
    const Node &operator[](uint16_t node) const { return nodes_[node]; }

    // node: Parsed node index, zero is the missing sentinel.
    bool is_string(uint16_t node) const { return node != 0 && nodes_[node].kind == Kind::String; }

    // node: Parsed node index, zero is the missing sentinel.
    bool is_null(uint16_t node) const { return node != 0 && nodes_[node].kind == Kind::Null; }

    // node: Parsed node index, zero is the missing sentinel.
    bool is_number(uint16_t node) const { return node != 0 && nodes_[node].kind == Kind::Number; }

private:
    // depth: Number of parent containers, capped at seven because the envelope is the eighth.
    uint16_t value(uint8_t depth);

    // node: Allocated node to fill with a decoded printable-ASCII string.
    bool quoted(Node &node);

    // node: Allocated node to fill with an unsigned bounded integer.
    bool integer(Node &node);

    // No parameters: consumes the four JSON whitespace characters.
    void skip();

    char *bytes_ = nullptr;
    uint16_t size_ = 0;
    uint16_t pos_ = 0;
    uint16_t used_ = 0;
    std::array<Node, 96> nodes_{};
};

// ch: JSON lexical whitespace, not arbitrary control characters.
constexpr bool space(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}

// ch: ASCII hexadecimal digit; upper case is only accepted for JSON escapes.
int digit(char ch, bool uppercase = false);

// text: Exactly sixteen lowercase hexadecimal characters.
bool hex64(std::string_view text, uint64_t &value);

// text: Canonical signed decimal int64 value encoded as a JSON string.
// result: Written only after overflow, sign and leading-zero checks.
bool int64_string(std::string_view text, int64_t &result);

} // namespace CODY::Domain::StateJson

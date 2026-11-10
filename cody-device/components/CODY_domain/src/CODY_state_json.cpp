
/* Copyright (c) 2026 Umberto Giacobbi */
#include "CODY_state_json.hpp"

#include <limits>

namespace CODY::Domain::StateJson {

// ch: ASCII hexadecimal digit.
// uppercase: Accept uppercase digits only while decoding an escape.
int digit(char ch, bool uppercase)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (uppercase && ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

// text: Exactly sixteen lowercase hexadecimal characters.
// value: Parsed value.
bool hex64(std::string_view text, uint64_t &value)
{
    if (text.size() != 16) return false;
    uint64_t result = 0;
    for (const auto ch : text) {
        const auto d = digit(ch);
        if (d < 0) return false;
        result = (result << 4U) | static_cast<uint64_t>(d);
    }
    value = result;
    return true;
}

// text: Canonical signed decimal int64 encoded as a string.
// result: Exact parsed signed code.
bool int64_string(std::string_view text, int64_t &result)
{
    if (text.empty()) return false;
    const bool negative = text.front() == '-';
    if (negative) text.remove_prefix(1);
    if (text.empty() || (text.front() == '0' && (negative || text.size() != 1))) return false;
    const uint64_t bound = uint64_t{std::numeric_limits<int64_t>::max()} + (negative ? 1U : 0U);
    uint64_t magnitude = 0;
    for (const auto ch : text) {
        if (ch < '0' || ch > '9') return false;
        const auto d = static_cast<uint64_t>(ch - '0');
        if (magnitude > (bound - d) / 10U) return false;
        magnitude = magnitude * 10U + d;
    }
    result = negative && magnitude == bound ? std::numeric_limits<int64_t>::min()
        : (negative ? -static_cast<int64_t>(magnitude) : static_cast<int64_t>(magnitude));
    return true;
}

// bytes: Mutable bounded scratch for one value.
// size: Length, excluding any terminator.
bool Record::parse(char *bytes, uint16_t size)
{
    bytes_ = bytes;
    size_ = size;
    pos_ = 0;
    used_ = 1;
    if (!value(0)) return false;
    skip();
    return pos_ == size_;
}

// No parameters: skips only JSON lexical whitespace.
void Record::skip()
{
    while (pos_ < size_ && space(bytes_[pos_])) ++pos_;
}

// node: Destination string span. Unescaping in place cannot overrun input.
bool Record::quoted(Node &node)
{
    node.kind = Kind::String;
    node.start = ++pos_;
    while (pos_ < size_) {
        auto ch = static_cast<unsigned char>(bytes_[pos_++]);
        if (ch == '"') return true;
        if (ch == '\\') {
            if (pos_ == size_) return false;
            ch = static_cast<unsigned char>(bytes_[pos_++]);
            if (ch == 'u') {
                if (size_ - pos_ < 4) return false;
                int code = 0;
                for (int i = 0; i < 4; ++i) {
                    const auto d = digit(bytes_[pos_++], true);
                    if (d < 0) return false;
                    code = code * 16 + d;
                }
                if (code < 32 || code > 126) return false;
                ch = static_cast<unsigned char>(code);
            } else if (ch != '"' && ch != '\\' && ch != '/') {
                return false;
            }
        }
        if (ch < 32 || ch > 126 || node.size == 1024) return false;
        bytes_[node.start + node.size++] = static_cast<char>(ch);
    }
    return false;
}

// node: Destination unsigned integer. Other number representations are unsupported.
bool Record::integer(Node &node)
{
    node.kind = Kind::Number;
    const bool zero = bytes_[pos_] == '0';
    do {
        const auto d = static_cast<uint32_t>(bytes_[pos_++] - '0');
        if (node.number > (UINT32_MAX - d) / 10U) return false;
        node.number = node.number * 10U + d;
        if (pos_ == size_ || bytes_[pos_] < '0' || bytes_[pos_] > '9') return true;
    } while (!zero);
    return false;
}

// depth: Number of parent containers within the bounded record.
uint16_t Record::value(uint8_t depth)
{
    skip();
    if (pos_ == size_ || used_ == nodes_.size() || depth > 6) return 0;
    const auto index = used_++;
    auto &node = nodes_[index];
    node = {};
    const char first = bytes_[pos_];
    if (first == '"') {
        if (!quoted(node)) return 0;
    } else if (first >= '0' && first <= '9') {
        if (!integer(node)) return 0;
    } else if (first == '{' || first == '[') {
        const bool object = first == '{';
        const char close = object ? '}' : ']';
        node.kind = object ? Kind::Object : Kind::Array;
        ++pos_;
        skip();
        if (pos_ == size_) return 0;
        if (bytes_[pos_] != close) {
            for (;;) {
                if (object) {
                    if (bytes_[pos_] != '"' || !value(depth + 1)) return 0;
                    skip();
                    if (pos_ == size_ || bytes_[pos_++] != ':') return 0;
                }
                if (!value(depth + 1)) return 0;
                skip();
                if (pos_ == size_) return 0;
                if (bytes_[pos_] == close) break;
                if (bytes_[pos_++] != ',') return 0;
                skip();
                if (pos_ == size_) return 0;
            }
        }
        ++pos_;
    } else {
        const std::string_view rest(bytes_ + pos_, size_ - pos_);
        if (rest.substr(0, 4) == "null") {
            node.kind = Kind::Null;
            pos_ += 4;
        } else if (rest.substr(0, 4) == "true" || rest.substr(0, 5) == "false") {
            node.kind = Kind::Boolean;
            pos_ += first == 't' ? 4 : 5;
        } else {
            return 0;
        }
    }
    node.next = used_;
    return index;
}

// object: Object node containing name/value children.
// name: Required decoded member name.
uint16_t Record::field(uint16_t object, std::string_view name) const
{
    if (object == 0 || nodes_[object].kind != Kind::Object) return 0;
    for (uint16_t i = object + 1; i < nodes_[object].next; i = nodes_[i + 1].next) {
        if (string(i) == name) return i + 1;
    }
    return 0;
}

// node: Object whose exact fields are checked.
// names: Expected unique names, all required.
bool Record::fields(uint16_t node, std::initializer_list<std::string_view> names) const
{
    if (node == 0 || nodes_[node].kind != Kind::Object) return false;
    std::size_t count = 0;
    for (uint16_t i = node + 1; i < nodes_[node].next; i = nodes_[i + 1].next) ++count;
    if (count != names.size()) return false;
    for (const auto name : names) if (!field(node, name)) return false;
    return true;
}

// node: Index of a decoded string, or zero when missing.
std::string_view Record::string(uint16_t node) const
{
    if (!is_string(node)) return {};
    return {bytes_ + nodes_[node].start, nodes_[node].size};
}

} // namespace CODY::Domain::StateJson

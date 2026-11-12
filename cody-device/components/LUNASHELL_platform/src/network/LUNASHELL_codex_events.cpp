/* VBWR B
 *
 * Project: ESP32 Luna Shell
 * Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
 * Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
 *
 * VibeWare: Human intent, AI execution, and plenty of tokens
 * Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
 *
 * Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
 * Human guidance: Umberto Giacobbi; requested VibeWare branding.
 *
 * Copyright (c) 2026 Umberto Giacobbi
 *
 * VBWR E */

#include "LUNASHELL_codex_events.hpp"

#include <cstring>

namespace LUNASHELL {
namespace {
struct PayloadReader {
    const char *hex;
    size_t length;
    size_t offset = 0;
    esp_err_t error = ESP_OK;

    // output: Receives one byte only when two valid lowercase hex digits remain.
    bool byte(uint8_t &output)
    {
        if (error != ESP_OK) {
            return false;
        }
        if (offset + 2 > length) {
            error = ESP_ERR_INVALID_SIZE;
            return false;
        }
        output = 0;
        for (size_t digit = 0; digit < 2; ++digit) {
            const char value = hex[offset++];
            uint8_t nibble = 0;
            if (value >= '0' && value <= '9') {
                nibble = static_cast<uint8_t>(value - '0');
            } else if (value >= 'a' && value <= 'f') {
                nibble = static_cast<uint8_t>(value - 'a' + 10);
            } else {
                error = ESP_ERR_INVALID_ARG;
                return false;
            }
            output = static_cast<uint8_t>((output << 4U) | nibble);
        }
        return true;
    }

    // output: Fixed-capacity destination, including its terminating NUL.
    // required: True when an empty authored value cannot be displayed unambiguously.
    template <size_t Capacity>
    bool text(char (&output)[Capacity], bool required)
    {
        uint8_t count = 0;
        if (!byte(count)) {
            return false;
        }
        if (count >= Capacity || (required && count == 0)) {
            error = count >= Capacity ? ESP_ERR_INVALID_SIZE : ESP_ERR_INVALID_ARG;
            return false;
        }
        bool visible = false;
        for (uint8_t index = 0; index < count; ++index) {
            uint8_t value = 0;
            if (!byte(value)) {
                return false;
            }
            if (value < 32 || value > 126) {
                error = ESP_ERR_INVALID_ARG;
                return false;
            }
            output[index] = static_cast<char>(value);
            visible = visible || value != ' ';
        }
        output[count] = '\0';
        if (required && !visible) {
            error = ESP_ERR_INVALID_ARG;
            return false;
        }
        return true;
    }

    // output: Receives a big-endian unsigned progress count.
    bool word(uint16_t &output)
    {
        uint8_t high = 0;
        uint8_t low = 0;
        if (!byte(high) || !byte(low)) {
            return false;
        }
        output = static_cast<uint16_t>((static_cast<uint16_t>(high) << 8U) | low);
        return true;
    }
};
} // namespace

// text: Decrypted bounded answer, not logged or displayed.
// length: Exact UTF-8 byte count before the terminator.
// Exception: None; returns false for malformed, overlong or control characters.
bool validCodexReplySpeech(const char *text, size_t length)
{
    if (text == nullptr || length == 0 || length > kCodexReplySpeechBytes) return false;
    size_t offset = 0;
    bool visible = false;
    while (offset < length) {
        const uint8_t first = static_cast<uint8_t>(text[offset++]);
        if (first < 0x80) {
            if (first < 0x20 || first == 0x7f) return false;
            visible = visible || first != ' ';
            continue;
        }
        unsigned continuation = 0;
        uint32_t value = 0;
        uint32_t minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) { continuation = 1; value = first & 0x1f; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { continuation = 2; value = first & 0x0f; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { continuation = 3; value = first & 7; minimum = 0x10000; }
        else return false;
        if (continuation > length - offset) return false;
        for (unsigned index = 0; index < continuation; ++index) {
            const uint8_t next = static_cast<uint8_t>(text[offset++]);
            if ((next & 0xc0) != 0x80) return false;
            value = (value << 6) | (next & 0x3f);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff) ||
            (value >= 0x80 && value <= 0x9f)) return false;
        visible = true;
    }
    return visible;
}

// hex: Complete authenticated INBOX summary body.
// length: Exact length in ASCII bytes.
// page: Network-owned scratch; publish only after the complete page validates.
// Exception: Returns original bounds and schema errors.
esp_err_t decodeCodexInboxPage(const char *hex, size_t length, CodexInboxPage &page)
{
    constexpr size_t max_bytes = 3 + kCodexInboxRows * (1 + 16 + 1 + 63 + 1 + 63);
    if (hex == nullptr || length < 6 || length > 2 * max_bytes || length % 2 != 0) {
        return ESP_ERR_INVALID_SIZE;
    }
    PayloadReader reader{hex, length};
    page = {};
    if (!reader.byte(page.total) || !reader.byte(page.offset) || !reader.byte(page.count)) return reader.error;
    const uint8_t max_offset = page.total > kCodexInboxRows ? page.total - kCodexInboxRows : 0;
    const uint8_t expected = page.total < kCodexInboxRows ? page.total : kCodexInboxRows;
    if (page.total > kCodexInboxCapacity || page.offset > max_offset || page.count != expected) {
        return ESP_ERR_INVALID_ARG;
    }
    constexpr char digits[] = "0123456789abcdef";
    bool seen_reply = false;
    for (uint8_t index = 0; index < page.count; ++index) {
        auto &row = page.rows[index];
        uint8_t kind = 0;
        if (!reader.byte(kind)) return reader.error;
        row.kind = static_cast<CodexEventKind>(kind);
        if (row.kind == CodexEventKind::ReplyReady) seen_reply = true;
        else if (row.kind != CodexEventKind::Question || seen_reply) return ESP_ERR_INVALID_ARG;
        bool nonzero = false;
        for (size_t byte = 0; byte < 16; ++byte) {
            uint8_t value = 0;
            if (!reader.byte(value)) return reader.error;
            nonzero = nonzero || value != 0;
            row.request_id[byte * 2] = digits[value >> 4U];
            row.request_id[byte * 2 + 1] = digits[value & 15U];
        }
        if (!nonzero) return ESP_ERR_INVALID_ARG;
        for (uint8_t previous = 0; previous < index; ++previous) {
            if (std::strcmp(page.rows[previous].request_id, row.request_id) == 0) return ESP_ERR_INVALID_ARG;
        }
        if (!reader.text(row.title, true) || !reader.text(row.preview, row.kind == CodexEventKind::Question)) {
            return reader.error;
        }
        if (row.kind == CodexEventKind::ReplyReady && row.preview[0] != '\0') return ESP_ERR_INVALID_ARG;
    }
    return reader.offset == length ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

// confirmation_code: Exact intentional-reset check; authentication belongs to the existing CCP link.
bool validCodexResetCode(const char *confirmation_code)
{
    return confirmation_code != nullptr && std::strcmp(confirmation_code, "043533477") == 0;
}

// value: Null-terminated semantic choice identifier, independent of display order.
bool validCodexChoiceId(const char *value)
{
    if (value == nullptr || value[0] < 'a' || value[0] > 'z') {
        return false;
    }
    for (size_t index = 1; index <= 12; ++index) {
        const char character = value[index];
        if (character == '\0') {
            return true;
        }
        if (index == 12 || !((character >= 'a' && character <= 'z') ||
            (character >= '0' && character <= '9') || character == '_')) {
            return false;
        }
    }
    return false;
}

// hex: Complete lowercase hex payload; callers authenticate the envelope first.
// length: Exact bounded input count; an odd or oversized value is rejected.
// candidate: Untrusted scratch; its partial contents are never a valid snapshot.
// Exception: Returns the original bounded reader error or INVALID_ARG for semantic failures.
esp_err_t decodeCodexEventPayload(const char *hex, size_t length, CodexEventPayload &candidate,
    CodexEncryptedReply *encrypted_reply)
{
    candidate = {};
    if (encrypted_reply != nullptr) *encrypted_reply = {};
    if (hex == nullptr || length == 0 || length > 3072 || length % 2 != 0) {
        return ESP_ERR_INVALID_SIZE;
    }
    PayloadReader reader{hex, length};
    uint8_t version = 0;
    uint8_t kind = 0;
    if (!reader.byte(version) || !reader.byte(kind)) {
        return reader.error;
    }
    if ((version != 2 && version != 3) || kind < static_cast<uint8_t>(CodexEventKind::Notice) ||
        kind > static_cast<uint8_t>(CodexEventKind::ReplyReady)) {
        return ESP_ERR_INVALID_ARG;
    }
    candidate.kind = static_cast<CodexEventKind>(kind);
    if (version == 3 && (candidate.kind != CodexEventKind::ReplyReady || encrypted_reply == nullptr)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!reader.text(candidate.context, false) || !reader.text(candidate.title, true) ||
        !reader.text(candidate.body, true) || !reader.byte(candidate.choice_count)) {
        return reader.error;
    }
    if (candidate.kind != CodexEventKind::ApprovalOnPc && std::strlen(candidate.body) > 63) {
        return ESP_ERR_INVALID_SIZE;
    }
    const bool question = candidate.kind == CodexEventKind::Question;
    if ((question && (candidate.choice_count < 2 || candidate.choice_count > 3)) ||
        (!question && candidate.choice_count != 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    for (uint8_t index = 0; index < candidate.choice_count; ++index) {
        auto &choice = candidate.choices[index];
        uint8_t tone = 0;
        if (!reader.text(choice.id, true) || !reader.text(choice.label, true) ||
            !reader.text(choice.detail, true) || !reader.byte(tone)) {
            return reader.error;
        }
        if (!validCodexChoiceId(choice.id) || tone > 3) {
            return ESP_ERR_INVALID_ARG;
        }
        for (uint8_t previous = 0; previous < index; ++previous) {
            if (std::strcmp(candidate.choices[previous].id, choice.id) == 0 ||
                std::strcmp(candidate.choices[previous].label, choice.label) == 0) {
                return ESP_ERR_INVALID_ARG;
            }
        }
        choice.tone = static_cast<CodexChoiceTone>(tone);
    }
    if (!reader.text(candidate.selected_id, false) || !reader.byte(candidate.failure_count)) {
        return reader.error;
    }
    if (candidate.selected_id[0] != '\0') {
        bool found = false;
        for (uint8_t index = 0; index < candidate.choice_count; ++index) {
            found = found || std::strcmp(candidate.selected_id, candidate.choices[index].id) == 0;
        }
        if (!question || !found) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    const bool failed = candidate.kind == CodexEventKind::ChecksFailed;
    if (candidate.failure_count > 6 || (failed && candidate.failure_count == 0) ||
        (!failed && candidate.failure_count != 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    for (uint8_t index = 0; index < candidate.failure_count; ++index) {
        if (!reader.text(candidate.failed_checks[index], true)) {
            return reader.error;
        }
        for (uint8_t previous = 0; previous < index; ++previous) {
            if (std::strcmp(candidate.failed_checks[previous], candidate.failed_checks[index]) == 0) {
                return ESP_ERR_INVALID_ARG;
            }
        }
    }
    uint8_t progress = 0;
    if (!reader.byte(progress) || !reader.word(candidate.passed) || !reader.word(candidate.total)) {
        return reader.error;
    }
    if (!reader.byte(candidate.pending_question_count) || !reader.byte(candidate.pending_entry_count)) {
        return reader.error;
    }
    if (candidate.pending_entry_count > 2 || candidate.pending_question_count > 128 ||
        candidate.pending_entry_count > candidate.pending_question_count ||
        (!question && (candidate.pending_question_count != 0 || candidate.pending_entry_count != 0))) {
        return ESP_ERR_INVALID_ARG;
    }
    for (uint8_t index = 0; index < candidate.pending_entry_count; ++index) {
        auto &entry = candidate.pending_questions[index];
        if (!reader.text(entry.request_id, true) || !reader.text(entry.context, false) ||
            !reader.text(entry.title, true)) { return reader.error; }
        if (std::strlen(entry.request_id) != 32 || std::strspn(entry.request_id, "0123456789abcdef") != 32 ||
            std::strcmp(entry.request_id, "00000000000000000000000000000000") == 0) {
            return ESP_ERR_INVALID_ARG;
        }
        for (uint8_t previous = 0; previous < index; ++previous) {
            if (std::strcmp(candidate.pending_questions[previous].request_id, entry.request_id) == 0) {
                return ESP_ERR_INVALID_ARG;
            }
        }
    }
    if (version == 3) {
        if (!reader.word(encrypted_reply->segment_index) || !reader.word(encrypted_reply->segment_count) ||
            !reader.word(encrypted_reply->length)) return reader.error;
        if (encrypted_reply->segment_count == 0 || encrypted_reply->segment_count > 64 ||
            encrypted_reply->segment_index >= encrypted_reply->segment_count ||
            encrypted_reply->length == 0 || encrypted_reply->length > kCodexReplySpeechBytes)
            return ESP_ERR_INVALID_SIZE;
        for (auto &value : encrypted_reply->nonce) if (!reader.byte(value)) return reader.error;
        for (size_t index = 0; index < static_cast<size_t>(encrypted_reply->length) + 16U; ++index)
            if (!reader.byte(encrypted_reply->ciphertext[index])) return reader.error;
    }
    if (reader.offset != length || progress > 1) {
        return ESP_ERR_INVALID_ARG;
    }
    candidate.progress_valid = progress == 1;
    if ((!candidate.progress_valid && (candidate.passed != 0 || candidate.total != 0)) ||
        (candidate.progress_valid && (candidate.kind != CodexEventKind::ChecksRunning &&
         candidate.kind != CodexEventKind::ChecksFailed && candidate.kind != CodexEventKind::ChecksPassed)) ||
        (candidate.progress_valid && (candidate.total == 0 || candidate.passed > candidate.total)) ||
        (candidate.progress_valid && failed && candidate.failure_count > candidate.total - candidate.passed) ||
        (candidate.kind == CodexEventKind::ChecksPassed &&
         (!candidate.progress_valid || candidate.passed != candidate.total))) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}
} // namespace LUNASHELL

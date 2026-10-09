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
esp_err_t decodeCodexEventPayload(const char *hex, size_t length, CodexEventPayload &candidate)
{
    candidate = {};
    if (hex == nullptr || length == 0 || length > 1024 || length % 2 != 0) {
        return ESP_ERR_INVALID_SIZE;
    }
    PayloadReader reader{hex, length};
    uint8_t version = 0;
    uint8_t kind = 0;
    if (!reader.byte(version) || !reader.byte(kind)) {
        return reader.error;
    }
    if (version != 1 || kind < 1 || kind > 7) {
        return ESP_ERR_INVALID_ARG;
    }
    candidate.kind = static_cast<CodexEventKind>(kind);
    if (!reader.text(candidate.context, false) || !reader.text(candidate.title, true) ||
        !reader.text(candidate.body, true) || !reader.byte(candidate.choice_count)) {
        return reader.error;
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

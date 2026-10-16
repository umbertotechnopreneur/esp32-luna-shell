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

#pragma once

#include <cstddef>
#include <cstdint>
#include "esp_err.h"

namespace LUNASHELL {

enum class CodexEventKind : uint8_t {
    None = 0, Notice = 1, Question = 2, ApprovalOnPc = 3, PrPending = 4,
    ChecksRunning = 5, ChecksFailed = 6, ChecksPassed = 7, ReplyReady = 8,
};
enum class CodexEventState : uint8_t {
    None, Pending, Sending, Received, Delivered, Cancelled, Expired, Rejected,
};
enum class CodexChoiceTone : uint8_t { Neutral, Positive, Negative, Info };

// state: Device transaction state independent of connection/activity freshness.
[[nodiscard]] constexpr bool codexEventIsOpen(CodexEventState state)
{
    return state == CodexEventState::Pending || state == CodexEventState::Sending ||
        state == CodexEventState::Received;
}

// state: Actual retired device state; none is absence, not a closed transaction.
[[nodiscard]] constexpr bool codexEventIsTerminal(CodexEventState state)
{
    return state == CodexEventState::Delivered || state == CodexEventState::Cancelled ||
        state == CodexEventState::Expired || state == CodexEventState::Rejected;
}

// state: Actual question state reported in the authenticated FIND observation.
[[nodiscard]] constexpr const char *codexEventStateToken(CodexEventState state)
{
    switch (state) {
    case CodexEventState::None: return "none";
    case CodexEventState::Pending: return "pending";
    case CodexEventState::Sending: return "sending";
    case CodexEventState::Received: return "received";
    case CodexEventState::Delivered: return "delivered";
    case CodexEventState::Cancelled: return "cancelled";
    case CodexEventState::Expired: return "expired";
    case CodexEventState::Rejected: return "rejected";
    }
    return nullptr;
}

// state: Actual retired state carried by the authenticated device acknowledgement.
// Returns nullptr for a live or absent transaction; never manufactures closure.
[[nodiscard]] constexpr const char *codexEventTerminalToken(CodexEventState state)
{
    return codexEventIsTerminal(state) ? codexEventStateToken(state) : nullptr;
}

struct CodexEventChoice {
    char id[13]{};
    char label[64]{};
    char detail[256]{};
    CodexChoiceTone tone = CodexChoiceTone::Neutral;
};

struct CodexPendingQuestion {
    char request_id[33]{};
    char context[25]{};
    char title[25]{};
};

// Payloads are bounded UI data. Context is a declared label, never a chat identity.
struct CodexEventPayload {
    CodexEventKind kind = CodexEventKind::None;
    char context[25]{};
    char title[25]{};
    // ApprovalOnPc carries the complete originating title; other bodies retain 63 bytes.
    char body[256]{};
    uint8_t choice_count = 0;
    CodexEventChoice choices[3]{};
    char selected_id[13]{};
    uint8_t failure_count = 0;
    char failed_checks[6][49]{};
    bool progress_valid = false;
    uint16_t passed = 0;
    uint16_t total = 0;
    uint8_t pending_question_count = 0;
    uint8_t pending_entry_count = 0;
    CodexPendingQuestion pending_questions[2]{};
};

// hex: Complete lowercase hexadecimal payload, without the EVENT envelope.
// length: Exact character count, bounded before any decoding or writes.
// candidate: Untrusted scratch; publish only after this function succeeds.
// Exception: Returns INVALID_SIZE for bounds, INVALID_ARG for schema/identity failures.
esp_err_t decodeCodexEventPayload(const char *hex, size_t length, CodexEventPayload &candidate);

// value: Null-terminated semantic choice identifier, independent of display order.
bool validCodexChoiceId(const char *value);

// confirmation_code: Literal human confirmation, including its leading zero; not a password.
bool validCodexResetCode(const char *confirmation_code);

} // namespace LUNASHELL

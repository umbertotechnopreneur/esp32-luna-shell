/* Copyright (c) 2026 Umberto Giacobbi */
#pragma once

#include <array>
#include <cstdint>

#include "esp_err.h"

namespace CODY::Domain {
template <typename Tag> struct EntityId;
using ChatId = EntityId<struct ChatTag>;
} // namespace CODY::Domain

namespace LUNASHELL {

inline constexpr uint8_t kCodexRecoveredAnswers = 4;

// A retained CCP1 transport identity, not a vNext semantic QuestionId or
// an authorization capability. Valid only as historical evidence.
struct CodexSavedTicket {
    std::array<uint8_t, 16> bytes{};
};

struct CodexSavedAnswer {
    CodexSavedTicket ticket{};
    std::array<char, 13> choice{};
    bool broker_receipt_observed = false;
};

// Every saved attempt is uncertain until reconciled by the live broker. This
// projection is never fed into the transport's active question or reply queue.
struct CodexRecoveryObservation {
    CodexSavedTicket last_open{};
    std::array<CodexSavedAnswer, kCodexRecoveredAnswers> attempts{};
    uint8_t attempt_count = 0;
    esp_err_t storage_error = ESP_OK;
};

// chat: Exact current committed ChatId, or a zero ID to clear the bookmark.
// Returns ESP_OK on queued admission, before settings commit and readback.
esp_err_t saveCodexSelection(const CODY::Domain::ChatId &chat);

// chat: Verified pairing-bound bookmark. Caller revalidates it against live state.
// Returns NOT_FINISHED for an admitted change not yet durable, NOT_FOUND if absent.
esp_err_t readCodexSelection(CODY::Domain::ChatId &chat);

// result: Copied historical question/answer references; never live authority.
esp_err_t readCodexRecovery(CodexRecoveryObservation &result);

} // namespace LUNASHELL

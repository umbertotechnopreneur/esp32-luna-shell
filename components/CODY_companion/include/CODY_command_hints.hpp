#pragma once
#include <cstring>
#include "CODY2D_text.hpp"
#include "CODY_activity_identity.hpp"
#include "LUNASHELL_device_snapshot.hpp"

namespace CODY {
// link: Canonical owner state; an expired READY may retain the original timeout while discovery runs.
// Returns true only for a retained pairing actively being rediscovered, never because a chat is idle.
inline bool home_is_reconnecting(const LUNASHELL::CodexLinkSnapshot &link)
{
    return link.retained_link_present && link.state == LUNASHELL::CodexLinkState::Discovering;
}

struct HomeReadyDwell {
    char chat[65]{};
    uint64_t entry_revision = 0;
    int64_t entered_us = -1;
};
static_assert(sizeof(HomeReadyDwell) <= 88, "Ready dwell retains identity/time only, never a snapshot");

// state: Display-owner entry identity and time; ordinary metadata updates cannot extend the dwell.
// ready: A healthy connected idle Home, not a reconnect or active question/operation.
// chat: Current canonical chat identity, including empty before a session is selected.
// revision: Canonical activity revision captured only on entry for provenance.
// now_us: Existing nonnegative pausable scene clock.
// calm: Receives whether the same idle owner has completed its 25-second introduction.
// Exception: Returns and logs rejected identity/clock input without changing canonical data.
inline esp_err_t advance_home_ready_dwell(HomeReadyDwell &state, bool ready, const char *chat,
    uint64_t revision, int64_t now_us, bool &calm)
{
    calm = false;
    if (chat == nullptr || std::strlen(chat) > 64 || now_us < 0) {
        ESP_LOGE("CODY_commands", "Ready dwell input rejected: clock=%lld (%s)",
            static_cast<long long>(now_us), esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    if (!ready) {
        state.entered_us = -1;
        return ESP_OK;
    }
    if (state.entered_us < 0 || std::strcmp(state.chat, chat) != 0) {
        std::strcpy(state.chat, chat);
        state.entry_revision = revision;
        state.entered_us = now_us;
    }
    calm = now_us >= state.entered_us && now_us - state.entered_us >= 25000000;
    return ESP_OK;
}

struct HomeReplyDwell {
    char chat[65]{};
    int64_t entered_us = -1;
};
static_assert(sizeof(HomeReplyDwell) <= 80, "Reply dwell retains bounded source identities only");

// state: Display-owner completion edge; metadata heartbeats never restart the introduction.
// ready: Whether the current valid canonical phase is ResponseReady.
// chat: Canonical chat identity, bounded to 64 ASCII characters by its owner.
// now_us: Existing nonnegative pausable scene clock.
// introduction: Receives true for the first five seconds of this observed completion edge.
// Exception: Returns and logs invalid identity/clock input; never modifies canonical phase.
inline esp_err_t advance_home_reply_dwell(HomeReplyDwell &state, bool ready, const char *chat,
    int64_t now_us, bool &introduction)
{
    introduction = false;
    if (chat == nullptr || std::strlen(chat) > 64 || now_us < 0) {
        ESP_LOGE("CODY_commands", "Reply dwell input rejected: clock=%lld (%s)",
            static_cast<long long>(now_us), esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    if (!ready) {
        state.entered_us = -1;
        return ESP_OK;
    }
    if (state.entered_us < 0 || std::strcmp(state.chat, chat) != 0) {
        std::strcpy(state.chat, chat);
        state.entered_us = now_us;
    }
    // ACTIVITY has no turn identity; only an observed phase exit or chat change starts another edge.
    introduction = now_us < state.entered_us || now_us - state.entered_us < 5000000;
    return ESP_OK;
}

enum class CommandHintId : uint8_t { None = 0, Name = 1, Timezone = 2, QuietHours = 3, Question = 4 };
struct CommandHint {
    CommandHintId id;
    const char *caption;
};
inline constexpr CommandHint kHomeCommandHints[] = {
    {CommandHintId::Name, "Change my name"},
    {CommandHintId::Timezone, "Set my timezone"},
    {CommandHintId::QuietHours, "Set quiet hours"},
    {CommandHintId::Question, "Ask Cody a question"}
};
// No parameters: Domain identities must remain unique independently of catalog order.
constexpr bool command_hint_ids_unique()
{
    for (unsigned first = 0; first != 4; ++first) {
        if (kHomeCommandHints[first].id == CommandHintId::None) {
            return false;
        }
        for (unsigned second = first + 1; second != 4; ++second) {
            if (kHomeCommandHints[first].id == kHomeCommandHints[second].id) {
                return false;
            }
        }
    }
    return true;
}
static_assert(command_hint_ids_unique(), "Command hint domain identities must be unique");
enum class CommandHintsMode : uint8_t { Introduction = 0, ReadyForMore = 1 };
struct CommandHintsState {
    CODY2D::TextLine title{};
    CODY2D::TextLine request{};
    char chat_id[65]{};
    int64_t started_us = 0;
    uint32_t revision = 0;
    CommandHintId page = CommandHintId::None;
    CommandHintsMode mode = CommandHintsMode::Introduction;
    bool connected = false;
    bool visible = false;
};

static_assert(sizeof(CommandHintsState) <= 256, "Home hints remain bounded display-owner storage");

// state: Route-owned token fields; glyph descriptors borrow only the current measured caption.
// metric: Stable domain identity, independent of carousel storage traversal.
// label: Authored semantic counter name.
// valid: Canonical individual validity, never inferred from another counter.
// value: Canonical exact count; Home shows whole thousands while technical panels retain exact values.
// unavailable: Truthful missing, stale or over-limit caption.
// changed: Accumulates field changes without resetting presentation time.
// Exception: Returns a logged formatting error without clipping the formatted caption.
inline esp_err_t update_home_token_field(ActivityIdentityState &state, HomeTokenMetric metric,
    const char *label, bool valid, uint64_t value, const char *unavailable, bool &changed)
{
    switch (metric) {
    case HomeTokenMetric::LastCall:
    case HomeTokenMetric::Window:
    case HomeTokenMetric::Free:
    case HomeTokenMetric::Total:
        break;
    default:
        ESP_LOGE("CODY_identity", "Token metric=%u rejected (%s)",
            static_cast<unsigned>(metric), esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    char caption[40]{};
    int count = 0;
    if (valid) {
        // Integer division intentionally reports completed thousands, never a rounded-up count.
        const bool thousands = value >= 1000;
        const uint64_t compact_value = thousands ? value / 1000 : value;
        char digits[21]{};
        const int digit_count = std::snprintf(digits, sizeof(digits), "%llu",
            static_cast<unsigned long long>(compact_value));
        if (digit_count <= 0 || digit_count >= static_cast<int>(sizeof(digits))) {
            ESP_LOGE("CODY_identity", "Token digits=%d rejected (%s)",
                digit_count, esp_err_to_name(ESP_ERR_INVALID_SIZE));
            return ESP_ERR_INVALID_SIZE;
        }
        // UINT64_MAX / 1000 needs 17 digits, five separators and NUL.
        // Together these bounded formatting buffers occupy 84 bytes, with no snapshot copies.
        char grouped[23]{};
        size_t output = 0;
        for (int index = 0; index < digit_count; ++index) {
            if (index > 0 && (digit_count - index) % 3 == 0) {
                grouped[output++] = '.';
            }
            grouped[output++] = digits[index];
        }
        count = std::snprintf(caption, sizeof(caption), "%s%s%s TT", label, grouped,
            thousands ? "K" : "");
    } else {
        count = std::snprintf(caption, sizeof(caption), "%s%s", label, unavailable);
    }
    if (count <= 0 || count >= static_cast<int>(sizeof(caption))) {
        ESP_LOGE("CODY_identity", "Token field=%u bytes=%d rejected (%s)",
            static_cast<unsigned>(metric), count, esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    auto &destination = state.token_fields[static_cast<size_t>(metric)];
    if (std::strcmp(destination, caption) != 0) {
        std::strcpy(destination, caption);
        changed = true;
    }
    return ESP_OK;
}

// state: Existing Home carousel with stable route-owned caption storage.
// font: Native measured font shared with project/model captions.
// link: Canonical authenticated activity and connection observation.
// technical: Canonical counters; source freshness and individual validity are authoritative.
// changed: Receives visible caption changes only, never periodic publication ticks.
// Exception: Returns genuine formatting or native glyph preparation failures.
inline esp_err_t update_home_tokens(ActivityIdentityState &state, const CODY2D::MonoFont &font,
    const LUNASHELL::CodexLinkSnapshot &link, const LUNASHELL::CodexTechnicalSnapshot &technical,
    bool &changed)
{
    changed = false;
    const bool coherent = link.data_valid && !link.data_stale &&
        link.state == LUNASHELL::CodexLinkState::Connected && link.activity.data_valid &&
        !link.activity.data_stale && technical.data_valid && technical.last_error == ESP_OK &&
        link.last_error == ESP_OK && link.activity.last_error == ESP_OK && technical.chat_id[0] != '\0' &&
        std::strcmp(technical.chat_id, link.activity.chat_id) == 0 &&
        technical.revision == link.activity.revision;
    const bool stale = coherent && (technical.data_stale || technical.context_stale);
    bool fields_changed = false;
    esp_err_t error = ESP_OK;
    if (!coherent || stale) {
        for (const auto metric : kHomeTokenMetrics) {
            error = update_home_token_field(state, metric, "Tokens: ", false, 0,
                stale ? "STALE" : "UNAVAILABLE", fields_changed);
            if (error != ESP_OK) {
                return error;
            }
        }
        state.token_page_count = 1;
        state.token_metric = HomeTokenMetric::LastCall;
    } else {
        state.token_page_count = 4;
        error = update_home_token_field(state, HomeTokenMetric::LastCall, "Last ",
            technical.used_valid, technical.used, "UNAVAILABLE", fields_changed);
        if (error == ESP_OK) {
            error = update_home_token_field(state, HomeTokenMetric::Window, "Window ",
                technical.capacity_valid, technical.capacity, "UNAVAILABLE", fields_changed);
        }
        if (error == ESP_OK) {
            const bool pair_valid = technical.used_valid && technical.capacity_valid;
            const bool within_limit = pair_valid && technical.used <= technical.capacity;
            error = update_home_token_field(state, HomeTokenMetric::Free, "Free ", within_limit,
                within_limit ? technical.capacity - technical.used : 0,
                pair_valid && !within_limit ? "OVERLIMIT" : "UNAVAILABLE", fields_changed);
        }
        if (error == ESP_OK) {
            error = update_home_token_field(state, HomeTokenMetric::Total, "Total ",
                technical.cumulative_valid, technical.cumulative, "UNAVAILABLE", fields_changed);
        }
        if (error != ESP_OK) {
            return error;
        }
    }
    const char *caption = state.token_fields[static_cast<size_t>(state.token_metric)];
    if (std::strcmp(caption, state.tokens) == 0) {
        return ESP_OK;
    }
    error = prepare_activity_identity_caption(font, caption, "", state.tokens,
        state.tokens_line, 0xBDF7, state.tokens_length, state.tokens_width, sizeof(state.tokens) - 1);
    if (error != ESP_OK) {
        return error;
    }
    state.tokens_offset = 0;
    changed = true;
    return ESP_OK;
}

// state: Display-owner storage retained through complete frame retirement.
// font: Existing native body font; no extra generated font or audio is needed.
// connected: Fresh healthy canonical CCP presence, never a retained pairing alone.
// eligible: Ordinary Home without an urgent/interactive presentation.
// chat_id: Current authentic session identity, or empty when no current session is observed.
// now_us: Existing active display clock; no task, poll timer or wake lease is created.
// changed: Receives only visibility/page transitions, not every rendered frame.
// mode: Ready for more keeps three Release-supported personalization prompts available.
// Exception: Returns invalid input or actual glyph preparation errors.
inline esp_err_t update_command_hints(CommandHintsState &state, const CODY2D::MonoFont &font,
    bool connected, bool eligible, const char *chat_id, int64_t now_us, bool &changed,
    CommandHintsMode mode = CommandHintsMode::Introduction)
{
    changed = false;
    if (chat_id == nullptr || std::strlen(chat_id) > 64 || now_us < 0) {
        ESP_LOGE("CODY_commands", "Hint input rejected: chat_bytes=%u clock=%lld (%s)",
            chat_id ? static_cast<unsigned>(std::strlen(chat_id)) : 0U,
            static_cast<long long>(now_us), esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    if (mode != CommandHintsMode::Introduction && mode != CommandHintsMode::ReadyForMore) {
        ESP_LOGE("CODY_commands", "Hint mode=%u rejected (%s)",
            static_cast<unsigned>(mode), esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    const bool new_chat = chat_id[0] != '\0' && std::strcmp(state.chat_id, chat_id) != 0;
    if (connected && (!state.connected || new_chat || state.mode != mode)) {
        state.started_us = now_us;
        if (chat_id[0] != '\0') {
            std::strcpy(state.chat_id, chat_id);
        }
    }
    state.connected = connected;
    state.mode = mode;
    const int64_t elapsed = now_us >= state.started_us ? now_us - state.started_us : 20000000;
    const bool ready = mode == CommandHintsMode::ReadyForMore;
    const int64_t page_time = ready ? elapsed % 15000000 : elapsed;
    const CommandHint *hint = connected && eligible && (ready || elapsed < 20000000)
        ? &kHomeCommandHints[static_cast<size_t>(page_time / 5000000)] : nullptr;
    const auto page = hint ? hint->id : CommandHintId::None;
    if (page == state.page) {
        return ESP_OK;
    }
    state.visible = false;
    if (hint != nullptr) {
        const char *captions[]{"Available commands", hint->caption};
        CODY2D::TextLine *lines[]{&state.title, &state.request};
        for (unsigned row = 0; row != 2; ++row) {
            const auto length = static_cast<uint16_t>(std::strlen(captions[row]));
            const unsigned width = (length - 1U) * font.advance + font.width;
            // At y=417 the circle still admits this centered 264-pixel chord.
            if (width > 264 || font.height > 25) {
                ESP_LOGE("CODY_commands", "Hint layout rejected: id=%u row=%u width=%u height=%u (%s)",
                    static_cast<unsigned>(page), row, width, font.height, esp_err_to_name(ESP_ERR_INVALID_SIZE));
                return ESP_ERR_INVALID_SIZE;
            }
            const esp_err_t error = CODY2D::prepare_text_line(font, {captions[row], length},
                {static_cast<int16_t>((466 - width) / 2), static_cast<int16_t>(360 + row * 32)},
                row == 0 ? 0xBDF7 : 0xFFFF, *lines[row]);
            if (error != ESP_OK) {
                ESP_LOGE("CODY_commands", "Hint preparation failed: id=%u row=%u (%s)",
                    static_cast<unsigned>(page), row, esp_err_to_name(error));
                return error;
            }
        }
        state.visible = true;
    }
    state.page = page;
    ++state.revision;
    changed = true;
    return ESP_OK;
}
} // namespace CODY

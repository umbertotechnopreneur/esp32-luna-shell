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

inline constexpr CODY2D::Rect kHomeCommandHintsBounds{113, 408, 240, 25};
inline constexpr char kHomeCommandMarquee[] =
    "Use @CCP in Codex and ask for the available commands";
inline constexpr int64_t kHomeCommandStepUs = 50000;
inline constexpr uint32_t kHomeCommandStepPixels = 2;
inline constexpr uint8_t kHomeCommandPassCount = 3;
struct CommandHintsState {
    CODY2D::TextLine request{};
    char window[32]{};
    int64_t elapsed_us = 0;
    int64_t sampled_us = -1;
    uint32_t revision = 0;
    uint32_t pixel_offset = 0;
    uint8_t completed_passes = 0;
    bool visible = false;
};

static_assert(sizeof(CommandHintsState) <= 128, "Finite Home footer retains only a bounded glyph window and clock");

// value: Canonical exact token count; compact output floors completed thousands.
// destination: Caller-owned ASCII buffer receiving grouping and an optional K suffix.
// capacity: Destination byte capacity including the terminating NUL.
// Exception: Returns logged argument or size errors; never truncates a numeric caption.
inline esp_err_t format_home_token_value(uint64_t value, char *destination, size_t capacity)
{
    if (destination == nullptr || capacity == 0) {
        ESP_LOGE("CODY_identity", "Token destination rejected: capacity=%u (%s)",
            static_cast<unsigned>(capacity), esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    const bool thousands = value >= 1000;
    char digits[21]{};
    const int count = std::snprintf(digits, sizeof(digits), "%llu",
        static_cast<unsigned long long>(thousands ? value / 1000 : value));
    if (count <= 0 || count >= static_cast<int>(sizeof(digits))) {
        ESP_LOGE("CODY_identity", "Token digits=%d rejected (%s)", count, esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    const size_t required = static_cast<size_t>(count + (count - 1) / 3 + (thousands ? 1 : 0) + 1);
    if (required > capacity) {
        ESP_LOGE("CODY_identity", "Token destination too small: required=%u capacity=%u (%s)",
            static_cast<unsigned>(required), static_cast<unsigned>(capacity), esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    size_t output = 0;
    for (int index = 0; index < count; ++index) {
        if (index > 0 && (count - index) % 3 == 0) {
            destination[output++] = '.';
        }
        destination[output++] = digits[index];
    }
    if (thousands) {
        destination[output++] = 'K';
    }
    destination[output] = '\0';
    return ESP_OK;
}

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
        char compact[24]{};
        const esp_err_t error = format_home_token_value(value, compact, sizeof(compact));
        if (error != ESP_OK) {
            return error;
        }
        count = std::snprintf(caption, sizeof(caption), "%s%s TT", label, compact);
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

// state: Display-owner lifetime and glyph window; route, chat and source changes never rearm it.
// font: Existing measured native body font; no new font or audio is allocated.
// connected: Fresh healthy canonical CCP presence, never retained pairing alone.
// eligible: Actually visible ordinary Home without an urgent or interactive presentation.
// now_us: Existing pausable active clock; hidden updates suspend accumulated scrolling.
// changed: Receives visibility or quantized pixel changes, never subpixel elapsed time.
// Exception: Returns original glyph errors or rejects invalid clocks and font geometry.
inline esp_err_t update_command_hints(CommandHintsState &state, const CODY2D::MonoFont &font,
    bool connected, bool eligible, int64_t now_us, bool &changed)
{
    changed = false;
    if (now_us < 0 || (state.sampled_us >= 0 && now_us < state.sampled_us)) {
        ESP_LOGE("CODY_commands", "Marquee clock rejected: current=%lld previous=%lld (%s)",
            static_cast<long long>(now_us), static_cast<long long>(state.sampled_us),
            esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    const bool was_visible = state.visible;
    if (!connected || !eligible || state.completed_passes == kHomeCommandPassCount) {
        state.sampled_us = -1;
        state.visible = false;
        if (was_visible) {
            ++state.revision;
            changed = true;
        }
        return ESP_OK;
    }
    const auto &bounds = kHomeCommandHintsBounds;
    // A clipped edge glyph may begin left of the viewport, but its native origin stays nonnegative.
    if (font.advance == 0 || font.width == 0 || font.width > bounds.x ||
        font.height == 0 || font.height > bounds.height) {
        ESP_LOGE("CODY_commands", "Marquee font rejected: advance=%u width=%u height=%u (%s)",
            font.advance, font.width, font.height, esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    constexpr size_t caption_length = sizeof(kHomeCommandMarquee) - 1U;
    const uint32_t text_width = static_cast<uint32_t>(caption_length - 1U) * font.advance + font.width;
    const uint32_t pass_pixels = bounds.width + text_width;
    const int64_t pass_us = static_cast<int64_t>((pass_pixels + kHomeCommandStepPixels - 1U) /
        kHomeCommandStepPixels) * kHomeCommandStepUs;
    const int64_t lifetime_us = pass_us * kHomeCommandPassCount;
    if (state.sampled_us >= 0) {
        // Saturate before addition so a long valid active interval cannot overflow or restart playback.
        state.elapsed_us += std::min(now_us - state.sampled_us, lifetime_us - state.elapsed_us);
    }
    state.sampled_us = now_us;
    const uint8_t completed_passes = static_cast<uint8_t>(state.elapsed_us / pass_us);
    const bool pass_changed = completed_passes != state.completed_passes;
    state.completed_passes = completed_passes;
    if (completed_passes == kHomeCommandPassCount) {
        state.sampled_us = -1;
        state.visible = false;
        state.request = {};
        state.window[0] = '\0';
        ++state.revision;
        changed = true;
        return ESP_OK;
    }
    const uint32_t offset = static_cast<uint32_t>((state.elapsed_us % pass_us) /
        kHomeCommandStepUs) * kHomeCommandStepPixels;
    if (was_visible && !pass_changed && offset == state.pixel_offset) {
        return ESP_OK;
    }
    const int32_t text_left = static_cast<int32_t>(bounds.x + bounds.width) - static_cast<int32_t>(offset);
    const size_t first = text_left + font.width <= bounds.x
        ? static_cast<size_t>((bounds.x - text_left - font.width) / font.advance + 1) : 0;
    const int32_t window_left = text_left + static_cast<int32_t>(first * font.advance);
    const size_t count = first < caption_length && window_left < bounds.x + bounds.width
        ? std::min(caption_length - first, static_cast<size_t>(
            (bounds.x + bounds.width - window_left + font.advance - 1) / font.advance)) : 0;
    if (count >= sizeof(state.window)) {
        ESP_LOGE("CODY_commands", "Marquee window rejected: glyphs=%u capacity=%u (%s)",
            static_cast<unsigned>(count), static_cast<unsigned>(sizeof(state.window)),
            esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    // Slice the original message without wrapping characters: every pass enters and leaves completely.
    if (count > 0) {
        std::memcpy(state.window, kHomeCommandMarquee + first, count);
    }
    state.window[count] = '\0';
    state.request = {};
    if (count > 0) {
        const esp_err_t error = CODY2D::prepare_text_line(font,
            {state.window, static_cast<uint16_t>(count)},
            {static_cast<int16_t>(window_left),
             static_cast<int16_t>(bounds.y + (bounds.height - font.height) / 2)}, 0xBDF7, state.request);
        if (error != ESP_OK) {
            ESP_LOGE("CODY_commands", "Marquee glyph preparation failed (%s)", esp_err_to_name(error));
            return error;
        }
    }
    state.pixel_offset = offset;
    state.visible = true;
    ++state.revision;
    changed = true;
    return ESP_OK;
}

// state: Stable display-owned glyph window; hidden commands produce no pixels.
// strip: Actual native destination tile, clipped without allocating another pixel buffer.
// first_row: Logical top row shared with the display orientation transform.
// first_column: Logical left column shared with the display orientation transform.
// Exception: Returns the original native text rendering error.
inline esp_err_t draw_command_hints(const CommandHintsState &state, CODY2D::Surface strip,
    uint16_t first_row, uint16_t first_column)
{
    if (!state.visible || !state.request.valid) {
        return ESP_OK;
    }
    const auto &bounds = kHomeCommandHintsBounds;
    const uint16_t left = std::max(first_column, bounds.x);
    const uint16_t right = std::min<uint16_t>(first_column + strip.width, bounds.x + bounds.width);
    const uint16_t top = std::max(first_row, bounds.y);
    const uint16_t bottom = std::min<uint16_t>(first_row + strip.height, bounds.y + bounds.height);
    if (left >= right || top >= bottom) {
        return ESP_OK;
    }
    const uint32_t offset = (top - first_row) * strip.stride + left - first_column;
    strip.pixels += offset;
    strip.pixel_capacity -= offset;
    strip.width = right - left;
    strip.height = bottom - top;
    return CODY2D::draw_text_rows(state.request, state.request.text.length, strip, top, left);
}
} // namespace CODY

#include "CODY_technical_panels.hpp"
#include "CODY2D_controls.hpp"
#include "esp_log.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

namespace CODY {
namespace {
constexpr uint16_t kWhite = 0xFFFF;
constexpr uint16_t kMuted = 0xBDF7;
constexpr uint16_t kAmber = 0xFD20;
constexpr uint16_t kCyan = 0x07FF;
constexpr uint16_t kRed = 0xF986;
constexpr uint16_t kNeutral = 0x632C;
constexpr uint16_t kPanelWidth = 320;
constexpr uint16_t kBarWidth = 296;
constexpr uint8_t kDetailRows = 4;

// operation: Specific rejected panel boundary or native rendering operation.
// error: Original error returned by that operation.
esp_err_t panel_result(const char *operation, esp_err_t error)
{
    if (error != ESP_OK) {
        ESP_LOGE("CODY_panels", "%s failed (%s)", operation, esp_err_to_name(error));
    }
    return error;
}

// text: Borrowed NUL-terminated producer field.
// maximum: Maximum producer byte length excluding the terminator.
// Exception: Rejects overlong or nonprintable metadata without repairing it.
esp_err_t validate_panel_text(const char *text, size_t maximum)
{
    if (!text) { return ESP_ERR_INVALID_ARG; }
    for (size_t index = 0; index <= maximum; ++index) {
        if (text[index] == '\0') { return ESP_OK; }
        if (index == maximum) { return ESP_ERR_INVALID_SIZE; }
        if (text[index] < 32 || text[index] > 126) { return ESP_ERR_INVALID_ARG; }
    }
    return ESP_ERR_INVALID_SIZE;
}

// text: Bounded canonical SHA-256 chat identity, or an absent producer field.
// phase: Actual activity identity; None means no selected chat.
bool has_observed_chat(const char *text, LUNASHELL::CodexActivityState phase)
{
    if (!text || phase == LUNASHELL::CodexActivityState::None || std::strlen(text) != 64) { return false; }
    for (size_t index = 0; index < 64; ++index) {
        if (text[index] != '0') { return true; }
    }
    return false;
}

// font: Actual immutable native font.
// text: Stable bounded caption; empty means this row is intentionally absent.
// origin: Fixed logical text origin.
// color: Native semantic foreground.
// line: Receives a prepared native glyph line, or a cleared absent row.
// Exception: Returns the original native font or text preparation error.
esp_err_t prepare_panel_line(const CODY2D::MonoFont &font, const char *text, CODY2D::Point origin,
                             uint16_t color, CODY2D::TextLine &line)
{
    const uint16_t length = static_cast<uint16_t>(std::strlen(text));
    if (length == 0) { line = {}; return ESP_OK; }
    return CODY2D::prepare_text_line(font, {text, length}, origin, color, line);
}

// state: Explicit independent measurement freshness.
const char *value_state_text(PanelValueState state)
{
    switch (state) {
    case PanelValueState::Current: return "CURRENT";
    case PanelValueState::Stale: return "STALE";
    case PanelValueState::Invalid: return "INVALID";
    default: return "UNAVAILABLE";
    }
}

// phase: Authenticated actual activity identity, independent of compaction.
const char *phase_text(LUNASHELL::CodexActivityState phase)
{
    using Phase = LUNASHELL::CodexActivityState;
    switch (phase) {
    case Phase::Active: return "Active";
    case Phase::Working: return "Working";
    case Phase::Editing: return "Editing code";
    case Phase::Running: return "Running command";
    case Phase::ResponseReady: return "Reply ready";
    case Phase::Paused: return "Waiting for you";
    case Phase::Interrupted: return "Interrupted";
    case Phase::Ended: return "Ended";
    default: return "Activity updates unavailable";
    }
}

// destination: Stable bounded caption storage retained through raster retirement.
// font: Actual immutable glyph metrics used for clipping decisions.
// source: Printable ASCII display text; canonical metadata stays unchanged.
// prefix: Authored technical field label.
void format_caption(char (&destination)[32], const CODY2D::MonoFont &font,
                    const char *source, const char *prefix)
{
    const size_t capacity = std::min<size_t>(31, (kPanelWidth - font.width) / font.advance + 1U);
    const size_t prefix_length = std::strlen(prefix);
    const size_t source_length = std::strlen(source);
    const size_t count = std::min(capacity, prefix_length + source_length);
    for (size_t index = 0; index < count; ++index) {
        destination[index] = index < prefix_length ? prefix[index] : source[index - prefix_length];
    }
    destination[count] = '\0';
    if (prefix_length + source_length > capacity && capacity >= 3) {
        std::memcpy(destination + capacity - 3, "...", 3);
    }
}

// first: Stable complete first line, without an ellipsis while continuation exists.
// second: Stable continuation line with explicit presentation-only truncation if needed.
// font: Actual native glyph metrics determining each row's capacity.
// source: Original bounded printable metadata; never modified by layout.
// prefix: Authored field label consuming the first line only.
void format_two_rows(char (&first)[32], char (&second)[32], const CODY2D::MonoFont &font,
                     const char *source, const char *prefix)
{
    const size_t capacity = std::min<size_t>(31, (kPanelWidth - font.width) / font.advance + 1U);
    const size_t prefix_length = std::strlen(prefix);
    const size_t source_length = std::strlen(source);
    const size_t count = std::min(capacity, prefix_length + source_length);
    for (size_t index = 0; index < count; ++index) {
        first[index] = index < prefix_length ? prefix[index] : source[index - prefix_length];
    }
    first[count] = '\0';
    const size_t consumed = count > prefix_length ? count - prefix_length : 0;
    format_caption(second, font, source + consumed, "");
}

// text: Bounded producer value, not a diagnostic status or an arbitrary substring.
// Returns false only for absence or explicit whole-value placeholder spellings.
bool has_detail_value(const char *text)
{
    return text[0] != '\0' && std::strcmp(text, "UNKNOWN") != 0 &&
        std::strcmp(text, "unknown") != 0 && std::strcmp(text, "Unknown") != 0 &&
        std::strcmp(text, "UNAVAILABLE") != 0 && std::strcmp(text, "unavailable") != 0 &&
        std::strcmp(text, "N/A") != 0 && std::strcmp(text, "n/a") != 0;
}

// destination: Stable bounded caption storage.
// label: Authored technical metric label.
// metric: Original numeric value and independent freshness.
void format_metric(char (&destination)[32], const char *label, const PanelMetric &metric)
{
    if (metric.state == PanelValueState::Current) {
        std::snprintf(destination, sizeof(destination), "%s %llu", label,
            static_cast<unsigned long long>(metric.value));
    } else {
        std::snprintf(destination, sizeof(destination), "%s %s", label, value_state_text(metric.state));
    }
}

// destination: Stable bounded caption storage.
// label: Honest duration identity, distinct from inferred active work.
// metric: Authoritative elapsed duration or update age in microseconds.
void format_duration(char (&destination)[32], const char *label, const PanelMetric &metric)
{
    if (metric.state != PanelValueState::Current) {
        format_metric(destination, label, metric);
        return;
    }
    const uint64_t seconds = metric.value / 1000000U;
    std::snprintf(destination, sizeof(destination), "%s %llu:%02llu", label,
        static_cast<unsigned long long>(seconds / 60U), static_cast<unsigned long long>(seconds % 60U));
}

// presentation: Serialized detail owner; at most four curated rows are admitted.
// font: Actual glyph metrics used to project text without changing canonical metadata.
// source: Known bounded display value; an absent optional field contributes no row.
// prefix: Authored semantic label, distinct from the canonical identity.
// color: Semantic foreground staged in the existing owner-retained prepared line.
void append_detail(PanelPresentation &presentation, const CODY2D::MonoFont &font,
                   const char *source, const char *prefix = "", uint16_t color = kMuted)
{
    if (!has_detail_value(source) || presentation.line_count >= kDetailRows) { return; }
    const uint8_t row = presentation.line_count++;
    format_caption(presentation.captions[row], font, source, prefix);
    presentation.lines[row].color = color;
}

// presentation: Serialized owner with reserved space for curated numeric facts.
// label: Short authored metric identity, bounded so a complete uint64 count fits.
// metric: Original observation with independent validity; unavailable is intentionally omitted.
void append_metric(PanelPresentation &presentation, const char *label, const PanelMetric &metric)
{
    if (metric.state == PanelValueState::Unavailable || presentation.line_count >= kDetailRows) { return; }
    format_metric(presentation.captions[presentation.line_count++], label, metric);
}

// presentation: Serialized owner retaining the prepared duration caption.
// label: Short authored duration identity.
// metric: Original observed duration; missing data never consumes a row.
void append_duration(PanelPresentation &presentation, const char *label, const PanelMetric &metric)
{
    if (metric.state == PanelValueState::Unavailable || presentation.line_count >= kDetailRows) { return; }
    format_duration(presentation.captions[presentation.line_count++], label, metric);
}

// font: Actual native glyph metrics for centering within the central viewport.
// text: Owner-retained bounded caption; empty text clears the optional prepared line.
// top: Logical top row below the reserved character artwork.
// color: Native semantic foreground.
// line: Receives the measured centered native line.
// Exception: Returns the original glyph/layout error.
esp_err_t prepare_centered_line(const CODY2D::MonoFont &font, const char *text,
                               uint16_t top, uint16_t color, CODY2D::TextLine &line)
{
    const size_t length = std::strlen(text);
    const size_t width = length ? (length - 1U) * font.advance + font.width : 0;
    if (width > kPanelWidth) { return ESP_ERR_INVALID_SIZE; }
    return prepare_panel_line(font, text, {static_cast<int16_t>(233 - width / 2U),
        static_cast<int16_t>(top)}, color, line);
}

// numerator: Original used context count; never replaced by a saturated count.
// denominator: Positive coherent capacity.
// scale: Small bounded display scale, at most the native bar width.
uint16_t scaled_usage(uint64_t numerator, uint64_t denominator, uint16_t scale)
{
    if (numerator >= denominator) { return scale; }
    uint16_t result = 0;
    // Ceil(capacity*p/scale) avoids multiplying a 64-bit token count by the scale.
    for (uint16_t pixel = 1; pixel <= scale; ++pixel) {
        const uint64_t threshold = denominator / scale * pixel +
            ((denominator % scale) * pixel + scale - 1U) / scale;
        if (numerator < threshold) { break; }
        result = pixel;
    }
    return result;
}

// inputs: Coherent lifecycle projection, with elapsed available only for a genuine start.
// presentation: Stable captions and indeterminate accent owner.
void format_compaction(const PanelInputs &inputs, PanelPresentation &presentation)
{
    using State = LUNASHELL::CodexCompactionState;
    presentation.compaction_active = inputs.compaction == State::Active;
    switch (inputs.compaction) {
    case State::Active:
        if (inputs.compaction_elapsed_us.state == PanelValueState::Unavailable) {
            std::strcpy(presentation.banner, "Compacting context");
        } else { format_duration(presentation.banner, "Compacting", inputs.compaction_elapsed_us); }
        break;
    case State::Refreshed:
        std::snprintf(presentation.banner, sizeof(presentation.banner), "%s",
            inputs.counters_pending ? "Context refreshed; pending" : "Context refreshed");
        break;
    case State::Orphaned:
        std::strcpy(presentation.banner, "Compaction not fully observed");
        break;
    case State::Interrupted:
        std::strcpy(presentation.banner, "Compaction interrupted");
        break;
    case State::Stale:
        std::strcpy(presentation.banner, "Compaction: STALE");
        break;
    default:
        std::strcpy(presentation.banner, inputs.simulated ? "SIMULATED" : "");
        break;
    }
}

// state: Immutable serialized panel owner captured by SceneContent.
// strip: Native tile already cleared by the compositor.
// first_row: Logical canvas row origin.
// first_column: Logical canvas column origin.
// Exception: Returns original native draw failure; never allocates or acquires observations.
esp_err_t draw_panel_callback(const void *state, CODY2D::Surface strip,
                             uint16_t first_row, uint16_t first_column)
{
    if (!state) { return panel_result("draw_callback", ESP_ERR_INVALID_ARG); }
    return draw_companion_panel_rows(*static_cast<const PanelPresentation *>(state), strip, first_row, first_column);
}

// valid: Genuine measurement availability from its acquisition owner.
// stale: Independently aged source, including link loss.
PanelValueState observed_value_state(bool valid, bool stale)
{
    return !valid ? PanelValueState::Unavailable : stale ? PanelValueState::Stale : PanelValueState::Current;
}

// age: Remote measured duration at receipt.
// local_age: Nonnegative local receipt age, checked for representable addition by the mapper.
uint64_t age_with_receipt(uint64_t age, uint64_t local_age)
{
    return age + local_age;
}
} // namespace

// current: Stable typed companion view.
// next: Receives the next page on success.
// Exception: Rejects unknown page IDs.
esp_err_t next_companion_panel(CompanionPanel current, CompanionPanel &next)
{
    switch (current) {
    case CompanionPanel::Home: next = CompanionPanel::Activity; break;
    case CompanionPanel::Activity: next = CompanionPanel::ModelContext; break;
    case CompanionPanel::ModelContext: next = CompanionPanel::SessionLink; break;
    case CompanionPanel::SessionLink: next = CompanionPanel::Activity; break;
    default: return panel_result("next", ESP_ERR_INVALID_ARG);
    }
    return ESP_OK;
}

// panel: Explicit technical page identity.
// inputs: Bounded borrowed canonical or visibly synthetic projection.
// font: Immutable actual body font retained by prepared lines.
// active_time_us: Nonnegative active clock for the indeterminate accent.
// presentation: Serialized in-place owner; partial preparation remains invalid.
// content: Receives its native compositor callback only after complete preparation.
// Exception: Returns real metadata or glyph preparation errors.
esp_err_t prepare_companion_panel(CompanionPanel panel, const PanelInputs &inputs,
    const CODY2D::MonoFont &font, int64_t active_time_us, PanelPresentation &presentation, SceneContent &content)
{
    const bool previously_valid = presentation.valid;
    const CompanionPanel previous_panel = presentation.panel;
    const uint16_t previous_bar_pixels = presentation.bar_pixels;
    const uint16_t previous_bar_color = presentation.bar_color;
    const bool previous_bar_visible = presentation.context_bar_visible;
    const bool previous_compaction_active = presentation.compaction_active;
    const uint32_t previous_compaction_clock = presentation.compaction_clock_us;
    uint64_t previous_row_colors = 0;
    for (uint8_t row = 0; row < kDetailRows; ++row) {
        previous_row_colors |= static_cast<uint64_t>(presentation.lines[row].color) << (row * 16U);
    }
    std::memcpy(presentation.prior_captions, presentation.captions, sizeof(presentation.captions));
    std::memcpy(presentation.prior_title, presentation.title, sizeof(presentation.title));
    std::memcpy(presentation.prior_banner, presentation.banner, sizeof(presentation.banner));
    std::memcpy(presentation.prior_hint, presentation.hint, sizeof(presentation.hint));
    presentation.valid = false;
    if (panel < CompanionPanel::Activity || panel > CompanionPanel::SessionLink ||
        active_time_us < 0 || font.advance == 0 || font.width > kPanelWidth || font.height > 25) {
        return panel_result("prepare_shape", ESP_ERR_INVALID_ARG);
    }
    const char *fields[] = {inputs.project, inputs.branch, inputs.description, inputs.chat_id, inputs.model, inputs.effort};
    constexpr const char *field_names[] = {"project", "branch", "description", "chat", "model", "effort"};
    constexpr size_t limits[] = {64, 64, 192, 64, 128, 16};
    for (size_t field = 0; field < 6; ++field) {
        const esp_err_t error = validate_panel_text(fields[field], limits[field]);
        if (error != ESP_OK) { return panel_result(field_names[field], error); }
    }
    const size_t chat_length = std::strlen(inputs.chat_id);
    if (chat_length != 0 && chat_length != 64) { return panel_result("prepare_chat_length", ESP_ERR_INVALID_ARG); }
    for (size_t index = 0; index < chat_length; ++index) {
        const char character = inputs.chat_id[index];
        if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'))) {
            return panel_result("prepare_chat_identity", ESP_ERR_INVALID_ARG);
        }
    }
    if (static_cast<uint8_t>(inputs.phase) > 9 || static_cast<uint8_t>(inputs.compaction) > 5 ||
        static_cast<uint8_t>(inputs.link) > 3 || static_cast<uint8_t>(inputs.activity) > 3 ||
        static_cast<uint8_t>(inputs.model_state) > 3 || static_cast<uint8_t>(inputs.effort_state) > 3) {
        return panel_result("prepare_identity", ESP_ERR_INVALID_ARG);
    }
    const PanelMetric *metrics[] = {&inputs.used, &inputs.capacity, &inputs.threshold, &inputs.cumulative,
        &inputs.activity_age_us, &inputs.usage_age_us, &inputs.turn_elapsed_us, &inputs.compaction_elapsed_us};
    for (const auto *metric : metrics) {
        if (static_cast<uint8_t>(metric->state) > 3) { return panel_result("prepare_metric_state", ESP_ERR_INVALID_ARG); }
    }
    const bool measured = inputs.context_coherent && inputs.used.state == PanelValueState::Current &&
        inputs.capacity.state == PanelValueState::Current;
    if (measured && inputs.capacity.value == 0) {
        return panel_result("context_zero_capacity", ESP_ERR_INVALID_ARG);
    }
    presentation.panel = panel;
    presentation.context_bar_visible = false;
    presentation.bar_color = kNeutral;
    presentation.bar_pixels = 0;
    presentation.line_count = 0;
    for (auto &caption : presentation.captions) { caption[0] = '\0'; }
    // Preparation is serialized and invalid until completion; reuse the existing
    // line foregrounds for semantic staging instead of allocating another owner array.
    for (uint8_t row = 0; row < kDetailRows; ++row) { presentation.lines[row].color = kMuted; }
    format_compaction(inputs, presentation);
    if (panel == CompanionPanel::Activity) {
        std::strcpy(presentation.title, "Activity");
        append_detail(presentation, font, inputs.project, "", kWhite);
        append_detail(presentation, font, inputs.branch, "branch: ");
        if (inputs.activity == PanelValueState::Current && inputs.phase != LUNASHELL::CodexActivityState::None) {
            append_detail(presentation, font, phase_text(inputs.phase), "", kCyan);
        } else if (inputs.activity == PanelValueState::Stale || inputs.activity == PanelValueState::Invalid) {
            append_detail(presentation, font, value_state_text(inputs.activity), "Activity: ", kAmber);
        }
        if (inputs.activity == PanelValueState::Current) { append_detail(presentation, font, inputs.description); }
        if (presentation.line_count == 0) { append_detail(presentation, font, "Start a chat in Codex"); }
    } else if (panel == CompanionPanel::ModelContext) {
        std::strcpy(presentation.title, "Model & Context");
        if (inputs.model_state == PanelValueState::Current && has_detail_value(inputs.model)) {
            const size_t row_capacity = std::min<size_t>(31, (kPanelWidth - font.width) / font.advance + 1U);
            if (std::strlen(inputs.model) + 12U > row_capacity) {
                format_two_rows(presentation.captions[0], presentation.captions[1], font, inputs.model, "Hook model: ");
                presentation.line_count = 2;
                presentation.lines[0].color = kWhite;
                presentation.lines[1].color = kWhite;
            } else { append_detail(presentation, font, inputs.model, "Hook model: ", kWhite); }
        } else if (inputs.model_state == PanelValueState::Stale || inputs.model_state == PanelValueState::Invalid) {
            append_detail(presentation, font, value_state_text(inputs.model_state), "Model: ");
        }
        if (inputs.effort_state == PanelValueState::Current) { append_detail(presentation, font, inputs.effort, "Effort: "); }
        else if (inputs.effort_state == PanelValueState::Stale || inputs.effort_state == PanelValueState::Invalid) {
            append_detail(presentation, font, value_state_text(inputs.effort_state), "Effort: ");
        }
        if (measured) {
            const uint16_t percent = scaled_usage(inputs.used.value, inputs.capacity.value, 100);
            presentation.bar_pixels = scaled_usage(inputs.used.value, inputs.capacity.value, kBarWidth);
            presentation.bar_color = percent >= 90 ? kRed : percent >= 70 ? kAmber : kCyan;
            presentation.context_bar_visible = true;
            // Two model rows plus one effort row leave one guaranteed context
            // slot; subsequent optional counts yield to these higher-priority facts.
            if (presentation.line_count >= kDetailRows) {
                return panel_result("context_row_capacity", ESP_ERR_INVALID_SIZE);
            }
            const uint8_t context_row = presentation.line_count++;
            presentation.lines[context_row].color = presentation.bar_color;
            auto &caption = presentation.captions[context_row];
            if (inputs.used.value > inputs.capacity.value) { std::strcpy(caption, "Context: OVER LIMIT"); }
            else { std::snprintf(caption, sizeof(caption), "Context used: %u%%%s", percent, inputs.context_estimated ? " ESTIMATE" : ""); }
            append_metric(presentation, "Used", inputs.used);
            append_metric(presentation, "Limit", inputs.capacity);
        } else {
            const bool invalid = inputs.used.state == PanelValueState::Invalid || inputs.capacity.state == PanelValueState::Invalid;
            const bool stale = inputs.used.state == PanelValueState::Stale || inputs.capacity.state == PanelValueState::Stale;
            if (invalid || stale) {
                presentation.context_bar_visible = true;
                append_detail(presentation, font, invalid ? "INVALID" : "STALE", "Context: ");
            }
        }
        append_metric(presentation, "Auto", inputs.threshold);
        if (presentation.line_count == 0) { append_detail(presentation, font, "No model details yet"); }
    } else {
        std::strcpy(presentation.title, "Session & Link");
        // An invalid initial snapshot cannot prove disconnection; retained state
        // cannot override the canonical link's independent freshness either.
        if (inputs.link == PanelValueState::Current) {
            append_detail(presentation, font, inputs.connected ? "PC connected" : "PC disconnected", "",
                inputs.connected ? kCyan : kAmber);
        } else if (inputs.link == PanelValueState::Unavailable) {
            append_detail(presentation, font, "Waiting for PC connection");
        } else { append_detail(presentation, font, value_state_text(inputs.link), "PC link: ", kAmber); }
        if (has_observed_chat(inputs.chat_id, inputs.phase) && presentation.line_count < kDetailRows) {
            // This caption is an explicitly abbreviated display, never a domain identity.
            std::snprintf(presentation.captions[presentation.line_count++], 32,
                "Chat ID %.6s...%.6s", inputs.chat_id, inputs.chat_id + 58);
        }
        append_duration(presentation, "Updated", inputs.activity_age_us);
        append_metric(presentation, "Total", inputs.cumulative);
    }
    std::snprintf(presentation.page, sizeof(presentation.page), "%u/3", static_cast<unsigned>(panel));
    std::snprintf(presentation.hint, sizeof(presentation.hint), "%s  Tap next; shake Home", presentation.page);
    presentation.page_line = {};
    esp_err_t error = prepare_centered_line(font, presentation.title, 170, kWhite, presentation.title_line);
    if (error == ESP_OK) { error = prepare_centered_line(font, presentation.banner, 208, kAmber, presentation.banner_line); }
    if (error == ESP_OK) { error = prepare_centered_line(font, presentation.hint, 367, kMuted, presentation.hint_line); }
    for (uint8_t row = 0; error == ESP_OK && row < presentation.line_count; ++row) {
        const uint16_t y = 234 + row * 30;
        const uint16_t color = presentation.lines[row].color;
        error = prepare_panel_line(font, presentation.captions[row],
            {73, static_cast<int16_t>(y)}, color, presentation.lines[row]);
    }
    if (error != ESP_OK) { return panel_result("prepare_text", error); }
    presentation.compaction_clock_us = static_cast<uint32_t>(active_time_us % 1800000 / 50000 * 50000);
    presentation.valid = true;
    uint64_t row_colors = 0;
    for (uint8_t row = 0; row < kDetailRows; ++row) {
        row_colors |= static_cast<uint64_t>(presentation.lines[row].color) << (row * 16U);
    }
    const bool visual_changed = !previously_valid || previous_panel != presentation.panel ||
        previous_row_colors != row_colors ||
        previous_bar_pixels != presentation.bar_pixels || previous_bar_color != presentation.bar_color ||
        previous_bar_visible != presentation.context_bar_visible ||
        previous_compaction_active != presentation.compaction_active ||
        (presentation.compaction_active && previous_compaction_clock != presentation.compaction_clock_us) ||
        std::memcmp(presentation.prior_captions, presentation.captions, sizeof(presentation.captions)) != 0 ||
        std::strcmp(presentation.prior_title, presentation.title) != 0 ||
        std::strcmp(presentation.prior_banner, presentation.banner) != 0 ||
        std::strcmp(presentation.prior_hint, presentation.hint) != 0;
    if (visual_changed) { ++presentation.revision; }
    content = {};
    content.bounds = kCompanionPanelContentBounds;
    content.state = &presentation;
    content.revision = presentation.revision;
    content.draw_rows = draw_panel_callback;
    return ESP_OK;
}

// panel: Selected technical page identity.
// link: Canonical link and activity from one synchronized observation.
// technical: Independently aged source metrics from the same observation.
// font: Actual immutable native glyph catalog.
// now_us: Device monotonic time, used only to age receipt-relative durations.
// presentation: Persistent serialized owner outside the renderer task stack.
// content: Receives a direct compositor callback on success.
// Exception: Returns the native preparation failure without guessing missing metrics.
esp_err_t prepare_companion_panel_from_observation(CompanionPanel panel,
    const LUNASHELL::CodexLinkSnapshot &link, const LUNASHELL::CodexTechnicalSnapshot &technical,
    const CODY2D::MonoFont &font, int64_t now_us, PanelPresentation &presentation, SceneContent &content)
{
    if (now_us < 0) { return panel_result("observation_clock", ESP_ERR_INVALID_ARG); }
    PanelInputs inputs{};
    inputs.connected = link.state == LUNASHELL::CodexLinkState::Connected;
    inputs.link = observed_value_state(link.data_valid, link.data_stale);
    inputs.activity = observed_value_state(link.activity.data_valid, link.activity.data_stale || !inputs.connected);
    inputs.project = link.activity.project;
    inputs.branch = link.activity.branch;
    inputs.description = link.activity.description;
    inputs.chat_id = has_observed_chat(link.activity.chat_id, link.activity.state) ? link.activity.chat_id : "";
    inputs.phase = link.activity.state;
    // ACTIVITY and TECH travel separately. An older turn's metadata must remain
    // unavailable until the matching revision arrives, even when its chat is unchanged.
    const bool chat_matches = technical.data_valid && technical.chat_id[0] != '\0' &&
        has_observed_chat(link.activity.chat_id, link.activity.state) &&
        std::strcmp(technical.chat_id, link.activity.chat_id) == 0;
    const bool owner_matches = chat_matches && link.activity.data_valid &&
        !link.activity.data_stale && technical.revision == link.activity.revision;
    if (chat_matches && !owner_matches && technical.compaction_valid) {
        // A packet gap is not a lifecycle end. Retain only an explicitly uncertain
        // or historical observation, with no previous-turn counters or elapsed time.
        using Compaction = LUNASHELL::CodexCompactionState;
        switch (technical.compaction) {
        case Compaction::Active: inputs.compaction = Compaction::Stale; break;
        case Compaction::Stale:
        case Compaction::Orphaned:
        case Compaction::Interrupted: inputs.compaction = technical.compaction; break;
        default: break;
        }
    }
    if (owner_matches) {
        const bool stale = technical.data_stale || !inputs.connected ||
            inputs.link != PanelValueState::Current;
        const uint64_t receipt_age = technical.received_at_us > 0 && now_us >= technical.received_at_us ?
            static_cast<uint64_t>(now_us - technical.received_at_us) : 0;
        const uint64_t available_age = std::numeric_limits<uint64_t>::max() - receipt_age;
        if (technical.activity_age_at_receive_us > available_age ||
            technical.usage_age_at_receive_us > available_age ||
            technical.compaction_age_at_receive_us > available_age ||
            technical.turn_elapsed_at_receive_us > available_age) {
            return panel_result("observation_duration_overflow", ESP_ERR_INVALID_SIZE);
        }
        inputs.model = technical.model;
        inputs.effort = technical.effort;
        inputs.model_state = observed_value_state(technical.model_valid && technical.model_source == LUNASHELL::CodexModelSource::HookMetadata, stale || technical.model_stale);
        inputs.effort_state = observed_value_state(technical.effort_valid, stale || technical.effort_stale);
        inputs.compaction = technical.compaction_valid ? technical.compaction : LUNASHELL::CodexCompactionState::None;
        if (stale && inputs.compaction == LUNASHELL::CodexCompactionState::Active) { inputs.compaction = LUNASHELL::CodexCompactionState::Stale; }
        inputs.context_coherent = technical.context_valid;
        const PanelValueState context = observed_value_state(technical.context_valid, stale || technical.context_stale);
        inputs.used = {technical.used, context};
        inputs.capacity = {technical.capacity, context};
        inputs.threshold = {technical.threshold, observed_value_state(technical.threshold_valid, stale || technical.context_stale)};
        inputs.cumulative = {technical.cumulative, observed_value_state(technical.cumulative_valid, stale || technical.context_stale)};
        inputs.activity_age_us = {age_with_receipt(technical.activity_age_at_receive_us, receipt_age), observed_value_state(technical.activity_age_valid, stale)};
        inputs.usage_age_us = {age_with_receipt(technical.usage_age_at_receive_us, receipt_age), context};
        using Phase = LUNASHELL::CodexActivityState;
        const bool turn_running = inputs.phase == Phase::Active || inputs.phase == Phase::Working || inputs.phase == Phase::Editing || inputs.phase == Phase::Running || inputs.phase == Phase::Paused;
        inputs.turn_elapsed_us = {age_with_receipt(technical.turn_elapsed_at_receive_us, turn_running ? receipt_age : 0), observed_value_state(technical.turn_elapsed_valid, stale || inputs.phase == Phase::Unavailable)};
        inputs.compaction_elapsed_us = {age_with_receipt(technical.compaction_age_at_receive_us, receipt_age), observed_value_state(technical.compaction_age_valid && inputs.compaction == LUNASHELL::CodexCompactionState::Active, stale)};
        inputs.counters_pending = inputs.compaction == LUNASHELL::CodexCompactionState::Refreshed && context != PanelValueState::Current;
    }
    return prepare_companion_panel(panel, inputs, font, now_us, presentation, content);
}

// presentation: Valid prepared owner whose animation is independent of observations.
// active_time_us: Nonnegative active presentation clock.
// changed: Receives whether the small indeterminate accent moved to another opacity.
// Exception: Returns invalid state or clock without manufacturing progress.
esp_err_t advance_companion_panel(PanelPresentation &presentation, int64_t active_time_us, bool &changed)
{
    changed = false;
    if (!presentation.valid || active_time_us < 0) { return panel_result("advance", ESP_ERR_INVALID_ARG); }
    const uint32_t clock = static_cast<uint32_t>(active_time_us % 1800000 / 50000 * 50000);
    changed = presentation.compaction_active && clock != presentation.compaction_clock_us;
    presentation.compaction_clock_us = clock;
    if (changed) { ++presentation.revision; }
    return ESP_OK;
}

// presentation: Immutable serialized complete technical panel.
// strip: Native RGB565 tile whose background belongs to the compositor.
// first_row: Logical tile row shared with touch and rotation.
// first_column: Logical tile column shared with touch and rotation.
// Exception: Returns original native text or antialiased control error.
esp_err_t draw_companion_panel_rows(const PanelPresentation &presentation, CODY2D::Surface strip,
                                    uint16_t first_row, uint16_t first_column)
{
    if (!presentation.valid) { return panel_result("draw_state", ESP_ERR_INVALID_STATE); }
    esp_err_t error = CODY2D::validate_surface(strip);
    const CODY2D::TextLine *fixed[] = {&presentation.title_line, &presentation.page_line, &presentation.banner_line, &presentation.hint_line};
    for (const auto *line : fixed) {
        if (error == ESP_OK && line->valid) { error = CODY2D::draw_text_rows(*line, line->text.length, strip, first_row, first_column); }
    }
    for (uint8_t row = 0; error == ESP_OK && row < presentation.line_count; ++row) {
        const auto &line = presentation.lines[row];
        if (line.valid) { error = CODY2D::draw_text_rows(line, line.text.length, strip, first_row, first_column); }
    }
    if (error == ESP_OK && presentation.context_bar_visible) {
        CODY2D::CapsuleControl track{};
        track.bounds = {85, 354, kBarWidth, 12};
        track.top_color = kNeutral;
        track.bottom_color = kNeutral;
        track.border_color = kNeutral;
        error = CODY2D::draw_capsule_rows(track, strip, first_row, first_column);
        if (error == ESP_OK && presentation.bar_pixels != 0) {
            track.top_color = presentation.bar_color;
            track.bottom_color = presentation.bar_color;
            track.border_color = presentation.bar_color;
            // Clip a complete AA capsule rather than constructing an invalid sub-12px capsule.
            const uint16_t left = std::max<uint16_t>(first_column, track.bounds.x);
            const uint16_t right = std::min<uint16_t>(first_column + strip.width,
                track.bounds.x + presentation.bar_pixels);
            if (left < right) {
                CODY2D::Surface clipped = strip;
                const uint16_t skipped = left - first_column;
                clipped.pixels += skipped;
                clipped.pixel_capacity -= skipped;
                clipped.width = right - left;
                error = CODY2D::draw_capsule_rows(track, clipped, first_row, left);
            }
        }
    }
    if (error == ESP_OK && presentation.compaction_active) {
        CODY2D::CapsuleControl accent{};
        accent.bounds = {73, 199, 320, 3};
        const uint32_t half = presentation.compaction_clock_us < 900000 ? presentation.compaction_clock_us : 1800000 - presentation.compaction_clock_us;
        const uint8_t opacity = static_cast<uint8_t>(80 + half * 175 / 900000);
        accent.top_color = CODY2D::blend_rgb565(kAmber, 0, opacity);
        accent.bottom_color = accent.top_color;
        accent.border_color = accent.top_color;
        error = CODY2D::draw_capsule_rows(accent, strip, first_row, first_column);
    }
    return panel_result("draw", error);
}
} // namespace CODY

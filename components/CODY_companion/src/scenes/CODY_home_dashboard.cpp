#include "CODY_home_dashboard.hpp"
#include "CODY_command_hints.hpp"
#include "esp_log.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace CODY {
namespace {
constexpr uint16_t kMint = 0x5FF9;
constexpr uint16_t kAmber = 0xFD20;
constexpr uint16_t kWhite = 0xFFFF;
constexpr uint16_t kMuted = 0x9CD3;
constexpr uint16_t kTrack = 0x2125;
// Authored equal-length samples of a symmetric cubic arc; no runtime trigonometry.
constexpr CODY2D::Vec2 kArcPoints[] = {
    {44.000f, 294.000f},
    {48.270f, 317.160f},
    {57.739f, 338.678f},
    {73.277f, 356.283f},
    {93.383f, 368.478f},
    {115.692f, 376.040f},
    {138.842f, 380.494f},
    {162.289f, 383.001f},
    {185.837f, 384.301f},
    {209.415f, 384.864f},
    {233.000f, 385.000f},
    {256.585f, 384.864f},
    {280.163f, 384.301f},
    {303.711f, 383.001f},
    {327.158f, 380.494f},
    {350.308f, 376.040f},
    {372.617f, 368.478f},
    {392.723f, 356.283f},
    {408.261f, 338.678f},
    {417.730f, 317.160f},
    {422.000f, 294.000f}
};
static_assert(std::size(kArcPoints) == kHomeDashboardSegments + 1);
// Inward normals computed from each sample's neighboring tangent, shared by all prepared ribbons.
constexpr CODY2D::Vec2 kArcNormals[] = {
    {0.983425f, -0.181314f}, {0.955828f, -0.293928f}, {0.842581f, -0.538569f},
    {0.641412f, -0.767197f}, {0.422242f, -0.906483f}, {0.255549f, -0.966796f},
    {0.147748f, -0.989025f}, {0.080744f, -0.996735f}, {0.039501f, -0.999220f},
    {0.014819f, -0.999890f}, {0.000000f, -1.000000f}, {-0.014819f, -0.999890f},
    {-0.039501f, -0.999220f}, {-0.080744f, -0.996735f}, {-0.147748f, -0.989025f},
    {-0.255549f, -0.966796f}, {-0.422242f, -0.906483f}, {-0.641412f, -0.767197f},
    {-0.842581f, -0.538569f}, {-0.955828f, -0.293928f}, {-0.983425f, -0.181314f}
};
constexpr CODY2D::Vec2 kCapSamples[] = {
    {.866025f, .5f}, {.5f, .866025f}, {0.f, 1.f},
    {-.5f, .866025f}, {-.866025f, .5f}, {-1.f, 0.f}
};
constexpr uint16_t kArcPalette[256] = {kTrack, kMint, kAmber};
static_assert(std::size(kArcNormals) == std::size(kArcPoints));

// operation: Exact dashboard operation that rejected an input or failed.
// error: Original failure, preserved for the caller and structured owner.
// Exception: Logs without changing the original error.
esp_err_t dashboard_result(const char *operation, esp_err_t error)
{
    if (error != ESP_OK) { ESP_LOGE("CODY_dashboard", "%s failed (%s)", operation, esp_err_to_name(error)); }
    return error;
}

// units: Authored distance from zero through 2000, in hundredths of a sampled arc segment.
// point: Receives the interpolated centerline point in logical display coordinates.
// normal: Receives the continuous inward ribbon normal, with no per-frame trigonometry.
void sample_arc(uint16_t units, CODY2D::Vec2 &point, CODY2D::Vec2 &normal)
{
    const unsigned index = std::min<unsigned>(units / 100, kHomeDashboardSegments - 1);
    const float fraction = units / 100.f - index;
    const auto &from = kArcPoints[index];
    const auto &to = kArcPoints[index + 1];
    const auto &from_normal = kArcNormals[index];
    const auto &to_normal = kArcNormals[index + 1];
    point = {from.x + (to.x - from.x) * fraction, from.y + (to.y - from.y) * fraction};
    normal = {from_normal.x + (to_normal.x - from_normal.x) * fraction,
        from_normal.y + (to_normal.y - from_normal.y) * fraction};
}

// owner: Persistent dashboard storage; only one track and two wrapping accents may be prepared.
// first: Inclusive authored arc distance in hundredths of a segment.
// last: Exclusive authored arc distance; a zero-length ribbon produces no pixels.
// color_slot: Track, mint or orange slot in the immutable native palette.
// Exception: Rejects invalid interval or capacity before publishing the prepared polygon.
esp_err_t prepare_arc_ribbon(HomeDashboardPresentation &owner, uint16_t first, uint16_t last, uint8_t color_slot)
{
    if (first > last || last > 2000 || color_slot > 2 || owner.arc_path_count >= std::size(owner.arc_paths)) {
        return dashboard_result("arc_ribbon_input", ESP_ERR_INVALID_ARG);
    }
    if (first == last) { return ESP_OK; }
    auto &path = owner.arc_paths[owner.arc_path_count];
    path.vertex_count = 0;
    path.color_slot = color_slot;
    CODY2D::Vec2 point{};
    CODY2D::Vec2 normal{};
    // units: Bounded authored centerline sample, including an interpolated free-space endpoint.
    // direction: Minus one selects the outside edge; plus one selects the inside edge.
    const auto append_edge = [&](uint16_t units, float direction) {
        sample_arc(units, point, normal);
        path.vertices[path.vertex_count++] = {point.x + normal.x * 3.5f * direction,
            point.y + normal.y * 3.5f * direction};
    };
    append_edge(first, -1.f);
    const unsigned first_interior = first / 100 + 1;
    for (unsigned index = first_interior; index * 100 < last; ++index) {
        append_edge(static_cast<uint16_t>(index * 100), -1.f);
    }
    append_edge(last, -1.f);
    // The six-point semicircle joins the two continuous edges without overlapping segment seams.
    for (const auto &sample : kCapSamples) {
        path.vertices[path.vertex_count++] = {point.x - normal.x * 3.5f * sample.x - normal.y * 3.5f * sample.y,
            point.y - normal.y * 3.5f * sample.x + normal.x * 3.5f * sample.y};
    }
    for (int index = (last - 1) / 100; index >= static_cast<int>(first_interior); --index) {
        append_edge(static_cast<uint16_t>(index * 100), 1.f);
    }
    append_edge(first, 1.f);
    for (unsigned index = 0; index + 1 < std::size(kCapSamples); ++index) {
        const auto &sample = kCapSamples[index];
        path.vertices[path.vertex_count++] = {point.x + normal.x * 3.5f * sample.x + normal.y * 3.5f * sample.y,
            point.y + normal.y * 3.5f * sample.x - normal.x * 3.5f * sample.y};
    }
    // Twenty intervals, their two edges and twelve cap samples need at most 52 vertices.
    static_assert((kHomeDashboardSegments + 1) * 2 + 10 <= kHomeDashboardArcVertices);
    ++owner.arc_path_count;
    return ESP_OK;
}

// state: Independent canonical metric validity.
const char *metric_status(PanelValueState state)
{
    switch (state) {
    case PanelValueState::Current: return "CURRENT";
    case PanelValueState::Stale: return "STALE";
    case PanelValueState::Invalid: return "INVALID";
    default: return "UNAVAILABLE";
    }
}

// font: Previously validated immutable glyph metrics.
// text: NUL-terminated bounded owner caption.
uint32_t text_width(const CODY2D::MonoFont &font, const char *text)
{
    const size_t length = std::strlen(text);
    return length == 0 ? 0 : static_cast<uint32_t>((length - 1) * font.advance + font.width);
}

// owner: Stable text owner with at most six retained lines.
// font: Immutable font retained through complete-frame retirement.
// text: Bounded printable ASCII caption copied into the owner.
// top: Logical vertical origin inside the lower dashboard.
// color: Authored semantic RGB565 foreground.
// left: Explicit x origin, or negative for centered placement.
// Exception: Logs and returns the original glyph error or rejected bounds.
esp_err_t add_line(HomeDashboardPresentation &owner, const CODY2D::MonoFont &font,
    const char *text, uint16_t top, uint16_t color, int16_t left = -1)
{
    const size_t length = std::strlen(text);
    const uint32_t width = text_width(font, text);
    if (length >= sizeof(owner.captions[0]) || owner.line_count >= std::size(owner.lines) || width > 320) {
        return dashboard_result("caption_capacity", ESP_ERR_INVALID_SIZE);
    }
    if (length == 0) { return ESP_OK; }
    const int x = left < 0 ? static_cast<int>((466 - width) / 2) : left;
    if (x < 73 || x + width > 393 || top < 268 || top + font.height > 393) {
        return dashboard_result("caption_geometry", ESP_ERR_INVALID_SIZE);
    }
    const unsigned row = owner.line_count;
    std::strcpy(owner.captions[row], text);
    const esp_err_t error = CODY2D::prepare_text_line(font, {owner.captions[row], static_cast<uint16_t>(length)},
        {static_cast<int16_t>(x), static_cast<int16_t>(top)}, color, owner.lines[row]);
    if (error != ESP_OK) { return dashboard_result("caption_prepare", error); }
    ++owner.line_count;
    return ESP_OK;
}

// destination: Small temporary metric label, bounded to the caller-owned buffer.
// capacity: Actual writable buffer capacity including NUL.
// label: Authored domain label, never a numeric index.
// metric: Canonical count and its independent validity.
// Exception: Returns formatting errors without clipping the caption; the hero owns the shared TT unit.
esp_err_t metric_caption(char *destination, size_t capacity, const char *label,
    const PanelMetric &metric)
{
    char number[24]{};
    int count = 0;
    if (metric.state == PanelValueState::Current) {
        const esp_err_t error = format_home_token_value(metric.value, number, sizeof(number));
        if (error != ESP_OK) { return error; }
        count = std::snprintf(destination, capacity, "%s %s", label, number);
    } else {
        count = std::snprintf(destination, capacity, "%s %s", label, metric_status(metric.state));
    }
    return count < 0 || static_cast<size_t>(count) >= capacity
        ? dashboard_result("metric_format", ESP_ERR_INVALID_SIZE) : ESP_OK;
}

// motion: Optional canonical classifier result, never guessed from posture.
// now_us: Original observation time used only for inference freshness.
const char *motion_caption(const LUNASHELL::ActivitySnapshot *motion, int64_t now_us);

// owner: Stable serialized line/ribbon buffers, invalid until preparation completes.
// inputs: Borrowed canonical projection shared with the technical-panel owner.
// title_font: Actual large numeric glyphs.
// body_font: Actual label glyphs and bounded large-count fallback.
// small_font: Actual secondary-row glyphs, never a resampled bitmap.
// active_time_us: Local animation clock, independent of observation age.
// motion: Optional independently acquired canonical activity result.
// now_us: Original monotonic observation time, used only for motion freshness.
// Exception: Propagates original numeric, text and ribbon preparation failures.
esp_err_t prepare_arc(HomeDashboardPresentation &owner, const PanelInputs &inputs,
    const CODY2D::MonoFont &title_font, const CODY2D::MonoFont &body_font,
    const CODY2D::MonoFont &small_font, int64_t active_time_us,
    const LUNASHELL::ActivitySnapshot *motion, int64_t now_us)
{
    PanelMetric available{};
    if (inputs.context_coherent && inputs.used.state == PanelValueState::Current &&
        inputs.capacity.state == PanelValueState::Current && inputs.capacity.value == 0) {
        return dashboard_result("zero_window_capacity", ESP_ERR_INVALID_ARG);
    }
    const bool measured = inputs.context_coherent && inputs.used.state == PanelValueState::Current &&
        inputs.capacity.state == PanelValueState::Current && inputs.capacity.value > 0;
    owner.arc_state = HomeDashboardArcState::Unavailable;
    if (inputs.used.state == PanelValueState::Invalid || inputs.capacity.state == PanelValueState::Invalid) {
        available.state = PanelValueState::Invalid;
        owner.arc_state = HomeDashboardArcState::Invalid;
    } else if (inputs.used.state == PanelValueState::Stale || inputs.capacity.state == PanelValueState::Stale) {
        available.state = PanelValueState::Stale;
        owner.arc_state = HomeDashboardArcState::Stale;
    } else if (measured) {
        if (inputs.used.value > inputs.capacity.value) {
            owner.arc_state = HomeDashboardArcState::OverLimit;
        } else {
            available = {inputs.capacity.value - inputs.used.value, PanelValueState::Current};
            owner.arc_state = HomeDashboardArcState::Measured;
        }
    }
    const bool compacting = inputs.compaction == LUNASHELL::CodexCompactionState::Active;
    if (compacting) { owner.arc_state = HomeDashboardArcState::Compacting; }
    const uint16_t accent = compacting ? kAmber : kMint;
    char caption[48]{};
    esp_err_t error = ESP_OK;
    bool stacked = false;
    if (available.state == PanelValueState::Current) {
        char number[24]{};
        error = format_home_token_value(available.value, number, sizeof(number));
        if (error != ESP_OK) { return error; }
        const uint32_t prefix_width = text_width(body_font, "Free");
        const uint32_t suffix_width = text_width(body_font, "TT");
        const uint32_t value_width = text_width(title_font, number);
        const uint32_t total_width = prefix_width + value_width + suffix_width + 20;
        if (total_width <= 320) {
            const int16_t left = static_cast<int16_t>((466 - total_width) / 2);
            error = add_line(owner, body_font, "Free", 277, kWhite, left);
            if (error == ESP_OK) { error = add_line(owner, title_font, number, 268, accent, left + prefix_width + 10); }
            if (error == ESP_OK) { error = add_line(owner, body_font, "TT", 277, kWhite, left + prefix_width + value_width + 20); }
        } else {
            // Extreme uint64 values remain exact compact text, using a second line rather than clipping.
            stacked = true;
            error = add_line(owner, body_font, "Free", 268, kWhite);
            const int count = std::snprintf(caption, sizeof(caption), "%s TT", number);
            if (count < 0 || count >= static_cast<int>(sizeof(caption))) { return dashboard_result("primary_format", ESP_ERR_INVALID_SIZE); }
            if (error == ESP_OK) { error = add_line(owner, body_font, caption, 294, accent); }
        }
    } else {
        const char *status = measured && inputs.used.value > inputs.capacity.value ? "OVERLIMIT" : metric_status(available.state);
        std::snprintf(caption, sizeof(caption), "Free %s", status);
        error = add_line(owner, body_font, caption, 279, kAmber);
    }
    if (error != ESP_OK) { return error; }
    char last[48]{};
    char window[48]{};
    error = metric_caption(last, sizeof(last), "Last", inputs.used);
    if (error == ESP_OK) { error = metric_caption(window, sizeof(window), "Window", inputs.capacity); }
    if (error != ESP_OK) { return error; }
    const size_t last_length = std::strlen(last);
    const size_t window_length = std::strlen(window);
    const size_t labels_length = last_length + window_length;
    const uint32_t paired_width = static_cast<uint32_t>((labels_length + 2) * small_font.advance + small_font.width);
    const uint16_t secondary_top = stacked ? 322 : 315;
    if (paired_width <= 320 && labels_length + 3 < sizeof(caption)) {
        // The complete pair and terminator fit; copy exact labels without formatter truncation.
        std::memcpy(caption, last, last_length);
        std::memcpy(caption + last_length, " | ", 3);
        std::memcpy(caption + last_length + 3, window, window_length + 1);
        error = add_line(owner, small_font, caption, secondary_top, kMuted);
    } else {
        error = metric_caption(window, sizeof(window), "Win", inputs.capacity);
        if (error != ESP_OK) { return error; }
        const int count = std::snprintf(caption, sizeof(caption), "%s | %s", last, window);
        if (count > 0 && count < static_cast<int>(sizeof(caption)) && text_width(small_font, caption) <= 320) {
            // Keep both ordinary counters visible using the compact window label and the hero's TT unit.
            error = add_line(owner, small_font, caption, secondary_top, kMuted);
        } else {
            // Exceptional full-width counts alternate without truncation; the narrower body face keeps all digits.
            const char *selected = (active_time_us / 5000000) % 2 == 0 ? last : window;
            const auto &metric_font = text_width(small_font, selected) <= 320 ? small_font : body_font;
            error = add_line(owner, metric_font, selected, secondary_top, kMuted);
        }
    }
    if (error != ESP_OK) { return error; }
    const auto &duration = compacting ? inputs.compaction_elapsed_us : inputs.turn_elapsed_us;
    if (duration.state == PanelValueState::Current) {
        const uint64_t seconds = duration.value / 1000000;
        std::snprintf(caption, sizeof(caption), "%02llu:%02llu",
            static_cast<unsigned long long>(seconds / 60), static_cast<unsigned long long>(seconds % 60));
    } else {
        std::snprintf(caption, sizeof(caption), "%s", metric_status(duration.state));
    }
    const char *motion_text = motion_caption(motion, now_us);
    char motion_error[24]{};
    if (motion != nullptr && motion->error != ESP_OK && motion->rejection != LUNASHELL::ActivityRejection::CadenceGap) {
        std::snprintf(motion_error, sizeof(motion_error), "IMU error 0x%X", static_cast<unsigned>(motion->error));
        motion_text = motion_error;
    }
    char status_line[48]{};
    const int status_length = std::snprintf(status_line, sizeof(status_line), "%s | %s", caption, motion_text);
    const uint16_t status_top = stacked ? 350 : 343;
    const uint16_t status_width = stacked ? 240 : 288;
    if (status_length > 0 && status_length < static_cast<int>(sizeof(status_line)) && text_width(body_font, status_line) <= status_width) {
        // The narrower native body face keeps timer and motion together inside the arc's inner boundary.
        error = add_line(owner, body_font, status_line, status_top, compacting ? kAmber : kWhite);
    } else {
        // Exceptional duration widths alternate honestly; ordinary standing/walking remains alongside the timer.
        const char *selected = (active_time_us / 5000000) % 2 == 0 ? caption : motion_text;
        error = add_line(owner, body_font, selected, status_top, compacting ? kAmber : kWhite);
    }
    if (error != ESP_OK) { return error; }
    owner.arc_units = available.state == PanelValueState::Current
        ? static_cast<uint16_t>(static_cast<double>(available.value) / static_cast<double>(inputs.capacity.value) * 2000.0) : 0;
    owner.animation_step = compacting ? static_cast<uint16_t>(active_time_us / 100000 % kHomeDashboardSegments) : 0;
    error = prepare_arc_ribbon(owner, 0, 2000, 0);
    if (error != ESP_OK) { return error; }
    if (compacting) {
        const uint16_t first = owner.animation_step * 100;
        error = prepare_arc_ribbon(owner, first, std::min<uint16_t>(first + 300, 2000), 2);
        if (error == ESP_OK && first + 300 > 2000) {
            error = prepare_arc_ribbon(owner, 0, first + 300 - 2000, 2);
        }
    } else if (owner.arc_state == HomeDashboardArcState::Measured) {
        error = prepare_arc_ribbon(owner, 0, owner.arc_units, 1);
    }
    return error;
}

// state: Prepared serialized dashboard borrowed until frame retirement.
// strip: Caller-owned RGB565 tile.
// first_row: Logical tile y origin.
// first_column: Logical tile x origin.
// Exception: Returns the original null-state or native draw error.
esp_err_t dashboard_callback(const void *state, CODY2D::Surface strip, uint16_t first_row, uint16_t first_column)
{
    if (state == nullptr) { return dashboard_result("draw_callback", ESP_ERR_INVALID_ARG); }
    return draw_home_dashboard_rows(*static_cast<const HomeDashboardPresentation *>(state), strip, first_row, first_column);
}

// motion: Optional canonical classifier result; never inferred from posture or source text.
// now_us: Local clock used only to reject an expired original inference window.
const char *motion_caption(const LUNASHELL::ActivitySnapshot *motion, int64_t now_us)
{
    if (motion == nullptr) { return "IMU UNAVAILABLE"; }
    if (motion->rejection == LUNASHELL::ActivityRejection::CadenceGap) { return "IMU CADENCE INVALID"; }
    if (motion->error != ESP_OK || !motion->scores_valid) { return "IMU UNAVAILABLE"; }
    if (motion->window_end_us <= 0 || now_us < motion->window_end_us || now_us - motion->window_end_us > 6000000) { return "IMU STALE"; }
    switch (motion->activity) {
    case LUNASHELL::ActivityClass::Downstairs: return "IMU Downstairs";
    case LUNASHELL::ActivityClass::Jogging: return "IMU Jogging";
    case LUNASHELL::ActivityClass::Sitting: return "IMU Sitting";
    case LUNASHELL::ActivityClass::Standing: return "IMU Standing";
    case LUNASHELL::ActivityClass::Upstairs: return "IMU Upstairs";
    case LUNASHELL::ActivityClass::Walking: return "IMU Walking";
    default: return "IMU UNKNOWN";
    }
}
} // namespace

// state: Serialized display-only clock; manual input restarts a complete visible dwell.
void reset_home_dashboard_rotation(HomeDashboardRotationState &state)
{
    state.sampled_us = -1;
    state.elapsed_us = 0;
}

// state: Serialized active-time sample and accumulated visible dwell.
// current: Canonical selected Home, ModelContext or SessionLink panel.
// active_us: Monotonic presentation clock, never an observation receipt timestamp.
// visible: Hidden routes suspend sampling and preserve their already displayed dwell.
// compacting: Authenticated current compaction reserves Home until the source reports completion.
// next: Receives one next identity at most; every newly displayed page receives a complete dwell.
// Exception: Logs rejected clocks or identities and leaves the rotation state unchanged.
esp_err_t advance_home_dashboard_rotation(HomeDashboardRotationState &state,
    CompanionPanel current, int64_t active_us, bool visible, bool compacting, CompanionPanel &next)
{
    const bool known_panel = current == CompanionPanel::Home || current == CompanionPanel::ModelContext ||
        current == CompanionPanel::SessionLink;
    if (!known_panel || active_us < 0 || state.sampled_us < -1 || state.elapsed_us < 0 ||
        state.elapsed_us >= kHomeDashboardRotationUs ||
        (state.sampled_us >= 0 && active_us < state.sampled_us)) {
        ESP_LOGE("CODY_dashboard", "Rotation rejected: panel=%u active=%lld sample=%lld elapsed=%lld (%s)",
            static_cast<unsigned>(current), static_cast<long long>(active_us),
            static_cast<long long>(state.sampled_us), static_cast<long long>(state.elapsed_us),
            esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    next = current;
    if (!visible) {
        state.sampled_us = -1;
        return ESP_OK;
    }
    if (compacting) {
        reset_home_dashboard_rotation(state);
        next = CompanionPanel::Home;
        return ESP_OK;
    }
    if (state.sampled_us >= 0) {
        const int64_t delta = active_us - state.sampled_us;
        const int64_t remaining = kHomeDashboardRotationUs - state.elapsed_us;
        if (delta >= remaining) {
            const esp_err_t error = next_home_dashboard_panel(current, next);
            if (error != ESP_OK) { return error; }
            state.elapsed_us = 0;
        } else {
            // Comparing the remaining dwell before addition bounds even a very long valid frame interval.
            state.elapsed_us += delta;
        }
    }
    state.sampled_us = active_us;
    return ESP_OK;
}

// current: Stable lower-panel identity.
// next: Receives the next explicit typed identity.
// Exception: Logs and rejects unknown panel values.
esp_err_t next_home_dashboard_panel(CompanionPanel current, CompanionPanel &next)
{
    switch (current) {
    case CompanionPanel::Home: next = CompanionPanel::ModelContext; break;
    case CompanionPanel::ModelContext: next = CompanionPanel::SessionLink; break;
    case CompanionPanel::SessionLink: next = CompanionPanel::Home; break;
    case CompanionPanel::Activity: next = CompanionPanel::Home; break;
    default: return dashboard_result("next_panel", ESP_ERR_INVALID_ARG);
    }
    return ESP_OK;
}

// panel: Selected lower dashboard identity, never an entire-screen replacement.
// link: Canonical synchronized link/activity observation.
// technical: Independently aged canonical metadata.
// title_font: Immutable numeric title glyphs.
// body_font: Immutable label and detail glyphs.
// now_us: Original monotonic observation clock.
// active_time_us: Independent pausable presentation clock.
// presentation: Serialized component-owned buffers retained through complete-frame retirement.
// content: Receives the compositor callback after full successful preparation.
// motion: Optional canonical activity inference; no consumer sensor access.
// small_font: Optional immutable secondary-row font, or the native body font.
// device_ip: Canonical local-device Wi-Fi address, distinct from the paired PC.
// device_ip_state: Independent current/stale/invalid/unavailable address state.
// Exception: Logs and returns original projection, glyph, size or geometry errors.
esp_err_t prepare_home_dashboard(CompanionPanel panel,
    const LUNASHELL::CodexLinkSnapshot &link, const LUNASHELL::CodexTechnicalSnapshot &technical,
    const CODY2D::MonoFont &title_font, const CODY2D::MonoFont &body_font,
    int64_t now_us, int64_t active_time_us, HomeDashboardPresentation &presentation,
    SceneContent &content, const LUNASHELL::ActivitySnapshot *motion, const CODY2D::MonoFont *small_font,
    const char *device_ip, PanelValueState device_ip_state)
{
    const bool previously_valid = presentation.valid;
    const auto previous_panel = presentation.panel;
    const auto previous_arc = presentation.arc_state;
    const auto previous_units = presentation.arc_units;
    const auto previous_step = presentation.animation_step;
    std::memcpy(presentation.prior_captions, presentation.captions, sizeof(presentation.captions));
    for (unsigned index = 0; index < std::size(presentation.lines); ++index) {
        presentation.prior_colors[index] = presentation.lines[index].color;
        presentation.prior_origins[index] = presentation.lines[index].origin;
        presentation.prior_fonts[index] = presentation.lines[index].font;
    }
    presentation.valid = false;
    presentation.line_count = presentation.arc_path_count = 0;
    presentation.price_visible = false;
    presentation.price_capsule = {};
    presentation.arc_units = presentation.animation_step = 0;
    presentation.arc_state = HomeDashboardArcState::Unavailable;
    presentation.panel = panel;
    for (auto &caption : presentation.captions) { caption[0] = '\0'; }
    for (auto &line : presentation.lines) { line = {}; }
    if (panel > CompanionPanel::SessionLink || panel == CompanionPanel::Activity || active_time_us < 0 ||
        device_ip == nullptr || static_cast<unsigned>(device_ip_state) > static_cast<unsigned>(PanelValueState::Invalid)) { return dashboard_result("prepare_input", ESP_ERR_INVALID_ARG); }
    const auto &secondary_font = small_font == nullptr ? body_font : *small_font;
    esp_err_t error = CODY2D::validate_mono_font(title_font);
    if (error == ESP_OK) { error = CODY2D::validate_mono_font(body_font); }
    if (error == ESP_OK) { error = CODY2D::validate_mono_font(secondary_font); }
    if (error != ESP_OK) { return dashboard_result("prepare_font", error); }
    PanelInputs inputs{};
    error = project_companion_panel_inputs(link, technical, now_us, "", inputs);
    if (error != ESP_OK) { return error; }
    if (panel == CompanionPanel::Home) {
        error = prepare_arc(presentation, inputs, title_font, body_font, secondary_font, active_time_us, motion, now_us);
    } else if (panel == CompanionPanel::ModelContext) {
        char caption[48]{};
        // The shared header owns model identity; this panel complements it with price and effort.
        const bool known_price = inputs.model_state == PanelValueState::Current && inputs.cost_state == PanelValueState::Current &&
            inputs.cost_band != LUNASHELL::CodexCostBand::Unknown;
        const uint16_t price_color = known_price ? cost_band_color(inputs.cost_band) : kMuted;
        if (known_price) {
            std::snprintf(caption, sizeof(caption), inputs.cost_band == LUNASHELL::CodexCostBand::Balanced ? "%s" : "Price %s",
                cost_band_text(inputs.cost_band));
        } else {
            std::snprintf(caption, sizeof(caption), "Price %s", metric_status(inputs.cost_state == PanelValueState::Current
                ? PanelValueState::Unavailable : inputs.cost_state));
        }
        error = add_line(presentation, body_font, caption, 294, price_color);
        if (error == ESP_OK) {
            presentation.price_visible = true;
            presentation.price_capsule.bounds = {89, 288, 288, 38};
            presentation.price_capsule.top_color = CODY2D::blend_rgb565(price_color, 0, 35);
            presentation.price_capsule.bottom_color = CODY2D::blend_rgb565(price_color, 0, 12);
            presentation.price_capsule.border_color = CODY2D::blend_rgb565(price_color, 0, 110);
            presentation.price_capsule.enabled = false;
            const char *effort = inputs.effort_state == PanelValueState::Current ? inputs.effort : metric_status(inputs.effort_state);
            const int count = std::snprintf(caption, sizeof(caption), "Thinking %s", effort);
            if (count < 0 || count >= static_cast<int>(sizeof(caption))) { return dashboard_result("effort_format", ESP_ERR_INVALID_SIZE); }
            const auto &effort_font = text_width(secondary_font, caption) <= 320 ? secondary_font : body_font;
            error = add_line(presentation, effort_font, caption, 350, kMuted);
        }
    } else {
        char caption[48]{};
        const char *status = inputs.link != PanelValueState::Current ? metric_status(inputs.link) :
            inputs.connected ? "PC connected" : link.retained_link_present && link.state == LUNASHELL::CodexLinkState::Discovering
                ? "Reconnecting" : "PC disconnected";
        if (link.last_error != ESP_OK && link.state != LUNASHELL::CodexLinkState::Discovering) {
            std::snprintf(caption, sizeof(caption), "Link error 0x%X", static_cast<unsigned>(link.last_error));
            status = caption;
        }
        error = add_line(presentation, body_font, status, 276,
            inputs.link == PanelValueState::Current && inputs.connected && link.last_error == ESP_OK ? kMint : kWhite);
        if (error == ESP_OK) {
            if (std::strlen(inputs.chat_id) == 64) {
                std::snprintf(caption, sizeof(caption), "Chat %.6s...%.6s", inputs.chat_id, inputs.chat_id + 58);
            } else {
                std::strcpy(caption, "Chat UNAVAILABLE");
            }
            error = add_line(presentation, secondary_font, caption, 319, kMuted);
        }
        if (error == ESP_OK) {
            if (device_ip_state == PanelValueState::Current && (device_ip[0] == '\0' || std::strlen(device_ip) > 15)) {
                return dashboard_result("device_ip_value", ESP_ERR_INVALID_ARG);
            }
            const int count = std::snprintf(caption, sizeof(caption), "Device IP %s",
                device_ip_state == PanelValueState::Current ? device_ip : metric_status(device_ip_state));
            if (count < 0 || count >= static_cast<int>(sizeof(caption))) { return dashboard_result("device_ip_format", ESP_ERR_INVALID_SIZE); }
            if (text_width(secondary_font, caption) <= 320) {
                error = add_line(presentation, secondary_font, caption, 362, kMuted);
            } else {
                // Full IPv4 values keep every octet in the same panel instead of scrolling or clipping.
                error = add_line(presentation, secondary_font, "Device IP", 346, kMuted);
                if (error == ESP_OK) {
                    error = add_line(presentation, secondary_font,
                        device_ip_state == PanelValueState::Current ? device_ip : metric_status(device_ip_state), 370, kWhite);
                }
            }
        }
    }
    if (error != ESP_OK) { return dashboard_result("prepare_content", error); }
    bool changed = !previously_valid || previous_panel != presentation.panel || previous_arc != presentation.arc_state ||
        previous_units != presentation.arc_units || previous_step != presentation.animation_step ||
        std::memcmp(presentation.prior_captions, presentation.captions, sizeof(presentation.captions)) != 0;
    for (unsigned index = 0; index < std::size(presentation.lines); ++index) {
        const auto &line = presentation.lines[index];
        changed = changed || presentation.prior_colors[index] != line.color ||
            presentation.prior_origins[index].x != line.origin.x || presentation.prior_origins[index].y != line.origin.y ||
            presentation.prior_fonts[index] != line.font;
    }
    if (changed) { ++presentation.revision; }
    presentation.valid = true;
    content = {};
    content.bounds = kHomeDashboardBounds;
    content.state = &presentation;
    content.revision = presentation.revision;
    content.draw_rows = dashboard_callback;
    return ESP_OK;
}

// presentation: Valid immutable dashboard state retained by its compositor owner.
// strip: Caller-owned tile preserving native or panel RGB565 ordering.
// first_row: Logical tile y origin.
// first_column: Logical tile x origin.
// Exception: Returns the original native validation, ribbon or glyph error.
esp_err_t draw_home_dashboard_rows(const HomeDashboardPresentation &presentation,
    CODY2D::Surface strip, uint16_t first_row, uint16_t first_column)
{
    if (!presentation.valid) { return dashboard_result("draw_state", ESP_ERR_INVALID_STATE); }
    esp_err_t error = ESP_OK;
    for (unsigned index = 0; error == ESP_OK && index < presentation.arc_path_count; ++index) {
        const auto &path = presentation.arc_paths[index];
        error = CODY2D::fill_path_rows(strip, first_row, path.vertices, path.vertex_count,
            kArcPalette, path.color_slot, path.color_slot, 255, first_column, CODY2D::FillAntialiasing::Coverage);
    }
    if (error == ESP_OK && presentation.price_visible) {
        error = CODY2D::draw_capsule_rows(presentation.price_capsule, strip, first_row, first_column);
    }
    for (unsigned row = 0; error == ESP_OK && row < presentation.line_count; ++row) {
        error = CODY2D::draw_text_rows(presentation.lines[row], presentation.lines[row].text.length, strip, first_row, first_column);
    }
    return dashboard_result("draw_rows", error);
}
} // namespace CODY

#include "CODY_prices_view.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <limits>

#include "CODY_flat_menu.hpp"
#include "CODY_page_marks.hpp"
#include "CODY_screen_content.hpp"
#include "CODY_ui_components.hpp"
#include "LUNASHELL_device_snapshot.hpp"

namespace CODY {
namespace {
constexpr DamageRect kBounds{73, 80, 393, 419};
constexpr int64_t kOldAfterUs = 24LL * 60 * 60 * 1000000;

// value: Raw USD per token from OpenRouter.
// valid: Source explicitly supplied a valid nonnegative price.
// text, size: Caller-owned caption with a semantic label.
// prefix: User-visible input, output or cached-input label.
// Exception: Unavailable or unrepresentable values are labelled explicitly.
void format_price(double value, bool valid, char *text, size_t size, const char *prefix)
{
    const double scaled = value * 10000000000.0; // USD/token to ten-thousandths of USD/million.
    if (!valid || !std::isfinite(scaled) || scaled < 0) {
        std::snprintf(text, size, "%s: N/A", prefix);
    } else if (scaled > 999999999999.0) {
        std::snprintf(text, size, "%s: Too large", prefix);
    } else if (scaled > 0 && scaled < 1) {
        std::snprintf(text, size, "%s: <$0.0001", prefix);
    } else {
        const auto fixed = static_cast<unsigned long long>(std::llround(scaled));
        std::snprintf(text, size, "%s: $%llu.%04llu", prefix, fixed / 10000ULL, fixed % 10000ULL);
    }
}

// font, source, top, color: One original caption and its logical line slot.
// view, index: Display-owned storage and prepared line retained by the frame.
// Exception: Preserves font measurement and bounds failures.
esp_err_t prepare_caption(const CODY2D::MonoFont &font, const char *source, uint16_t top,
    uint16_t color, PricesView &view, size_t index)
{
    if (index >= view.lines.size()) return ESP_ERR_INVALID_ARG;
    return FlatMenuRow::prepare_label(font, source, {93, top, 280, 25}, color,
        view.labels[index], sizeof(view.labels[index]), view.lines[index]);
}

// state: Immutable caller-owned presentation through complete frame retirement.
// strip: Borrowed RGB565 tile; first_row and first_column are logical origins.
// Exception: Preserves the first native row, caption or indicator error.
esp_err_t draw_rows(const void *state, CODY2D::Surface strip, uint16_t first_row, uint16_t first_column)
{
    if (!state) return ESP_ERR_INVALID_ARG;
    const auto &view = *static_cast<const PricesView *>(state);
    auto error = CODY2D::validate_surface(strip);
    for (const auto &line : view.lines) {
        if (error == ESP_OK && line.valid)
            error = CODY2D::draw_text_rows(line, line.text.length, strip, first_row, first_column);
    }
    for (size_t i = 0; error == ESP_OK && i < view.row_count && !view.detail; ++i)
        error = FlatMenuRow::draw(view.rows[i], 24, strip, first_row, first_column);
    const unsigned pages = (view.status.count + PricesView::kRows - 1U) / PricesView::kRows;
    if (error == ESP_OK && !view.detail && pages > 1) {
        const auto visible = static_cast<uint8_t>(std::min<unsigned>(7, pages));
        const unsigned first = pages <= 7 ? 0 : std::min<unsigned>(view.page > 3 ? view.page - 3 : 0, pages - 7);
        error = draw_page_marks(static_cast<uint8_t>(view.page - first), visible, strip, first_row, first_column, 400, true);
    }
    return error;
}

// view: Prepared display owner; content receives a borrowed immutable callback.
// Exception: Preserves native heading preparation failure.
esp_err_t attach(const PricesView &view, SceneContent &content)
{
    ScreenHeader header{};
    header.title = {"Model prices", 12};
    header.title_bounds = {73, 83, 320, 40};
    return ScreenContent::attach(content, kBounds, &view, view.revision, draw_rows, header);
}

// before, after: Explicit source status fields used by this presentation.
// Exception: None; no memory padding or inferred source data is compared.
bool same_status(const LUNASHELL::OpenRouterCatalogStatus &before,
    const LUNASHELL::OpenRouterCatalogStatus &after)
{
    return before.generation == after.generation && before.count == after.count && before.valid == after.valid &&
        before.truncated == after.truncated && before.last_error == after.last_error && before.stage == after.stage &&
        before.fetched_monotonic_us == after.fetched_monotonic_us &&
        before.timestamp_valid == after.timestamp_valid &&
        before.clock_observed_utc_seconds == after.clock_observed_utc_seconds;
}

// id: Exact catalog identity retained separately from its compact display label.
// Exception: Unknown namespaces remain unchanged.
const char *model_label(const char *id)
{
    return std::strncmp(id, "openai/", 7) == 0 ? id + 7 : id;
}

// status, quote: Canonical catalog status and optional selected quote.
// now_us: Local monotonic clock used only for explicit age validation.
// Exception: Missing quotes are unavailable, not old zero-valued prices.
bool is_old_quote(const LUNASHELL::OpenRouterCatalogStatus &status,
    const LUNASHELL::OpenRouterModelQuote *quote, int64_t now_us)
{
    const int64_t fetched = quote ? quote->fetched_monotonic_us : status.fetched_monotonic_us;
    if ((quote && !quote->quote_available && !quote->model_not_found) || fetched <= 0) return false;
    const bool prerequisites_missing = status.stage == LUNASHELL::OpenRouterCatalogStage::Wifi ||
        status.stage == LUNASHELL::OpenRouterCatalogStage::Clock;
    const bool failed_after_observation = quote && quote->last_error != ESP_OK &&
        !(quote->model_not_found && quote->last_error == ESP_ERR_NOT_FOUND);
    return prerequisites_missing || failed_after_observation || now_us <= 0 ||
        now_us < fetched || now_us - fetched > kOldAfterUs;
}
} // namespace

// view, font, now_us, content: See the public presenter contract.
// Exception: Preserves source/typography errors, including a refresh-generation race.
esp_err_t prepare_prices_view(PricesView &view, const CODY2D::MonoFont &font,
    int64_t now_us, SceneContent &content)
{
    LUNASHELL::OpenRouterCatalogStatus status{};
    auto &snapshots = LUNASHELL::DeviceSnapshotService::instance();
    auto read = snapshots.readOpenRouterCatalogStatus(status);
    if (read != ESP_OK) { status.last_error = read; status.valid = false; }
    const auto *previous_quote = view.detail && view.selected < view.row_count ? &view.quotes[view.selected] : nullptr;
    bool old = is_old_quote(status, previous_quote, now_us);
    bool row_ages_same = true;
    if (!view.detail) {
        for (size_t i = 0; i < view.row_count; ++i)
            if (view.row_old[i] != is_old_quote(status, &view.quotes[i], now_us)) row_ages_same = false;
    }
    if (view.prepared && view.font == &font && same_status(view.status, status) &&
        view.old_quote == old && row_ages_same)
        return attach(view, content);
    if (view.revision == std::numeric_limits<uint32_t>::max()) return ESP_ERR_INVALID_STATE;
    char selected_id[sizeof(view.quotes[0].id)]{};
    if (view.detail && view.selected < view.row_count)
        std::memcpy(selected_id, view.quotes[view.selected].id, sizeof(selected_id));
    view.row_count = 0;
    size_t pages = 0;
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        pages = (status.count + PricesView::kRows - 1U) / PricesView::kRows;
        if (pages && view.page >= pages) view.page = static_cast<uint16_t>(pages - 1U);
        if (!status.valid || !status.count) { view.detail = false; break; }
        size_t written = 0;
        read = snapshots.readOpenRouterCatalogPage(
            status.generation, view.page * PricesView::kRows, view.quotes.data(), view.quotes.size(), written);
        if (read == ESP_OK) {
            view.row_count = static_cast<uint8_t>(written);
            if (view.selected >= view.row_count || (view.detail &&
                std::strcmp(selected_id, view.quotes[view.selected].id) != 0)) {
                view.selected = 0; view.detail = false;
            }
            break;
        }
        if (read == ESP_ERR_INVALID_STATE && attempt == 0) {
            read = snapshots.readOpenRouterCatalogStatus(status);
            if (read == ESP_OK) continue;
        }
        status.valid = false;
        status.last_error = read;
        view.detail = false;
        break;
    }
    const auto *selected_quote = view.detail && view.selected < view.row_count ? &view.quotes[view.selected] : nullptr;
    old = is_old_quote(status, selected_quote, now_us);
    view.status = status;
    view.old_quote = old;
    view.lines = {};
    view.rows = {};

    auto error = prepare_caption(font, old ? "OpenRouter OLD USD/M" : "OpenRouter USD/M",
        126, old ? UI::Palette::warning : UI::Palette::muted, view, 0);
    if (error != ESP_OK) return error;
    const bool checked_only = selected_quote && !selected_quote->quote_available;
    const bool authoritative = selected_quote && (selected_quote->quote_available || selected_quote->model_not_found);
    const bool clock_valid = selected_quote ? (authoritative ? selected_quote->timestamp_valid : selected_quote->attempt_clock_valid) : status.timestamp_valid;
    const int64_t clock_epoch = selected_quote ? (authoritative ? selected_quote->clock_observed_utc_seconds : selected_quote->attempt_clock_utc_seconds) : status.clock_observed_utc_seconds;
    const int64_t clock_mono = selected_quote ? (authoritative ? selected_quote->clock_observed_monotonic_us : selected_quote->attempt_clock_monotonic_us) : status.clock_observed_monotonic_us;
    const int64_t event_mono = selected_quote ? (authoritative ? selected_quote->fetched_monotonic_us : selected_quote->attempt_completed_monotonic_us) : status.fetched_monotonic_us;
    char clock_text[48];
    std::snprintf(clock_text, sizeof(clock_text), selected_quote ?
        (checked_only ? "Checked: N/A" : "Updated: N/A") : "Latest: N/A");
    if (clock_valid && clock_epoch > 0 && clock_epoch <= std::numeric_limits<int64_t>::max() - 2 &&
        clock_mono > 0 && event_mono > 0) {
        const int64_t delta = event_mono - clock_mono;
        if (delta >= -2000000 && delta <= 2000000) {
            const int64_t estimated = clock_epoch + delta / 1000000;
            const time_t epoch = static_cast<time_t>(estimated);
            struct tm utc{};
            if (estimated > 0 && static_cast<int64_t>(epoch) == estimated && gmtime_r(&epoch, &utc))
                std::snprintf(clock_text, sizeof(clock_text), selected_quote ?
                    (checked_only ? "Checked~%02d-%02d %02d:%02d UTC" : "Updated~%02d-%02d %02d:%02d UTC") :
                    "Latest~%02d-%02d %02d:%02d UTC",
                    utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min);
        }
    }
    error = prepare_caption(font, clock_text, 154, UI::Palette::muted, view, 1);
    if (error != ESP_OK) return error;
    if (view.detail && view.row_count) {
        const auto &quote = view.quotes[view.selected];
        error = prepare_caption(font, model_label(quote.id), 182, UI::Palette::cyan, view, 2);
        char text[48]{};
        format_price(quote.input_usd_per_token, quote.input_valid, text, sizeof(text), "Input");
        if (error == ESP_OK) error = prepare_caption(font, text, 210, UI::Palette::text, view, 3);
        format_price(quote.output_usd_per_token, quote.output_valid, text, sizeof(text), "Output");
        if (error == ESP_OK) error = prepare_caption(font, text, 238, UI::Palette::text, view, 4);
        format_price(quote.cached_input_usd_per_token, quote.cached_input_valid, text, sizeof(text), "Cache");
        if (error == ESP_OK) error = prepare_caption(font, text, 266, UI::Palette::text, view, 5);
        if (quote.context_valid) std::snprintf(text, sizeof(text), "Context: %llu", static_cast<unsigned long long>(quote.context_tokens));
        else std::snprintf(text, sizeof(text), "Context: N/A");
        if (error == ESP_OK) error = prepare_caption(font, text, 294, UI::Palette::text, view, 6);
        if (quote.max_output_valid) std::snprintf(text, sizeof(text), "Max out: %llu", static_cast<unsigned long long>(quote.max_output_tokens));
        else std::snprintf(text, sizeof(text), "Max out: N/A");
        if (error == ESP_OK) error = prepare_caption(font, text, 322, UI::Palette::text, view, 7);
        const char *note = !quote.quote_available ?
            (quote.model_not_found ? "No OpenRouter quote" : "Not fetched") :
            !quote.tiered_valid ? "Base / tiers unknown" : quote.tiered ? "Base / tiers apply" : "Base rate";
        if (quote.last_error != ESP_OK && quote.last_error != ESP_ERR_NOT_FOUND &&
            quote.attempt_completed_monotonic_us > 0) {
            const char *tier = !quote.quote_available ? "N/A" :
                !quote.tiered_valid ? "Tier?" : quote.tiered ? "Tiers" : "Base";
            if (quote.http_status && quote.http_status != 200)
                std::snprintf(text, sizeof(text), "%s / HTTP %d", tier, quote.http_status);
            else if (quote.http_status == 200)
                std::snprintf(text, sizeof(text), "Invalid response");
            else
                std::snprintf(text, sizeof(text), "%s", quote.quote_available ? "Update failed" : "Fetch failed");
            note = text;
        }
        if (error == ESP_OK) error = prepare_caption(font, note, 350, UI::Palette::warning, view, 8);
    } else if (!view.row_count) {
        char message[48];
        if (!status.valid) std::snprintf(message, sizeof(message), "Unavailable: 0x%lx", static_cast<unsigned long>(status.last_error));
        else std::snprintf(message, sizeof(message), "No models available");
        error = prepare_caption(font, message, 216, UI::Palette::muted, view, 2);
    } else {
        for (size_t i = 0; error == ESP_OK && i < view.row_count; ++i) {
            auto &row = view.rows[i];
            const auto &model = view.quotes[i];
            const bool price_available = model.quote_available &&
                (model.input_valid || model.output_valid || model.cached_input_valid);
            const bool row_old = is_old_quote(status, &model, now_us);
            view.row_old[i] = row_old;
            row.bounds = {93, static_cast<uint16_t>(182 + 60 * i), 280, 58};
            row.enabled = true;
            row.icon = CODY2D::ActionIcon::Statistics;
            row.icon_color = price_available && !row_old ? UI::Palette::cyan : UI::Palette::warning;
            row.icon_center = {static_cast<int16_t>(row.bounds.x + 24), static_cast<int16_t>(row.bounds.y + 29)};
            char name[80];
            const char *prefix = row_old ? (price_available ? "OLD " : "OLD N/A ") :
                (price_available ? "" : "N/A ");
            std::snprintf(name, sizeof(name), "%s%s", prefix, model_label(model.id));
            error = FlatMenuRow::prepare_label(font, name,
                {static_cast<uint16_t>(row.bounds.x + 48), row.bounds.y, 232, row.bounds.height},
                UI::Palette::text, view.row_labels[i], sizeof(view.row_labels[i]), row.caption);
        }
        char page_text[48];
        std::snprintf(page_text, sizeof(page_text), status.truncated ? "Page %u/%u / limited" : "Page %u/%u",
            unsigned(view.page + 1U), unsigned(pages));
        if (error == ESP_OK) error = prepare_caption(font, page_text, 366, UI::Palette::muted, view, 2);
    }
    if (error != ESP_OK) return error;
    ++view.revision;
    view.font = &font;
    view.prepared = true;
    return attach(view, content);
}

// view, direction: Change page only within the current published list.
// Exception: None; out-of-range swipes are ignored.
bool prices_move_page(PricesView &view, int direction)
{
    if (!view.prepared || view.detail || (direction != -1 && direction != 1)) return false;
    const int pages = (view.status.count + PricesView::kRows - 1U) / PricesView::kRows;
    const int next = static_cast<int>(view.page) + direction;
    if (next < 0 || next >= pages) return false;
    view.page = static_cast<uint16_t>(next);
    view.selected = 0;
    view.prepared = false;
    return true;
}

// view, point: Select only an enabled row of the retired list.
// Exception: None; points outside a row leave the list unchanged.
bool prices_select_at(PricesView &view, CODY2D::Point point)
{
    if (!view.prepared || view.detail) return false;
    for (size_t i = 0; i < view.row_count; ++i) {
        const auto &r = view.rows[i].bounds;
        if (point.x >= r.x && point.y >= r.y && point.x < r.x + r.width && point.y < r.y + r.height) {
            view.selected = static_cast<uint8_t>(i); view.detail = true; view.prepared = false; return true;
        }
    }
    return false;
}

// view: Return from details to the same list page after frame retirement.
// Exception: None; returns false when already on the list.
bool prices_back(PricesView &view)
{
    if (!view.detail) return false;
    view.detail = false; view.prepared = false; return true;
}
} // namespace CODY

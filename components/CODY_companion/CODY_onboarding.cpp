#include "CODY_onboarding.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>

#include "CODY2D_controls.hpp"
#include "CODY_jelly.hpp"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_platform.h"
#include "scene_data.hpp"
#include "navigation_data.hpp"
#include "esp_log.h"
#include "qrcode.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace CODY {
namespace {
constexpr char kTag[] = "CODY_onboarding";
constexpr char kInstallUrl[] = "https://play.google.com/store/apps/details?id=com.espressif.provble";
constexpr uint16_t kQrModulesMax = 57; // Version 10; bounded independently of the framebuffer.
constexpr uint16_t kQrBytes = (kQrModulesMax * kQrModulesMax + 7U) / 8U;
constexpr uint16_t kQrSide = 270;
constexpr uint16_t kQrQuietModules = 4;
constexpr uint16_t kQrLeft = 98;
constexpr uint16_t kQrTop = 147;
constexpr uint16_t kBlueTop = 0x765E; // Authored #75CBF3 anchor.
constexpr uint16_t kBlueBottom = 0x34FC; // Authored #359FE7 anchor.
constexpr uint16_t kLookNeutralTop = 0x2946;
constexpr uint16_t kLookNeutralBottom = 0x18C4;
constexpr uint16_t kCaptionWidth = 232;
constexpr CODY2D::Rect kForwardBounds{396, 203, 60, 60};
constexpr CODY2D::Rect kBackBounds{10, 203, 60, 60};
constexpr CODY2D::Rect kLookListViewport{117, 226, 232, 146};
constexpr CODY2D::Rect kLookItemBase{117, 226, 220, 38};
constexpr uint16_t kLookItemStep = 44;
constexpr uint16_t kLookScrollBarLeft = 343;
constexpr uint16_t kLookScrollBarWidth = 4;
constexpr uint16_t kLookDragThreshold = 5;
constexpr size_t kLookItemCapacity = 8;
constexpr int64_t kHelloReadableHoldUs = 1000000;
using PairState = LUNASHELL::PairingSessionState;

enum class Route : uint8_t {
    None, Hello04, Invitation05, Install06, Scan06Bis, Pairing07, Failed08, Online09, Look11
};
enum class Work : uint8_t { InstallQr, SessionQr, Start, Retry, Cancel };
enum class NavigationDirection : uint8_t { Previous, Next };
enum class OnboardingAction : uint8_t { None, ShowQr, PairCody, Cancel, Retry, Continue, Done };
enum class GestureAxis : uint8_t { Undecided, Horizontal, Vertical };

struct QrModules {
    uint8_t bits[kQrBytes]{};
    uint16_t side = 0;
};
struct Command {
    Work work = Work::InstallQr;
    uint32_t generation = 0;
    uint32_t attempt = 0;
    uint32_t session = 0;
};
struct Result {
    Command command{};
    esp_err_t error = ESP_OK;
    QrModules qr{};
};
struct LookItem {
    const char *id = nullptr;
    CODY2D::CapsuleControl control{};
};
struct ContentView {
    CODY2D::TextLine lines[3]{};
    uint8_t line_count = 0;
    uint16_t visible_characters[3]{};
    CODY2D::CapsuleControl action{};
    bool action_visible = false;
    OnboardingAction action_id = OnboardingAction::None;
    QrModules qr{};
    char error_text[32]{};
    LookItem look_items[kLookItemCapacity]{};
    const char *selected_look_id = nullptr;
    uint8_t look_count = 0;
    uint16_t look_scroll = 0;
    bool look_list_visible = false;
};
ContentView s_view;
Route s_route = Route::None;
uint32_t s_generation = 1;
uint32_t s_revision = 0;
uint32_t s_navigation_revision = 0;
uint32_t s_attempt = 0;
uint32_t s_session = 0;
bool s_command_pending = false;
bool s_cancel_to_install = false;
bool s_pairing_owned = false;
bool s_online_link = false;
int64_t s_hello_completed_at_us = -1;
int64_t s_last_pairing_change_us = -1;
esp_err_t s_visible_error = ESP_OK;
Work s_failed_work = Work::Start;
bool s_local_failure = false;
bool s_look_touch_active = false;
bool s_look_touch_dragged = false;
int16_t s_look_touch_start_y = 0;
uint16_t s_look_scroll_start = 0;
bool s_swipe_active = false;
CODY2D::Point s_swipe_start{};
CODY2D::Point s_swipe_last{};
Route s_swipe_route = Route::None;
uint16_t s_swipe_rotation = 0;
GestureAxis s_swipe_axis = GestureAxis::Undecided;

// A single-slot mailbox keeps blocking radio admission and QR encoding off the
// display worker. Release/acquire transfers ownership; no per-frame queue allocation.
TaskHandle_t s_worker = nullptr;
StaticTask_t s_worker_control{};
StackType_t s_worker_stack[4096]{};
std::atomic<bool> s_work_busy{false};
std::atomic<bool> s_result_ready{false};
Command s_command{};
Result s_result{};
QrModules *s_encoding_target = nullptr; // Only the action worker calls the QR library.

// bytes: Owned storage that may contain a proof-of-possession-derived QR.
// size: Exact writable byte count; volatile stores are not optimized away.
void wipe(void *bytes, size_t size)
{
    volatile uint8_t *cursor = static_cast<volatile uint8_t *>(bytes);
    while (size-- != 0) {
        *cursor++ = 0;
    }
}

// handle: Library-owned QR, borrowed only for this synchronous callback.
void copy_qr_modules(esp_qrcode_handle_t handle)
{
    if (handle == nullptr || s_encoding_target == nullptr) {
        return;
    }
    const int side = esp_qrcode_get_size(handle);
    if (side < 21 || side > kQrModulesMax) {
        return;
    }
    s_encoding_target->side = static_cast<uint16_t>(side);
    for (uint16_t row = 0; row < side; ++row) {
        for (uint16_t column = 0; column < side; ++column) {
            if (esp_qrcode_get_module(handle, column, row)) {
                const uint32_t bit = row * side + column;
                s_encoding_target->bits[bit / 8U] |= 1U << (bit % 8U);
            }
        }
    }
}

// payload: Counted by the underlying library; never logged or published in snapshots.
// qr: Receives compact modules, never a full RGB framebuffer.
esp_err_t encode_qr(const char *payload, QrModules &qr)
{
    wipe(&qr, sizeof(qr));
    esp_qrcode_config_t config = ESP_QRCODE_CONFIG_DEFAULT();
    config.max_qrcode_version = 10;
    config.qrcode_ecc_level = ESP_QRCODE_ECC_MED;
    config.display_func = copy_qr_modules; // Never use the console QR callback.
    s_encoding_target = &qr;
    const esp_err_t error = esp_qrcode_generate(&config, payload);
    s_encoding_target = nullptr;
    if (error != ESP_OK) {
        return error;
    }
    return qr.side != 0 ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

// command: Typed action and attempt/session identities acquired by the route owner.
// result: Original operation result and optional bounded QR modules.
void execute_command(const Command &command, Result &result)
{
    result.command = command;
    if (command.work == Work::InstallQr) {
        result.error = encode_qr(kInstallUrl, result.qr);
        return;
    }
    if (command.work == Work::SessionQr) {
        LUNASHELL::ConnectivityObservation observation;
        result.error = LUNASHELL::DeviceSnapshotService::instance().readConnectivityObservation(observation);
        if (result.error != ESP_OK) {
            return;
        }
        const auto &pairing = observation.radios.pairing;
        if (!pairing.data_valid || pairing.data_stale || pairing.state != PairState::ReadyToScan ||
            pairing.attempt_id != command.attempt || pairing.provisioning_session_id != command.session) {
            result.error = ESP_ERR_INVALID_STATE;
            return;
        }
        char payload[192]{};
        result.error = LUNASHELL_provisioning_copy_qr(payload, sizeof(payload), command.session);
        if (result.error == ESP_OK) {
            result.error = encode_qr(payload, result.qr);
        }
        wipe(payload, sizeof(payload));
        return;
    }
    LUNASHELL_pairing_action_t action;
    switch (command.work) {
    case Work::Start:
        action = LUNASHELL_PAIRING_ACTION_START;
        break;
    case Work::Retry:
        action = LUNASHELL_PAIRING_ACTION_RETRY;
        break;
    case Work::Cancel:
        action = LUNASHELL_PAIRING_ACTION_CANCEL;
        break;
    default:
        result.error = ESP_ERR_INVALID_ARG;
        return;
    }
    result.error = LUNASHELL_pairing_request(action, command.attempt);
}

// context: Unused; one worker exclusively owns library callbacks and mailbox results.
void action_worker(void *context)
{
    (void)context;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        // The acquire pairs with enqueue's release, before reading its mailbox.
        if (!s_work_busy.load(std::memory_order_acquire)) {
            continue;
        }
        Result completed{};
        execute_command(s_command, completed);
        if (completed.error != ESP_OK) {
            ESP_LOGE(kTag, "Action %u attempt=%lu session=%lu failed (%s)",
                to_index(completed.command.work), static_cast<unsigned long>(completed.command.attempt),
                static_cast<unsigned long>(completed.command.session), esp_err_to_name(completed.error));
        }
        s_result = completed;
        wipe(&completed, sizeof(completed));
        s_result_ready.store(true, std::memory_order_release);
    }
}

// work: Explicit radio or QR preparation action.
// attempt: Current canonical attempt identity; never an inferred UI list position.
// session: Current canonical provisioning-session identity, or zero for non-QR actions.
esp_err_t enqueue(Work work, uint32_t attempt, uint32_t session = 0)
{
    if (s_worker == nullptr || s_work_busy.load(std::memory_order_acquire)) {
        ESP_LOGE(kTag, "Action mailbox unavailable (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    s_command = {work, s_generation, attempt, session};
    s_command_pending = true;
    ++s_navigation_revision;
    s_work_busy.store(true, std::memory_order_release);
    xTaskNotifyGive(s_worker);
    return ESP_OK;
}

// text: Bounded immutable English text, retained through frame retirement.
// top: Logical caption baseline container, entirely inside the shared content chord.
// line: Receives the prepared flash-font layout.
// Exception: Returns ESP_ERR_INVALID_SIZE for empty, unsupported or out-of-canvas text.
esp_err_t prepare_caption(const char *text, uint16_t top, CODY2D::TextLine &line)
{
    const auto &font = SceneData::body_font;
    const size_t length = std::strlen(text);
    const uint32_t width = length == 0 ? 0 : (length - 1U) * font.advance + font.width;
    if (length == 0 || length > 31 || width > kCaptionWidth || top + font.height > 466) {
        ESP_LOGE(kTag, "Caption rejected: length=%u width=%lu top=%u (%s)",
                 static_cast<unsigned>(length), static_cast<unsigned long>(width), top,
                 esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    return CODY2D::prepare_text_line(font, {text, static_cast<uint16_t>(length)},
        {static_cast<int16_t>((466U - width) / 2U), static_cast<int16_t>(top)}, 0xFFFF, line);
}

// action: Explicit route action; never inferred from an English label.
// enabled: True only when the route has an admissible backend or navigation action.
// Exception: Rejects an unknown action identity without changing the view.
esp_err_t prepare_action(OnboardingAction action, bool enabled)
{
    switch (action) {
    case OnboardingAction::None:
    case OnboardingAction::ShowQr:
    case OnboardingAction::PairCody:
    case OnboardingAction::Cancel:
    case OnboardingAction::Retry:
    case OnboardingAction::Continue:
    case OnboardingAction::Done:
        break;
    default:
        ESP_LOGE(kTag, "Unknown navigation action %u (%s)", to_index(action), esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    if (s_view.action_id != action || s_view.action.enabled != enabled) {
        ++s_navigation_revision;
    }
    s_view.action_id = action;
    s_view.action_visible = action != OnboardingAction::None;
    const auto bounds = action == OnboardingAction::Cancel ? kBackBounds : kForwardBounds;
    s_view.action = {bounds, {}, NavigationData::top, NavigationData::bottom, enabled};
    return ESP_OK;
}

// No parameters: maximum list displacement that still ends on the last authored row.
uint16_t look_scroll_limit()
{
    if (s_view.look_count == 0) {
        return 0;
    }
    const uint32_t content_bottom = kLookItemBase.y +
        static_cast<uint32_t>(s_view.look_count - 1U) * kLookItemStep + kLookItemBase.height;
    const uint32_t viewport_bottom = kLookListViewport.y + kLookListViewport.height;
    return content_bottom > viewport_bottom
        ? static_cast<uint16_t>(content_bottom - viewport_bottom) : 0;
}

// No parameters: build the fixed-capacity list from stable catalog IDs and current preference.
// Exception: Returns the real catalog, font or unsupported-renderer error.
esp_err_t prepare_look_list()
{
#if !CODY_ENABLE_PEARL_JELLY
    ESP_LOGE(kTag, "Screen 11 requires the authored jelly look catalog (%s)",
             esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
#else
    const size_t count = jelly_skin_count();
    if (count == 0 || count > kLookItemCapacity) {
        ESP_LOGE(kTag, "Screen 11 look count rejected: %u (%s)",
                 static_cast<unsigned>(count), esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    JellySkinSelection selected;
    esp_err_t error = selected_jelly_skin(selected);
    if (error != ESP_OK) {
        return error;
    }
    s_view.look_list_visible = true;
    s_view.look_count = static_cast<uint8_t>(count);
    s_view.selected_look_id = selected.id;
    for (size_t index = 0; index < count; ++index) {
        JellySkinSelection entry;
        error = jelly_skin_at(index, entry);
        if (error != ESP_OK) {
            return error;
        }
        const uint16_t row_top = kLookItemBase.y + static_cast<uint16_t>(index) * kLookItemStep;
        auto &item = s_view.look_items[index];
        item.id = entry.id;
        item.control = {
            {kLookItemBase.x, row_top, kLookItemBase.width, kLookItemBase.height},
            {}, kLookNeutralTop, kLookNeutralBottom, true
        };
        const uint16_t caption_top = row_top +
            (kLookItemBase.height - SceneData::body_font.height) / 2U;
        error = prepare_caption(entry.display_name, caption_top, item.control.caption);
        if (error != ESP_OK) {
            return error;
        }
    }
    return prepare_action(OnboardingAction::Done, true);
#endif
}

// strip: Existing content-clipped tile retaining its original stride and byte order.
// first_row: Logical y origin of the tile.
// first_column: Logical x origin of the tile.
// bounds: Full-canvas rectangle to intersect with this tile.
// color: Native RGB565 fill color.
// Exception: Returns the real destination-surface fill error.
esp_err_t fill_canvas_rect(CODY2D::Surface strip, uint16_t first_row, uint16_t first_column,
                           CODY2D::Rect bounds, uint16_t color)
{
    const uint16_t top = std::max(first_row, bounds.y);
    const uint16_t left = std::max(first_column, bounds.x);
    const uint16_t bottom = std::min<uint16_t>(first_row + strip.height, bounds.y + bounds.height);
    const uint16_t right = std::min<uint16_t>(first_column + strip.width, bounds.x + bounds.width);
    if (right <= left || bottom <= top) {
        return ESP_OK;
    }
    return CODY2D::fill_rect(strip,
        {static_cast<uint16_t>(left - first_column), static_cast<uint16_t>(top - first_row),
         static_cast<uint16_t>(right - left), static_cast<uint16_t>(bottom - top)}, color);
}

// view: Immutable prepared list, selection and bounded scroll position.
// strip: Scene-content tile; rows are clipped again to the list viewport.
// first_row: Logical y origin shared with touch coordinates.
// first_column: Logical x origin shared with touch coordinates.
// Exception: Returns the real invalid-state, surface or text drawing error.
esp_err_t draw_look_list(const ContentView &view, CODY2D::Surface strip,
                         uint16_t first_row, uint16_t first_column)
{
    if (!view.look_list_visible || view.look_count == 0 ||
        view.look_count > kLookItemCapacity || view.selected_look_id == nullptr) {
        if (!view.look_list_visible) {
            return ESP_OK;
        }
        ESP_LOGE(kTag, "Look list draw state rejected: count=%u selected=%s (%s)",
                 view.look_count, view.selected_look_id != nullptr ? view.selected_look_id : "<null>",
                 esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    const uint16_t clipped_top = std::max(first_row, kLookListViewport.y);
    const uint16_t clipped_left = std::max(first_column, kLookListViewport.x);
    const uint16_t clipped_bottom = std::min<uint16_t>(first_row + strip.height,
        kLookListViewport.y + kLookListViewport.height);
    const uint16_t clipped_right = std::min<uint16_t>(first_column + strip.width,
        kLookListViewport.x + kLookListViewport.width);
    if (clipped_right <= clipped_left || clipped_bottom <= clipped_top) {
        return ESP_OK;
    }
    const uint32_t offset = static_cast<uint32_t>(clipped_top - first_row) * strip.stride +
        clipped_left - first_column;
    CODY2D::Surface list_strip = strip;
    list_strip.pixels += offset;
    list_strip.width = clipped_right - clipped_left;
    list_strip.height = clipped_bottom - clipped_top;
    list_strip.pixel_capacity -= offset;
    for (uint8_t index = 0; index < view.look_count; ++index) {
        const auto &item = view.look_items[index];
        if (item.id == nullptr || !item.control.caption.valid) {
            ESP_LOGE(kTag, "Look row draw state rejected: row=%u id=%s caption=%u (%s)",
                     index, item.id != nullptr ? item.id : "<null>", item.control.caption.valid,
                     esp_err_to_name(ESP_ERR_INVALID_STATE));
            return ESP_ERR_INVALID_STATE;
        }
        CODY2D::CapsuleControl row = item.control;
        row.bounds.y = static_cast<uint16_t>(row.bounds.y - view.look_scroll);
        row.caption.origin.y = static_cast<int16_t>(row.caption.origin.y - view.look_scroll);
        row.caption.bounds.y = static_cast<uint16_t>(row.caption.bounds.y - view.look_scroll);
        const bool selected = std::strcmp(item.id, view.selected_look_id) == 0;
        row.top_color = selected ? kBlueTop : kLookNeutralTop;
        row.bottom_color = selected ? kBlueBottom : kLookNeutralBottom;
        row.caption.color = selected ? 0x0001 : 0xFFFF;
        const esp_err_t error = CODY2D::draw_capsule_rows(
            row, list_strip, clipped_top, clipped_left);
        if (error != ESP_OK) {
            return error;
        }
    }
    const CODY2D::Rect track{kLookScrollBarLeft, kLookListViewport.y,
        kLookScrollBarWidth, kLookListViewport.height};
    esp_err_t error = fill_canvas_rect(list_strip, clipped_top, clipped_left, track, kLookNeutralBottom);
    const uint16_t limit = look_scroll_limit();
    const uint16_t thumb_height = limit == 0 ? kLookListViewport.height : 48;
    const uint16_t thumb_range = kLookListViewport.height - thumb_height;
    const uint16_t thumb_top = kLookListViewport.y +
        (limit == 0 ? 0 : static_cast<uint16_t>(view.look_scroll * thumb_range / limit));
    if (error == ESP_OK) {
        error = fill_canvas_rect(list_strip, clipped_top, clipped_left,
            {kLookScrollBarLeft, thumb_top, kLookScrollBarWidth, thumb_height}, kBlueTop);
    }
    return error;
}

// qr: Compact immutable module map from the encoder.
// strip: Already clipped caller-owned content tile.
// first_row: Logical y origin of the tile.
// first_column: Logical x origin of the tile.
// Exception: Rejects unsupported QR geometry before grid arithmetic or pixel writes.
esp_err_t draw_qr_rows(const QrModules &qr, CODY2D::Surface strip,
                       uint16_t first_row, uint16_t first_column)
{
    if (qr.side == 0) {
        return ESP_OK;
    }
    if (qr.side < 21 || qr.side > kQrModulesMax || (qr.side - 21U) % 4U != 0) {
        ESP_LOGE(kTag, "QR module extent rejected: side=%u (%s)", qr.side, esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    const uint16_t grid_side = qr.side + 2U * kQrQuietModules;
    const uint16_t scale = kQrSide / grid_side;
    if (scale == 0) {
        ESP_LOGE(kTag, "QR viewport cannot fit side=%u (%s)", qr.side, esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    const uint16_t inset = (kQrSide - grid_side * scale) / 2U;
    const uint16_t top = std::max(first_row, kQrTop);
    const uint16_t bottom = std::min<uint16_t>(first_row + strip.height, kQrTop + kQrSide);
    const uint16_t left = std::max(first_column, kQrLeft);
    const uint16_t right = std::min<uint16_t>(first_column + strip.width, kQrLeft + kQrSide);
    for (uint16_t row = top; row < bottom; ++row) {
        for (uint16_t column = left; column < right; ++column) {
            const int module_x = (static_cast<int>(column) - kQrLeft - inset) / scale - kQrQuietModules;
            const int module_y = (static_cast<int>(row) - kQrTop - inset) / scale - kQrQuietModules;
            const bool inside = column >= kQrLeft + inset && row >= kQrTop + inset &&
                module_x >= 0 && module_y >= 0 && module_x < qr.side && module_y < qr.side;
            const uint32_t bit = inside ? module_y * qr.side + module_x : 0;
            const bool dark = inside && (qr.bits[bit / 8U] & (1U << (bit % 8U))) != 0;
            strip.pixels[(row - first_row) * strip.stride + column - first_column] =
                CODY2D::rgb565_native_to_stored(dark ? 0 : 0xFFFF, strip.byte_order);
        }
    }
    return ESP_OK;
}

// state: Immutable content owned by this route through frame retirement.
// strip: Valid content-clipped tile with its original stride and byte order.
// first_row: Logical tile y origin.
// first_column: Logical tile x origin.
// Exception: Returns the first real QR, text or list drawing error.
esp_err_t draw_content(const void *state, CODY2D::Surface strip,
                       uint16_t first_row, uint16_t first_column)
{
    const auto &view = *static_cast<const ContentView *>(state);
    esp_err_t error = draw_qr_rows(view.qr, strip, first_row, first_column);
    for (uint8_t index = 0; error == ESP_OK && index < view.line_count; ++index) {
        error = CODY2D::draw_text_rows(view.lines[index], view.visible_characters[index],
            strip, first_row, first_column);
    }
    if (error == ESP_OK) {
        error = draw_look_list(view, strip, first_row, first_column);
    }
    return error;
}

// scene: Valid route receiving the current content revision.
// Exception: Returns the scene-content validation error without replacing the frame.
esp_err_t attach_content(SceneFrame &scene)
{
    uint16_t preceding = scene.definition->title_visible ? scene.title.text.length : 0;
    for (uint8_t index = 0; index < s_view.line_count; ++index) {
        s_view.visible_characters[index] = scene_content_characters(scene, s_view.lines[index].text.length, preceding);
        preceding += s_view.lines[index].text.length;
    }
    const DamageRect controls[] = {{10, 203, 70, 263}, {396, 203, 456, 263}};
    if (s_route == Route::Look11) {
        SceneContent content;
        content.bounds = {117, 226, 349, 436};
        content.state = &s_view;
        content.revision = ++s_revision;
        content.control_revision = s_navigation_revision;
        std::copy(std::begin(controls), std::end(controls), content.control_bounds);
        content.draw_rows = draw_content;
        return set_scene_content(scene, content);
    }
    const uint16_t top = s_route == Route::Install06 || s_route == Route::Scan06Bis ? 150 :
        s_route == Route::Invitation05 ? 230 : 346;
    SceneContent content;
    if (s_route == Route::Install06 || s_route == Route::Scan06Bis) {
        // Include the small caption above the QR. Every bounding corner remains
        // inside the circular display, and the lateral controls stay separate.
        content.bounds = {kQrLeft, 117, kQrLeft + kQrSide, kQrTop + kQrSide};
    } else {
        content.bounds = {117, top, 349, 428};
    }
    content.state = &s_view;
    content.revision = ++s_revision;
    content.control_revision = s_navigation_revision;
    std::copy(std::begin(controls), std::end(controls), content.control_bounds);
    content.draw_rows = draw_content;
    return set_scene_content(scene, content);
}

// route: Explicit atlas identity; no navigation is inferred from array position.
// scene: Receives a fully validated immutable layout and caller-owned content.
// Exception: Returns the real scene, catalog, text or content preparation error.
esp_err_t enter_route(Route route, SceneFrame &scene)
{
    SceneId identity;
    switch (route) {
    case Route::Invitation05:
        identity = SceneId::PairInvitation;
        break;
    case Route::Install06:
        identity = SceneId::InstallApp;
        break;
    case Route::Scan06Bis:
        identity = SceneId::ScanToPair;
        break;
    case Route::Pairing07:
        identity = SceneId::Pairing;
        break;
    case Route::Failed08:
        identity = SceneId::PairingFailed;
        break;
    case Route::Online09:
        identity = SceneId::Online;
        break;
    case Route::Look11:
        identity = SceneId::CodyLook;
        break;
    default:
        ESP_LOGE(kTag, "Unknown content route %u (%s)", to_index(route), esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    const SceneDefinition *definition = nullptr;
    esp_err_t error = find_scene(identity, definition);
    SceneFrame prepared;
    if (error == ESP_OK) {
        error = begin_scene(*definition, prepared);
    }
    if (error != ESP_OK) {
        return error;
    }
    // QR state is retired on every departure, including cancellation and failure.
    wipe(&s_view, sizeof(s_view));
    s_look_touch_active = false;
    s_look_touch_dragged = false;
    s_swipe_active = false;
    ++s_navigation_revision;
    s_route = route;
    if (route == Route::Invitation05) {
        constexpr const char *captions[] = {"We'll show a QR code.", "Scan it to install", "the pairing app."};
        for (const char *text : captions) {
            error = prepare_caption(text, 242 + s_view.line_count * 26U, s_view.lines[s_view.line_count]);
            if (error != ESP_OK) {
                return error;
            }
            ++s_view.line_count;
        }
        error = prepare_action(OnboardingAction::ShowQr, true);
    } else if (route == Route::Install06) {
        error = prepare_caption("1. Scan to install", 117, s_view.lines[0]);
        s_view.lines[0].color = prepared.definition->title_color;
        s_view.line_count = 1;
        if (error == ESP_OK) {
            error = prepare_action(OnboardingAction::PairCody, !s_command_pending);
        }
    } else if (route == Route::Scan06Bis) {
        error = prepare_caption("2. Scan in the app", 117, s_view.lines[0]);
        s_view.lines[0].color = prepared.definition->title_color;
        s_view.line_count = 1;
    } else if (route == Route::Pairing07) {
        error = prepare_action(OnboardingAction::Cancel, s_pairing_owned && !s_command_pending);
    } else if (route == Route::Failed08) {
        std::snprintf(s_view.error_text, sizeof(s_view.error_text), "Error: 0x%lx",
            static_cast<unsigned long>(s_visible_error));
        error = prepare_caption(s_view.error_text, 348, s_view.lines[0]);
        s_view.line_count = 1;
        if (error == ESP_OK) {
            error = prepare_action(OnboardingAction::None, false); // A current observation enables Retry/Cancel separately.
        }
    } else if (route == Route::Online09) {
        error = prepare_caption("Wi-Fi connected", 350, s_view.lines[0]);
        s_view.line_count = 1;
#if CODY_ENABLE_PEARL_JELLY
        if (error == ESP_OK) {
            error = prepare_action(OnboardingAction::Continue, true);
        }
#endif
    } else if (route == Route::Look11) {
        error = prepare_look_list();
    }
    if (error == ESP_OK) {
        error = attach_content(prepared);
    }
    if (error != ESP_OK) {
        return error;
    }
    scene = prepared;
    ESP_LOGI(kTag, "Atlas route entered: %s", definition->name);
    return ESP_OK;
}

// scene: Current route that displays a real local action/preparation error.
// error: Original operation error, not substituted by a successful presentation.
esp_err_t show_error(SceneFrame &scene, esp_err_t error)
{
    s_visible_error = error;
    return enter_route(Route::Failed08, scene);
}

} // namespace

// No parameters: all action storage is static; no framebuffer or per-frame allocation.
esp_err_t onboarding_service_init()
{
    if (s_worker != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    s_worker = xTaskCreateStatic(action_worker, "CODY_actions", sizeof(s_worker_stack), nullptr,
        3, s_worker_stack, &s_worker_control);
    if (s_worker == nullptr) {
        ESP_LOGE(kTag, "Action worker creation failed (%s)", esp_err_to_name(ESP_ERR_NO_MEM));
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

// No parameters: reset route ownership without stopping or inventing a radio session.
void reset_onboarding()
{
    ++s_generation;
    s_route = Route::None;
    s_command_pending = false;
    s_pairing_owned = false;
    s_cancel_to_install = false;
    s_attempt = 0;
    s_session = 0;
    s_last_pairing_change_us = -1;
    s_hello_completed_at_us = -1;
    s_local_failure = false;
    s_online_link = false;
    s_look_touch_active = false;
    s_look_touch_dragged = false;
    s_look_touch_start_y = 0;
    s_look_scroll_start = 0;
    s_swipe_active = false;
    s_swipe_route = Route::None;
    s_swipe_axis = GestureAxis::Undecided;
    wipe(&s_view, sizeof(s_view));
}

// No parameters: this is first setup, not a clock routine greeting.
void begin_onboarding()
{
    reset_onboarding();
    s_route = Route::Hello04;
}

// scene: Current first-setup scene, always owned by the existing display worker.
// completed_scene: Last fully retired text; never advance from a failed/partial frame.
// active_time_us: Existing animation time, excluding standby and pause.
// changed: Receives whether scene geometry or content needs a new frame.
// Exception: Returns the first real snapshot, command, route or content error.
esp_err_t update_onboarding(SceneFrame &scene, const SceneFrame &completed_scene,
                            int64_t active_time_us, bool &changed)
{
    changed = false;
    Result result;
    const bool have_result = s_result_ready.load(std::memory_order_acquire);
    if (have_result) {
        result = s_result;
        wipe(&s_result, sizeof(s_result));
        s_result_ready.store(false, std::memory_order_release);
        s_work_busy.store(false, std::memory_order_release);
    }
    if (s_route == Route::None) {
        wipe(&result, sizeof(result));
        return ESP_OK;
    }
    if (s_route == Route::Hello04) {
        wipe(&result, sizeof(result));
        const bool hello_retired = scene.valid && scene.definition != nullptr &&
            scene.visible_characters == scene.title.text.length &&
            completed_scene.valid && completed_scene.definition == scene.definition &&
            completed_scene.definition->id == SceneId::Greeting &&
            completed_scene.visible_characters == completed_scene.title.text.length;
        if (!hello_retired) {
            return ESP_OK;
        }
        if (s_hello_completed_at_us < 0) {
            s_hello_completed_at_us = active_time_us;
        }
        if (active_time_us - s_hello_completed_at_us < kHelloReadableHoldUs) {
            return ESP_OK;
        }
        changed = true;
        return enter_route(Route::Invitation05, scene);
    }
    if (!scene.valid || scene.definition == nullptr) {
        ESP_LOGE(kTag, "Active route has no prepared scene (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    if (scene.content.control_revision != s_navigation_revision) {
        scene.content.control_revision = s_navigation_revision;
        changed = true;
    }
    uint16_t preceding = scene.definition->title_visible ? scene.title.text.length : 0;
    bool caption_changed = false;
    for (uint8_t index = 0; index < s_view.line_count; ++index) {
        const uint16_t visible = scene_content_characters(scene, s_view.lines[index].text.length, preceding);
        caption_changed = caption_changed || visible != s_view.visible_characters[index];
        preceding += s_view.lines[index].text.length;
    }
    if (caption_changed) {
        const esp_err_t caption_error = attach_content(scene);
        if (caption_error != ESP_OK) {
            return caption_error;
        }
        changed = true;
    }
    if (s_route == Route::Look11) {
        wipe(&result, sizeof(result));
        return ESP_OK;
    }
    LUNASHELL::ConnectivityObservation observation;
    esp_err_t error = LUNASHELL::DeviceSnapshotService::instance().readConnectivityObservation(observation);
    if (error != ESP_OK) {
        wipe(&result, sizeof(result));
        return error;
    }
    const auto &pairing = observation.radios.pairing;
    if (have_result && result.command.generation == s_generation) {
        s_command_pending = false;
        scene.content.control_revision = ++s_navigation_revision;
        changed = true;
        if (result.error != ESP_OK) {
            // A QR copy can race credentials/cancellation: follow the newer real
            // state instead of treating an expired session as a renderer crash.
            const bool expired_qr = result.command.work == Work::SessionQr &&
                result.error == ESP_ERR_INVALID_STATE && pairing.data_valid &&
                (pairing.state != PairState::ReadyToScan || pairing.attempt_id != result.command.attempt ||
                 pairing.provisioning_session_id != result.command.session);
            if (!expired_qr) {
                s_failed_work = result.command.work;
                s_local_failure = true;
                error = show_error(scene, result.error);
                if (error == ESP_OK) {
                    error = prepare_action(OnboardingAction::Retry, true);
                    if (error == ESP_OK) {
                        error = attach_content(scene);
                    }
                }
            }
        } else if (result.command.work == Work::InstallQr && s_route == Route::Install06) {
            s_local_failure = false;
            s_view.qr = result.qr;
            error = prepare_action(OnboardingAction::PairCody, true);
            if (error == ESP_OK) {
                error = attach_content(scene);
            }
        } else if (result.command.work == Work::SessionQr && s_pairing_owned && pairing.data_valid &&
                   pairing.attempt_id == s_attempt && pairing.state == PairState::ReadyToScan &&
                   pairing.provisioning_session_id == result.command.session) {
            error = enter_route(Route::Scan06Bis, scene);
            if (error == ESP_OK) {
                s_local_failure = false;
                s_view.qr = result.qr;
                s_session = result.command.session;
                error = attach_content(scene);
            }
        } else if (result.command.work == Work::Start || result.command.work == Work::Retry) {
            s_local_failure = false;
            s_attempt = result.command.attempt + 1U; // Backend admission increments exactly once.
            s_pairing_owned = true;
            s_last_pairing_change_us = -1;
        }
        wipe(&result, sizeof(result));
        if (error != ESP_OK) {
            return error;
        }
    } else {
        wipe(&result, sizeof(result));
    }
    if (!s_pairing_owned) {
        return ESP_OK;
    }
    if (!pairing.data_valid || pairing.data_stale || pairing.attempt_id != s_attempt) {
        if (s_route != Route::Failed08) {
            changed = true;
            return show_error(scene, pairing.last_error != ESP_OK ? pairing.last_error : ESP_ERR_INVALID_STATE);
        }
        return ESP_OK;
    }
    if (pairing.changed_at_us != s_last_pairing_change_us) {
        changed = true;
        s_last_pairing_change_us = pairing.changed_at_us;
        if (pairing.state == PairState::Cancelled) {
            s_pairing_owned = false;
            s_cancel_to_install = false;
            error = enter_route(Route::Install06, scene);
            if (error == ESP_OK) {
                error = enqueue(Work::InstallQr, 0);
            }
        } else if ((pairing.state == PairState::Failed || pairing.state == PairState::TimedOut) &&
                   !s_command_pending) {
            s_local_failure = false;
            error = show_error(scene, pairing.cleanup_error != ESP_OK ? pairing.cleanup_error : pairing.last_error);
            if (error == ESP_OK) {
                const bool can_retry = pairing.cleanup_error == ESP_OK;
                const bool can_cancel = pairing.state == PairState::Failed && !can_retry;
                error = prepare_action(can_retry ? OnboardingAction::Retry : can_cancel ? OnboardingAction::Cancel : OnboardingAction::None,
                    (can_retry || can_cancel) && !s_command_pending);
                if (error == ESP_OK) {
                    error = attach_content(scene);
                }
            }
        } else if (pairing.state == PairState::Connecting || pairing.state == PairState::Completing ||
                   pairing.state == PairState::Cancelling || pairing.state == PairState::Starting) {
            if (s_route != Route::Pairing07) {
                error = enter_route(Route::Pairing07, scene);
            }
            if (error == ESP_OK) {
                error = prepare_action(OnboardingAction::Cancel, !s_command_pending &&
                    pairing.state != PairState::Cancelling && pairing.state != PairState::Completing);
                if (error == ESP_OK) {
                    error = attach_content(scene);
                }
            }
        } else if (pairing.state == PairState::Idle) {
            // A confirmed closed session may follow owner-controlled standby.
            // Never keep rendering the previous attempt's device QR.
            s_pairing_owned = false;
            s_cancel_to_install = false;
            error = enter_route(Route::Install06, scene);
            if (error == ESP_OK) {
                error = enqueue(Work::InstallQr, 0);
            }
        }
        if (error != ESP_OK) {
            return error;
        }
    }
    if (s_route != Route::Failed08 && pairing.state == PairState::ReadyToScan &&
        pairing.provisioning_session_id != s_session &&
        !s_command_pending && !s_cancel_to_install) {
        if (s_route == Route::Scan06Bis) {
            // Never leave an old session QR visible while a replacement encodes.
            changed = true;
            error = enter_route(Route::Pairing07, scene);
            if (error != ESP_OK) {
                return error;
            }
        }
        error = enqueue(Work::SessionQr, s_attempt, pairing.provisioning_session_id);
        if (error != ESP_OK) {
            changed = true;
            return show_error(scene, error);
        }
    }
    if (pairing.state == PairState::Online) {
        const auto &wifi = observation.radios.wifi;
        const bool online = wifi.data_valid && !wifi.data_stale && wifi.health == LUNASHELL::DeviceHealth::Ready &&
            wifi.last_error == ESP_OK && wifi.connected && wifi.ip_acquired;
        if (!online && s_route != Route::Online09) {
            // Manager completion alone is not an online route. Keep the pending
            // presentation until the same canonical observation has a live IP.
            if (s_route != Route::Pairing07 || s_view.line_count == 0 || s_view.action_visible) {
                changed = true;
                error = enter_route(Route::Pairing07, scene);
                if (error == ESP_OK) {
                    error = prepare_action(OnboardingAction::None, false);
                }
                if (error == ESP_OK) {
                    error = prepare_caption("Waiting for Wi-Fi", 350, s_view.lines[0]);
                    s_view.line_count = 1;
                }
                if (error == ESP_OK) {
                    error = attach_content(scene);
                }
            }
        } else if (s_route != Route::Online09 || online != s_online_link) {
            changed = true;
            error = enter_route(Route::Online09, scene);
            s_online_link = online;
            if (error == ESP_OK && !online) {
                constexpr char title[] = "No connection";
                error = set_scene_title(scene, {title, sizeof(title) - 1U});
                if (error == ESP_OK) {
                    error = prepare_caption(wifi.data_valid && !wifi.data_stale ? "Wi-Fi disconnected" : "UNAVAILABLE",
                        350, s_view.lines[0]);
                }
            }
            if (error == ESP_OK) {
                error = attach_content(scene);
            }
        }
    }
    return error;
}

// bounds: Full-canvas rectangle whose half-open hit area matches renderer coordinates.
// point: Logical touch point already mapped through the display rotation owner.
bool point_in_rect(CODY2D::Rect bounds, CODY2D::Point point)
{
    return point.x >= bounds.x && point.y >= bounds.y &&
        point.x < bounds.x + bounds.width && point.y < bounds.y + bounds.height;
}

// scene: Current screen 11 frame whose preview must follow a successful selection.
// point: Release point inside the list viewport after a tap-sized gesture.
// Exception: Returns the real persistence or content-layout error without changing selection.
esp_err_t select_look_at(SceneFrame &scene, CODY2D::Point point)
{
#if !CODY_ENABLE_PEARL_JELLY
    ESP_LOGE(kTag, "Screen 11 selection requires the jelly renderer (%s)",
             esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
#else
    for (uint8_t index = 0; index < s_view.look_count; ++index) {
        const auto &item = s_view.look_items[index];
        if (item.id == nullptr) {
            ESP_LOGE(kTag, "Screen 11 selection row %u has no stable ID (%s)",
                     index, esp_err_to_name(ESP_ERR_INVALID_STATE));
            return ESP_ERR_INVALID_STATE;
        }
        CODY2D::CapsuleControl target = item.control;
        target.bounds.y = static_cast<uint16_t>(target.bounds.y - s_view.look_scroll);
        if (!CODY2D::capsule_contains(target, point)) {
            continue;
        }
        if (std::strcmp(item.id, s_view.selected_look_id) == 0) {
            return ESP_OK;
        }
        const esp_err_t error = persist_jelly_skin(item.id);
        if (error != ESP_OK) {
            return error;
        }
        s_view.selected_look_id = item.id;
        return attach_content(scene);
    }
    return ESP_OK;
#endif
}

// No parameters: only routes with a real previous destination expose Back.
bool previous_navigation_available()
{
    return s_route == Route::Install06 || s_route == Route::Scan06Bis ||
        s_route == Route::Look11 ||
        (s_view.action_visible && s_view.action_id == OnboardingAction::Cancel);
}

// No parameters: forward actions retain their backend admission and enabled state.
bool next_navigation_available()
{
    return s_view.action_visible && s_view.action_id != OnboardingAction::Cancel;
}

// scene: Current route, never advanced on unconfirmed cancellation/cleanup.
// direction: Explicit previous/next intent, shared by tap and released swipe.
// Exception: Returns the real persistence, snapshot, command or route error.
esp_err_t activate_onboarding_navigation(SceneFrame &scene, NavigationDirection direction)
{
    const bool back_hit = direction == NavigationDirection::Previous && previous_navigation_available();
    const bool action_hit = direction == NavigationDirection::Next && next_navigation_available();
    if (!back_hit && !action_hit) {
        return ESP_OK;
    }
    const bool cancel_hit = back_hit && s_view.action_id == OnboardingAction::Cancel;
    if (s_command_pending || ((action_hit || cancel_hit) && !s_view.action.enabled)) {
        return ESP_OK;
    }
    ScenePresentation presentation;
    const esp_err_t presentation_error = prepare_scene_presentation(scene, presentation);
    if (presentation_error != ESP_OK || !presentation.content_visible) {
        return presentation_error;
    }
    if (back_hit && s_route == Route::Install06) {
        return enter_route(Route::Invitation05, scene);
    }
    if ((back_hit || action_hit) && s_route == Route::Look11) {
        return enter_route(Route::Online09, scene);
    }
    if (action_hit && s_route == Route::Invitation05) {
        esp_err_t error = enter_route(Route::Install06, scene);
        if (error == ESP_OK) {
            error = enqueue(Work::InstallQr, 0);
        }
        if (error != ESP_OK) {
            return show_error(scene, error);
        }
        return ESP_OK;
    }
    if (action_hit && s_route == Route::Online09) {
        return enter_route(Route::Look11, scene);
    }
    LUNASHELL::ConnectivityObservation observation;
    const esp_err_t observation_error = LUNASHELL::DeviceSnapshotService::instance().readConnectivityObservation(observation);
    if (observation_error != ESP_OK) {
        return observation_error;
    }
    const auto &pairing = observation.radios.pairing;
    if (s_route == Route::Failed08 && s_local_failure && s_failed_work == Work::InstallQr) {
        esp_err_t error = enter_route(Route::Install06, scene);
        if (error == ESP_OK) {
            error = enqueue(Work::InstallQr, 0);
        }
        return error == ESP_OK ? ESP_OK : show_error(scene, error);
    }
    if (!pairing.data_valid || pairing.data_stale) {
        return show_error(scene, pairing.last_error != ESP_OK ? pairing.last_error : ESP_ERR_INVALID_STATE);
    }
    if (s_route == Route::Failed08 && s_local_failure && s_failed_work == Work::SessionQr &&
        pairing.state == PairState::ReadyToScan && pairing.attempt_id == s_attempt) {
        const esp_err_t error = enqueue(Work::SessionQr, s_attempt, pairing.provisioning_session_id);
        return error == ESP_OK ? ESP_OK : show_error(scene, error);
    }
    Work work = Work::Cancel;
    if (s_route == Route::Failed08 && s_local_failure && s_failed_work == Work::Cancel) {
        work = Work::Cancel; // Retry a rejected cancellation, not a second Start.
    } else if (s_route == Route::Install06) {
        work = pairing.state == PairState::Failed || pairing.state == PairState::TimedOut ? Work::Retry : Work::Start;
    } else if (s_route == Route::Failed08 && pairing.cleanup_error == ESP_OK) {
        work = pairing.state == PairState::Failed || pairing.state == PairState::TimedOut ? Work::Retry : Work::Start;
    }
    if (s_pairing_owned && pairing.attempt_id != s_attempt) {
        return show_error(scene, ESP_ERR_INVALID_STATE);
    }
    s_cancel_to_install = work == Work::Cancel;
    const esp_err_t request_error = enqueue(work, pairing.attempt_id);
    if (request_error != ESP_OK) {
        return show_error(scene, request_error);
    }
    if (work != Work::Cancel) {
        s_session = 0;
        return enter_route(Route::Pairing07, scene);
    }
    if (s_route == Route::Scan06Bis) {
        // Retire the session's secret immediately; navigation still awaits Cancelled.
        return enter_route(Route::Pairing07, scene);
    }
    const esp_err_t action_error = prepare_action(OnboardingAction::Cancel, false);
    return action_error == ESP_OK ? attach_content(scene) : action_error;
}

// scene: Current route; swipes use the exact same gated actions as the chevrons.
// point: Real logical coordinate from the existing touch acquisition owner.
// pressed: Actual contact state, not an inferred sensor value.
// pressed_edge: True only for the first pressed sample of the contact.
// rotation_degrees: Shared evaluated orientation, never a second sensor read.
// consumed: True for controls/list gestures and a recognized navigation release.
// Exception: Returns the real route, persistence, snapshot or action error.
esp_err_t handle_onboarding_touch(SceneFrame &scene, CODY2D::Point point,
                                  bool pressed, bool pressed_edge, uint16_t rotation_degrees, bool &consumed)
{
    consumed = false;
    if (s_route == Route::None || s_route == Route::Hello04) {
        return ESP_OK;
    }
    if (s_swipe_route != s_route || (s_swipe_active && s_swipe_rotation != rotation_degrees)) {
        s_swipe_active = false;
        s_look_touch_active = false;
        s_look_touch_dragged = false;
    }
    const CODY2D::CapsuleControl back{kBackBounds, {}, NavigationData::top, NavigationData::bottom, true};
    const CODY2D::CapsuleControl next{kForwardBounds, {}, NavigationData::top, NavigationData::bottom, true};
    if (pressed_edge) {
        const bool back_hit = previous_navigation_available() && CODY2D::capsule_contains(back, point);
        const bool next_hit = next_navigation_available() && CODY2D::capsule_contains(next, point);
        if (back_hit || next_hit) {
            consumed = true;
            return activate_onboarding_navigation(scene,
                back_hit ? NavigationDirection::Previous : NavigationDirection::Next);
        }
        // A candidate does not steal Cody's drag. Only an unmistakable horizontal
        // release navigates; vertical drags retain the look-list's scroll behavior.
        s_swipe_active = point_in_rect({73, 73, 320, 320}, point);
        s_swipe_start = point;
        s_swipe_last = point;
        s_swipe_route = s_route;
        s_swipe_rotation = rotation_degrees;
        s_swipe_axis = GestureAxis::Undecided;
        if (s_route == Route::Look11 && point_in_rect(kLookListViewport, point)) {
            s_look_touch_active = true;
            s_look_touch_dragged = false;
            s_look_touch_start_y = point.y;
            s_look_scroll_start = s_view.look_scroll;
        }
    }
    if (pressed && s_swipe_active) {
        s_swipe_last = point;
        const int dx = std::abs(point.x - s_swipe_start.x);
        const int dy = std::abs(point.y - s_swipe_start.y);
        if (s_swipe_axis == GestureAxis::Undecided && std::max(dx, dy) >= 12) {
            if (dx * 2 > dy * 3) {
                s_swipe_axis = GestureAxis::Horizontal;
            } else if (dy * 2 > dx * 3) {
                s_swipe_axis = GestureAxis::Vertical;
            }
        }
    }
    if (!pressed && s_swipe_active) {
        s_swipe_active = false;
        const int dx = s_swipe_last.x - s_swipe_start.x;
        const int dy = s_swipe_last.y - s_swipe_start.y;
        if (s_swipe_axis == GestureAxis::Horizontal && std::abs(dx) >= 48 &&
            std::abs(dx) * 2 > std::abs(dy) * 3) {
            s_look_touch_active = false;
            s_look_touch_dragged = false;
            consumed = true;
            return activate_onboarding_navigation(scene,
                dx < 0 ? NavigationDirection::Next : NavigationDirection::Previous);
        }
    }
    if (s_route == Route::Look11 && s_look_touch_active) {
        consumed = true;
        if (pressed) {
            const int displacement = s_look_touch_start_y - point.y;
            if (std::abs(displacement) >= kLookDragThreshold || s_swipe_axis == GestureAxis::Horizontal) {
                s_look_touch_dragged = true;
            }
            if (s_swipe_axis == GestureAxis::Horizontal) {
                return ESP_OK;
            }
            const int scroll = std::clamp<int>(s_look_scroll_start + displacement, 0, look_scroll_limit());
            if (scroll != s_view.look_scroll) {
                s_view.look_scroll = static_cast<uint16_t>(scroll);
                return attach_content(scene);
            }
            return ESP_OK;
        }
        s_look_touch_active = false;
        const bool tap = !s_look_touch_dragged && point_in_rect(kLookListViewport, s_swipe_last);
        s_look_touch_dragged = false;
        return tap ? select_look_at(scene, s_swipe_last) : ESP_OK;
    }
    return ESP_OK;
}

// direction: Icon orientation in the same logical coordinate basis as touch.
// enabled: Disabled actions keep their full target but use dimmer ink.
// strip: Caller-owned destination tile, including the lateral region.
// first_row: Logical tile y origin.
// first_column: Logical tile x origin.
// Exception: Returns the first real capsule or stroke drawing error.
esp_err_t draw_navigation_chevron(NavigationDirection direction, bool enabled,
                                  CODY2D::Surface strip, uint16_t first_row, uint16_t first_column)
{
    const auto bounds = direction == NavigationDirection::Previous ? kBackBounds : kForwardBounds;
    const CODY2D::CapsuleControl control{bounds, {}, NavigationData::top, NavigationData::bottom, enabled};
    esp_err_t error = CODY2D::draw_capsule_rows(control, strip, first_row, first_column);
    const float center_x = bounds.x + bounds.width / 2.f;
    const float center_y = bounds.y + bounds.height / 2.f;
    const float sign = direction == NavigationDirection::Previous ? -1.f : 1.f;
    const CODY2D::Vec2 points[] = {{center_x - sign * 3.f, center_y - 8.f},
        {center_x + sign * 5.f, center_y}, {center_x - sign * 3.f, center_y + 8.f}};
    const uint16_t ink = CODY2D::fade_rgb565_to_black(NavigationData::ink, enabled ? 255 : 128);
    for (uint16_t index = 0; error == ESP_OK && index < 2; ++index) {
        error = CODY2D::stroke_rows(strip, first_row, points[index], points[index + 1U], 2.f, ink, first_column);
    }
    return error;
}

// strip: Valid caller-owned logical tile outside the content slot as well.
// first_row: Logical tile y origin, shared with all cloud/text rendering.
// first_column: Logical tile x origin, shared with hit testing.
// Exception: Returns the real surface or primitive drawing error.
esp_err_t draw_onboarding_navigation(CODY2D::Surface strip, uint16_t first_row,
                                    uint16_t first_column)
{
    esp_err_t error = ESP_OK;
    if (previous_navigation_available()) {
        const bool enabled = !s_command_pending &&
            (s_view.action_id != OnboardingAction::Cancel || s_view.action.enabled);
        error = draw_navigation_chevron(NavigationDirection::Previous, enabled, strip, first_row, first_column);
    }
    if (error == ESP_OK && next_navigation_available()) {
        error = draw_navigation_chevron(NavigationDirection::Next, !s_command_pending && s_view.action.enabled,
            strip, first_row, first_column);
    }
    return error;
}

} // namespace CODY

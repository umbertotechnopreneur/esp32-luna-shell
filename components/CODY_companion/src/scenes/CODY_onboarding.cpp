#include "CODY_onboarding.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>

#include "CODY2D_controls.hpp"
#include "CODY_jelly.hpp"
#include "CODY_look_screen.hpp"
#include "CODY_settings.hpp"
#include "LUNASHELL_build_mode.hpp"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_platform.h"
#include "scene_data.hpp"
#include "navigation_data.hpp"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "qrcode.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace CODY {
namespace {
constexpr char kTag[] = "CODY_onboarding";
constexpr char kInstallUrl[] = "https://play.google.com/store/apps/details?id=com.espressif.provble";
constexpr uint16_t kQrModulesMax = 57; // Version 10; bounded independently of the framebuffer.
constexpr uint16_t kQrBytes = (kQrModulesMax * kQrModulesMax + 7U) / 8U;
constexpr uint16_t kQrQuietModules = 4;
constexpr CODY2D::Rect kInstallQrBounds{113, 140, 240, 240};
constexpr CODY2D::Rect kSessionQrBounds{131, 182, 204, 204};
constexpr CODY2D::Rect kFooterBounds{113, 390, 240, 40};
constexpr uint16_t kActionBlueTop = 0x765E;
constexpr uint16_t kActionBlueBottom = 0x34FC;
constexpr uint16_t kCaptionWidth = 232;
#if CONFIG_LUNASHELL_BUILD_MODE_DEBUG
constexpr uint16_t kDebugTextWidth = 386;
constexpr uint16_t kDebugTextColor = 0xF7BE; // CSS whitesmoke (#F5F5F5) in native RGB565.
constexpr uint8_t kPairingDebugLineCount = 6;
#endif
constexpr uint8_t kContentLineCapacity = 8;
constexpr size_t kDebugLineBytes = 64;
constexpr CODY2D::Rect kForwardBounds{396, 203, 60, 60};
constexpr CODY2D::Rect kBackBounds{10, 203, 60, 60};
constexpr int64_t kHelloReadableHoldUs = 1000000;
constexpr int64_t kCodexIdleHoldUs = 10LL * 1000000LL;
constexpr bool kPairingDebugEnabled = LUNASHELL::build_mode::kDevelopmentDiagnosticsEnabled;
constexpr bool kCodexIntegrationEnabled = CONFIG_LUNA_SHELL_CODY_ENABLE_CODEX;
using PairState = LUNASHELL::PairingSessionState;
using CodexState = LUNASHELL::CodexLinkState;

enum class Route : uint8_t {
    None, Hello04, Invitation05, Install06, Pairing07, Failed08, Online09,
    MakeItYours10, Look11, ConnectCodex12, SetupComplete13, LookingForPc14,
    Connected15, Idle16, Settings36, DebugText
};
enum class Work : uint8_t {
    InstallQr,
    SessionQr,
    Start,
    Retry,
    Cancel,
    SaveLook,
    CompleteFirstBoot,
    CodexStart,
    CodexCopyCode,
    CodexDiscover,
};
enum class NavigationDirection : uint8_t { Previous, Next };
enum class OnboardingAction : uint8_t {
    None, ShowQr, PairCody, Cancel, Retry, Continue, Next, Done, LetsGo
};
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
    LUNASHELL::CharacterLookSaveSnapshot look_save{};
};
struct Result {
    Command command{};
    esp_err_t error = ESP_OK;
    esp_err_t publication_error = ESP_OK;
    QrModules qr{};
    char codex_code[7]{};
};
struct ContentView {
    CODY2D::TextLine lines[kContentLineCapacity]{};
    uint8_t line_count = 0;
    uint16_t visible_characters[kContentLineCapacity]{};
    CODY2D::CapsuleControl action{};
    bool action_visible = false;
    uint16_t action_visible_characters = 0;
    OnboardingAction action_id = OnboardingAction::None;
    QrModules qr{};
    CODY2D::Rect qr_bounds{};
    char error_text[32]{};
    char debug_text[kContentLineCapacity][kDebugLineBytes]{};
};
ContentView s_view;
Route s_route = Route::None;
uint32_t s_generation = 1;
uint32_t s_look_request_id = 0; // Never reset with a route; late writes keep their identity.
uint32_t s_revision = 0;
uint32_t s_navigation_revision = 0;
uint32_t s_text_generation = 0;
uint32_t s_attempt = 0;
uint32_t s_session = 0;
bool s_command_pending = false;
bool s_cancel_to_install = false;
bool s_pairing_owned = false;
bool s_online_link = false;
bool s_online_presentation_current = false;
bool s_first_boot_persisted = false;
bool s_pairing_debug_dismissed = false;
int64_t s_hello_completed_at_us = -1;
int64_t s_last_pairing_change_us = -1;
uint32_t s_codex_attempt = 0;
int64_t s_last_codex_change_us = -1;
int64_t s_codex_idle_started_at_us = -1;
bool s_codex_owned = false;
bool s_codex_error_visible = false;
esp_err_t s_codex_visible_error = ESP_OK;
char s_codex_code[7]{};
esp_err_t s_visible_error = ESP_OK;
Work s_failed_work = Work::Start;
bool s_local_failure = false;
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
// result: Original operation result, separate snapshot publication error and optional QR.
// Exception: The result retains every persistence, snapshot and radio failure.
void execute_command(const Command &command, Result &result)
{
    result.command = command;
    if (command.work == Work::CompleteFirstBoot) {
        LUNASHELL::ConnectivityObservation observation;
        result.error = LUNASHELL::DeviceSnapshotService::instance().readConnectivityObservation(observation);
        if (result.error != ESP_OK) {
            return;
        }
        const auto &pairing = observation.radios.pairing;
        const auto &wifi = observation.radios.wifi;
        if (!pairing.data_valid || pairing.data_stale || pairing.state != PairState::Online ||
            pairing.attempt_id != command.attempt || !wifi.data_valid || wifi.data_stale ||
            wifi.health != LUNASHELL::DeviceHealth::Ready || wifi.last_error != ESP_OK ||
            !wifi.connected || !wifi.ip_acquired || wifi.ip_address[0] == '\0') {
            result.error = ESP_ERR_INVALID_STATE;
            ESP_LOGE(kTag,
                "First-boot completion rejected: pairing-valid=%u stale=%u state=%u attempt=%lu/%lu "
                "wifi-valid=%u stale=%u health=%u error=%s connected=%u ip=%u (%s)",
                pairing.data_valid, pairing.data_stale, to_index(pairing.state),
                static_cast<unsigned long>(pairing.attempt_id),
                static_cast<unsigned long>(command.attempt), wifi.data_valid, wifi.data_stale,
                to_index(wifi.health), esp_err_to_name(wifi.last_error), wifi.connected,
                wifi.ip_acquired, esp_err_to_name(result.error));
            return;
        }
        result.error = persist_first_boot(false);
        return;
    }
    if (command.work == Work::SaveLook) {
#if CODY_ENABLE_PEARL_JELLY
        result.error = persist_jelly_skin(command.look_save.requested_id);
#else
        result.error = ESP_ERR_NOT_SUPPORTED;
#endif
        auto completed = command.look_save;
        completed.state = result.error == ESP_OK
            ? LUNASHELL::CharacterLookSaveSnapshot::State::Saved
            : LUNASHELL::CharacterLookSaveSnapshot::State::Failed;
        completed.error = result.error;
        completed.completed_at_us = esp_timer_get_time();
        result.publication_error =
            LUNASHELL::DeviceSnapshotService::instance().reportCharacterLookSave(completed);
        return;
    }
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
    if (command.work == Work::CodexCopyCode) {
        result.error = LUNASHELL_codex_link_copy_pairing_code(
            result.codex_code,
            sizeof(result.codex_code),
            command.attempt
        );
        return;
    }
    if (command.work == Work::CodexStart || command.work == Work::CodexDiscover) {
        const LUNASHELL_codex_link_action_t action = command.work == Work::CodexStart
            ? LUNASHELL_CODEX_LINK_ACTION_START_PAIRING
            : LUNASHELL_CODEX_LINK_ACTION_START_DISCOVERY;
        result.error = LUNASHELL_codex_link_request(action, command.attempt);
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

// context: Unused; one worker owns blocking radio, QR and preference operations.
// Exception: Operation and publication errors remain separate in the result mailbox.
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
        if (completed.error != ESP_OK || completed.publication_error != ESP_OK) {
            ESP_LOGE(kTag, "Action %u attempt=%lu session=%lu failed: operation=%s publication=%s",
                to_index(completed.command.work), static_cast<unsigned long>(completed.command.attempt),
                static_cast<unsigned long>(completed.command.session), esp_err_to_name(completed.error),
                esp_err_to_name(completed.publication_error));
        }
        s_result = completed;
        wipe(&completed, sizeof(completed));
        s_result_ready.store(true, std::memory_order_release);
    }
}

// work: Explicit radio, Codex-link or QR preparation action.
// attempt: Current canonical attempt identity; never an inferred UI list position.
// session: Current canonical provisioning-session identity, or zero for other actions.
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

// scene: Active selector whose previous selection stays visible during the write.
// id: Flash-owned catalog ID from a real tap; copied into bounded request storage.
// Exception: Returns the real admission, preparation or snapshot publication error.
esp_err_t enqueue_look_save(SceneFrame &scene, const char *id)
{
    if (s_worker == nullptr || s_work_busy.load(std::memory_order_acquire) ||
        s_look_request_id == UINT32_MAX || id == nullptr) {
        ESP_LOGE(kTag, "Look worker admission rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    Command command;
    command.work = Work::SaveLook;
    command.generation = s_generation;
    auto &operation = command.look_save;
    const size_t length = std::strlen(id);
    if (length == 0 || length >= sizeof(operation.requested_id)) {
        ESP_LOGE(kTag, "Look ID length rejected: %u (%s)",
                 static_cast<unsigned>(length), esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    std::memcpy(operation.requested_id, id, length + 1U);
    operation.request_id = s_look_request_id + 1U;
    operation.state = LUNASHELL::CharacterLookSaveSnapshot::State::Saving;
    operation.started_at_us = esp_timer_get_time();
    esp_err_t error = begin_look_save(scene, id, operation.request_id);
    if (error != ESP_OK) {
        return error;
    }
    error = LUNASHELL::DeviceSnapshotService::instance().reportCharacterLookSave(operation);
    if (error != ESP_OK) {
        return error;
    }
    s_look_request_id = operation.request_id;
    s_command = command;
    s_command_pending = true;
    scene.content.control_revision = ++s_navigation_revision;
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

// destination: Fixed route-owned buffer retaining the formatted text through frame retirement.
// capacity: Exact writable capacity including the null terminator.
// format: Printable-ASCII format containing exactly one string conversion.
// value: Canonical snapshot value copied into the debug record.
// Exception: Returns ESP_ERR_INVALID_SIZE rather than truncating a diagnostic value.
#if CONFIG_LUNASHELL_BUILD_MODE_DEBUG
esp_err_t format_debug_line(char *destination, size_t capacity, const char *format, const char *value)
{
    if (destination == nullptr || capacity == 0 || format == nullptr || value == nullptr) {
        ESP_LOGE(kTag, "Debug line format rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    const int length = std::snprintf(destination, capacity, format, value);
    if (length <= 0 || static_cast<size_t>(length) >= capacity) {
        ESP_LOGE(kTag, "Debug line would be truncated: length=%d capacity=%u (%s)",
                 length, static_cast<unsigned>(capacity), esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

// text: Bounded printable-ASCII line retained in ContentView.
// top: Logical top edge selected against the circle chord at this row.
// line: Receives the centered whitesmoke text geometry.
// Exception: Returns the real font or circular-layout preparation error.
esp_err_t prepare_debug_caption(const char *text, uint16_t top, CODY2D::TextLine &line)
{
    const auto &font = SceneData::body_font;
    const size_t length = text == nullptr ? 0 : std::strlen(text);
    const uint32_t width = length == 0 ? 0 : (length - 1U) * font.advance + font.width;
    if (length == 0 || length >= kDebugLineBytes || width > kDebugTextWidth ||
        top + font.height > 356) {
        ESP_LOGE(kTag, "Debug caption rejected: length=%u width=%lu top=%u (%s)",
                 static_cast<unsigned>(length), static_cast<unsigned long>(width), top,
                 esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    return CODY2D::prepare_text_line(font, {text, static_cast<uint16_t>(length)},
        {static_cast<int16_t>((466U - width) / 2U), static_cast<int16_t>(top)},
        kDebugTextColor, line);
}
#endif

// No parameters: primary setup actions use a captioned footer, not lateral arrows.
bool uses_footer_action()
{
    return s_route == Route::Invitation05 || s_route == Route::Install06 ||
        s_route == Route::Pairing07 || s_route == Route::Failed08 || s_route == Route::Online09 ||
        s_route == Route::MakeItYours10 || s_route == Route::Settings36 ||
        s_route == Route::ConnectCodex12 || s_route == Route::SetupComplete13;
}

// action: Stable action identity; the returned English label has flash lifetime.
// label: Receives the primary capsule caption, or null for no action.
// Exception: Unknown identities are rejected rather than inferred from route order.
esp_err_t action_label(OnboardingAction action, const char *&label)
{
    switch (action) {
    case OnboardingAction::None:
        label = nullptr;
        break;
    case OnboardingAction::ShowQr:
        label = "Show QR";
        break;
    case OnboardingAction::PairCody:
        label = "Pair Cody";
        break;
    case OnboardingAction::Cancel:
        label = "Cancel";
        break;
    case OnboardingAction::Retry:
        label = "Retry";
        break;
    case OnboardingAction::Continue:
        label = "Continue";
        break;
    case OnboardingAction::Next:
        label = "Next";
        break;
    case OnboardingAction::Done:
        label = "Done";
        break;
    case OnboardingAction::LetsGo:
        label = "Let's go";
        break;
    default:
        ESP_LOGE(kTag, "Unknown navigation action %u (%s)", to_index(action), esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

// action: Explicit route action; never inferred from an English label.
// enabled: True only when the route has an admissible backend or navigation action.
// Exception: Rejects an unknown action identity without changing the view.
esp_err_t prepare_action(OnboardingAction action, bool enabled)
{
    const char *label = nullptr;
    esp_err_t error = action_label(action, label);
    if (error != ESP_OK) {
        return error;
    }
    if (s_view.action_id != action || s_view.action.enabled != enabled) {
        ++s_navigation_revision;
    }
    if (s_view.action_id != action) {
        ++s_text_generation;
    }
    s_view.action_id = action;
    s_view.action_visible = action != OnboardingAction::None;
    const auto bounds = uses_footer_action() ? kFooterBounds :
        action == OnboardingAction::Cancel ? kBackBounds : kForwardBounds;
    s_view.action = {bounds, {}, uses_footer_action() ? kActionBlueTop : NavigationData::top,
        uses_footer_action() ? kActionBlueBottom : NavigationData::bottom, enabled};
    if (label != nullptr && uses_footer_action()) {
        const auto &font = SceneData::body_font;
        const uint16_t length = static_cast<uint16_t>(std::strlen(label));
        const uint16_t width = (length - 1U) * font.advance + font.width;
        if (width > bounds.width || font.height > bounds.height) {
            ESP_LOGE(kTag, "Action caption does not fit (%s)", esp_err_to_name(ESP_ERR_INVALID_SIZE));
            return ESP_ERR_INVALID_SIZE;
        }
        error = CODY2D::prepare_text_line(font, {label, length},
            {static_cast<int16_t>(bounds.x + (bounds.width - width) / 2U),
             static_cast<int16_t>(bounds.y + (bounds.height - font.height) / 2U)},
            0x0001, s_view.action.caption);
    }
    return error;
}

// qr: Compact immutable module map from the encoder.
// bounds: Route-owned square viewport; quiet zones and integer modules stay intact.
// strip: Already clipped caller-owned content tile.
// first_row: Logical y origin of the tile.
// first_column: Logical x origin of the tile.
// Exception: Rejects unsupported QR geometry before grid arithmetic or pixel writes.
esp_err_t draw_qr_rows(const QrModules &qr, CODY2D::Rect bounds, CODY2D::Surface strip,
                       uint16_t first_row, uint16_t first_column)
{
    if (qr.side == 0) {
        return ESP_OK;
    }
    if (qr.side < 21 || qr.side > kQrModulesMax || (qr.side - 21U) % 4U != 0 ||
        bounds.width != bounds.height || bounds.x + bounds.width > 466 || bounds.y + bounds.height > 466) {
        ESP_LOGE(kTag, "QR module extent rejected: side=%u (%s)", qr.side, esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    const uint16_t grid_side = qr.side + 2U * kQrQuietModules;
    const uint16_t scale = bounds.width / grid_side;
    if (scale == 0) {
        ESP_LOGE(kTag, "QR viewport cannot fit side=%u (%s)", qr.side, esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    const uint16_t inset = (bounds.width - grid_side * scale) / 2U;
    const uint16_t top = std::max(first_row, bounds.y);
    const uint16_t bottom = std::min<uint16_t>(first_row + strip.height, bounds.y + bounds.height);
    const uint16_t left = std::max(first_column, bounds.x);
    const uint16_t right = std::min<uint16_t>(first_column + strip.width, bounds.x + bounds.width);
    for (uint16_t row = top; row < bottom; ++row) {
        for (uint16_t column = left; column < right; ++column) {
            const int module_x = (static_cast<int>(column) - bounds.x - inset) / scale - kQrQuietModules;
            const int module_y = (static_cast<int>(row) - bounds.y - inset) / scale - kQrQuietModules;
            const bool inside = column >= bounds.x + inset && row >= bounds.y + inset &&
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
// Exception: Returns the first real QR, capsule or text drawing error.
esp_err_t draw_content(const void *state, CODY2D::Surface strip,
                       uint16_t first_row, uint16_t first_column)
{
    const auto &view = *static_cast<const ContentView *>(state);
    esp_err_t error = draw_qr_rows(view.qr, view.qr_bounds, strip, first_row, first_column);
    for (uint8_t index = 0; error == ESP_OK && index < view.line_count; ++index) {
        error = CODY2D::draw_text_rows(view.lines[index], view.visible_characters[index],
            strip, first_row, first_column);
    }
    if (error == ESP_OK && view.action_visible && view.action.caption.valid) {
        // Keep the full caption's centered geometry while drawing its typed prefix.
        CODY2D::CapsuleControl capsule = view.action;
        capsule.caption = {};
        error = CODY2D::draw_capsule_rows(capsule, strip, first_row, first_column);
        if (error == ESP_OK) {
            error = CODY2D::draw_text_rows(view.action.caption, view.action_visible_characters,
                strip, first_row, first_column);
        }
    }
    return error;
}

// scene: Valid route receiving the current content revision.
// Exception: Returns the scene-content validation error without replacing the frame.
esp_err_t attach_content(SceneFrame &scene)
{
    if (s_route == Route::Look11) {
        return attach_look_content(scene, s_navigation_revision);
    }
    uint16_t preceding = scene.definition->title_visible ? scene.title.text.length : 0;
    for (uint8_t index = 0; index < s_view.line_count; ++index) {
        s_view.visible_characters[index] = scene_content_characters(scene, s_view.lines[index].text.length, preceding);
        preceding += s_view.lines[index].text.length;
    }
    uint16_t content_characters = 0;
    for (uint8_t index = 0; index < s_view.line_count; ++index) {
        content_characters += s_view.visible_characters[index];
    }
    s_view.action_visible_characters = s_view.action_visible && s_view.action.caption.valid ?
        scene_content_characters(scene, s_view.action.caption.text.length, preceding) : 0;
    content_characters += s_view.action_visible_characters;
    const uint16_t top = s_route == Route::DebugText ? 130 :
        (s_route == Route::MakeItYours10 || s_route == Route::Settings36) ? 204 :
        s_route == Route::Install06 ? kInstallQrBounds.y :
        s_route == Route::Pairing07 ? kSessionQrBounds.y :
        s_route == Route::Invitation05 ? 230 : 346;
    SceneContent content;
    // The debug page uses the wider middle chord; normal setup routes keep the
    // QR/footer union used by their existing controls.
    content.bounds = s_route == Route::DebugText
        ? DamageRect{40, top, 426, 356}
        : DamageRect{kFooterBounds.x, top,
            static_cast<uint16_t>(kFooterBounds.x + kFooterBounds.width),
            static_cast<uint16_t>(kFooterBounds.y + kFooterBounds.height)};
    content.state = &s_view;
    content.revision = ++s_revision;
    content.control_revision = s_navigation_revision;
    content.visible_characters = content_characters;
    content.text_generation = s_text_generation;
    if (s_route == Route::DebugText) {
        // Full-screen dismissal belongs to the route owner, not an invisible control.
    } else if (uses_footer_action()) {
        content.control_bounds[1] = {kFooterBounds.x, kFooterBounds.y,
            static_cast<uint16_t>(kFooterBounds.x + kFooterBounds.width),
            static_cast<uint16_t>(kFooterBounds.y + kFooterBounds.height)};
    } else {
        content.control_bounds[0] = {10, 203, 70, 263};
        content.control_bounds[1] = {396, 203, 456, 263};
    }
    content.draw_rows = draw_content;
    return set_scene_content(scene, content);
}

// observation: One coherent canonical Wi-Fi/BLE/pairing snapshot after successful handoff.
// scene: Receives the reusable micro-cloud debug layout and six immutable text rows.
// Exception: Rejects unavailable identities, truncation or layout failures without inventing values.
#if CONFIG_LUNASHELL_BUILD_MODE_DEBUG
esp_err_t enter_pairing_debug(const LUNASHELL::ConnectivityObservation &observation, SceneFrame &scene)
{
    const auto &wifi = observation.radios.wifi;
    const auto &ble = observation.radios.ble;
    if (wifi.ssid[0] == '\0' || wifi.ip_address[0] == '\0' || ble.device_name[0] == '\0') {
        ESP_LOGE(kTag, "Pairing debug rejected incomplete identities (%s)",
                 esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    const SceneDefinition *definition = nullptr;
    esp_err_t error = find_scene(SceneId::DebugText, definition);
    SceneFrame prepared;
    if (error == ESP_OK) {
        error = begin_scene(*definition, prepared);
    }
    if (error != ESP_OK) {
        return error;
    }
    wipe(&s_view, sizeof(s_view));
    reset_look_screen();
    s_swipe_active = false;
    ++s_navigation_revision;
    ++s_text_generation;
    s_route = Route::DebugText;
    s_view.line_count = kPairingDebugLineCount;
    error = format_debug_line(s_view.debug_text[0], kDebugLineBytes,
        "1. Wi-Fi connected to `%s`", wifi.ssid);
    if (error == ESP_OK) {
        error = format_debug_line(s_view.debug_text[1], kDebugLineBytes,
            "2. IP obtained: `%s`", wifi.ip_address);
    }
    if (error == ESP_OK) {
        error = format_debug_line(s_view.debug_text[2], kDebugLineBytes,
            "3. `%s`", "STA Got IP");
    }
    if (error == ESP_OK) {
        error = format_debug_line(s_view.debug_text[3], kDebugLineBytes,
            "4. `%s`", "Provisioning stopped");
    }
    if (error == ESP_OK) {
        error = format_debug_line(s_view.debug_text[4], kDebugLineBytes,
            "5. Entered `%s` screen", "online");
    }
    if (error == ESP_OK) {
        error = format_debug_line(s_view.debug_text[5], kDebugLineBytes,
            "6. BLE restarted as `%s`", ble.device_name);
    }
    for (uint8_t index = 0; error == ESP_OK && index < s_view.line_count; ++index) {
        error = prepare_debug_caption(s_view.debug_text[index],
            static_cast<uint16_t>(132U + index * 38U), s_view.lines[index]);
    }
    if (error == ESP_OK) {
        error = attach_content(prepared);
    }
    if (error != ESP_OK) {
        return error;
    }
    scene = prepared;
    ESP_LOGI(kTag, "Reusable debug-text route entered after pairing handoff");
    return ESP_OK;
}
#endif

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
    case Route::Pairing07:
        identity = SceneId::Pairing;
        break;
    case Route::Failed08:
        identity = SceneId::PairingFailed;
        break;
    case Route::Online09:
        identity = SceneId::Online;
        break;
    case Route::MakeItYours10:
        identity = SceneId::MakeItYours;
        break;
    case Route::Settings36:
        identity = SceneId::Settings;
        break;
    case Route::ConnectCodex12:
        identity = SceneId::ConnectCodex;
        break;
    case Route::SetupComplete13:
        identity = SceneId::SetupComplete;
        break;
    case Route::LookingForPc14:
        identity = SceneId::LookingForPc;
        break;
    case Route::Connected15:
        identity = SceneId::Connected;
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
    reset_look_screen();
    s_swipe_active = false;
    ++s_navigation_revision;
    ++s_text_generation;
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
        s_view.qr_bounds = kInstallQrBounds;
        error = prepare_action(OnboardingAction::PairCody, !s_command_pending);
    } else if (route == Route::Pairing07) {
        s_view.qr_bounds = kSessionQrBounds;
        error = prepare_caption("Starting Bluetooth", 222, s_view.lines[0]);
        s_view.lines[0].color = prepared.definition->title_color;
        s_view.line_count = 1;
        if (error == ESP_OK) {
            error = prepare_action(OnboardingAction::Cancel, s_pairing_owned && !s_command_pending);
        }
    } else if (route == Route::Failed08) {
        std::snprintf(s_view.error_text, sizeof(s_view.error_text), "Error: 0x%lx",
            static_cast<unsigned long>(s_visible_error));
        error = prepare_caption(s_view.error_text, 348, s_view.lines[0]);
        s_view.line_count = 1;
        if (error == ESP_OK) {
            error = prepare_action(OnboardingAction::None, false); // A current observation enables Retry/Cancel separately.
        }
    } else if (route == Route::Online09) {
        // Returning from a child must re-evaluate the canonical connection,
        // even if it was already offline before the child was opened.
        s_online_presentation_current = false;
        error = prepare_caption("Wi-Fi connected", 350, s_view.lines[0]);
        s_view.line_count = 1;
#if CODY_ENABLE_PEARL_JELLY
        if (error == ESP_OK) {
            error = prepare_action(OnboardingAction::Continue, true);
        }
#endif
    } else if (route == Route::MakeItYours10) {
        constexpr const char *captions[] = {
            "Shake to wake", "Auto-rotate", "Sounds", "Orientation 0 / 180"
        };
        for (const char *text : captions) {
            error = prepare_caption(text, 204 + s_view.line_count * 38U,
                                    s_view.lines[s_view.line_count]);
            if (error != ESP_OK) {
                return error;
            }
            ++s_view.line_count;
        }
        error = prepare_action(OnboardingAction::Next, true);
    } else if (route == Route::Settings36) {
        constexpr const char *captions[] = {"Personalization", "Choose Cody's look"};
        for (const char *text : captions) {
            error = prepare_caption(text, 232 + s_view.line_count * 38U,
                                    s_view.lines[s_view.line_count]);
            if (error != ESP_OK) {
                return error;
            }
            ++s_view.line_count;
        }
        error = prepare_action(OnboardingAction::Continue, true);
    } else if (route == Route::ConnectCodex12) {
        const char *message = s_codex_code[0] == '\0'
            ? "Preparing secure code"
            : "Enter code in Codex";
        error = prepare_caption(message, 350, s_view.lines[0]);
        s_view.line_count = 1;
        if (error == ESP_OK && s_codex_code[0] != '\0') {
            error = prepare_caption(s_codex_code, 380, s_view.lines[1]);
            s_view.line_count = 2;
        }
        if (error == ESP_OK) {
            error = prepare_action(OnboardingAction::None, false);
        }
    } else if (route == Route::SetupComplete13) {
        error = prepare_caption("Setup confirmed", 350, s_view.lines[0]);
        s_view.line_count = 1;
        if (error == ESP_OK) {
            error = prepare_action(OnboardingAction::LetsGo, true);
        }
    } else if (route == Route::LookingForPc14) {
        error = prepare_caption("Looking for paired PC", 350, s_view.lines[0]);
        s_view.line_count = 1;
        if (error == ESP_OK) {
            error = prepare_caption("Authenticated search", 380, s_view.lines[1]);
            s_view.line_count = 2;
        }
        if (error == ESP_OK) {
            error = prepare_action(OnboardingAction::None, false);
        }
    } else if (route == Route::Connected15) {
        error = prepare_caption("Codex PC found", 350, s_view.lines[0]);
        s_view.line_count = 1;
        if (error == ESP_OK) {
            error = prepare_caption("CCP is ready", 380, s_view.lines[1]);
            s_view.line_count = 2;
        }
        if (error == ESP_OK) {
            error = prepare_action(OnboardingAction::None, false);
        }
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

// scene: Current screen replaced by a truthful retry view on screen 12.
// error: Original CCP1 operation or canonical-state failure.
// Exception: Returns content preparation errors without replacing the real CCP1 error.
esp_err_t show_codex_error(SceneFrame &scene, esp_err_t error)
{
    s_codex_error_visible = true;
    s_codex_visible_error = error == ESP_OK ? ESP_ERR_INVALID_STATE : error;
    wipe(s_codex_code, sizeof(s_codex_code));
    esp_err_t presentation_error = enter_route(Route::ConnectCodex12, scene);
    if (presentation_error != ESP_OK) {
        return presentation_error;
    }
    wipe(&s_view, sizeof(s_view));
    std::snprintf(
        s_view.error_text,
        sizeof(s_view.error_text),
        "Error: 0x%lx",
        static_cast<unsigned long>(s_codex_visible_error)
    );
    presentation_error = prepare_caption(s_view.error_text, 350, s_view.lines[0]);
    s_view.line_count = 1;
    if (presentation_error == ESP_OK) {
        presentation_error = prepare_action(OnboardingAction::Retry, true);
    }
    return presentation_error == ESP_OK ? attach_content(scene) : presentation_error;
}

// scene: Current atlas frame retired while the native idle animation remains visible.
// Exception: This route owns no scene content and performs no backend action itself.
esp_err_t enter_codex_idle(SceneFrame &scene)
{
    wipe(&s_view, sizeof(s_view));
    reset_look_screen();
    scene = {};
    s_route = Route::Idle16;
    s_codex_idle_started_at_us = -1;
    s_swipe_active = false;
    ++s_navigation_revision;
    ++s_text_generation;
    ESP_LOGI(kTag, "Atlas route entered: 16 Idle; authenticated discovery waits 10 active seconds");
    return ESP_OK;
}

// scene: Host frame that starts a fresh display-code pairing attempt.
// Exception: Returns canonical snapshot, mailbox or presentation errors unchanged.
esp_err_t begin_codex_pairing(SceneFrame &scene)
{
    LUNASHELL::ConnectivityObservation observation{};
    esp_err_t error = LUNASHELL::DeviceSnapshotService::instance()
        .readConnectivityObservation(observation);
    if (error != ESP_OK) {
        return show_codex_error(scene, error);
    }
    const auto &codex_link = observation.radios.codex_link;
    if (!codex_link.data_valid || codex_link.data_stale) {
        return show_codex_error(
            scene,
            codex_link.last_error != ESP_OK ? codex_link.last_error : ESP_ERR_INVALID_STATE
        );
    }
    s_codex_error_visible = false;
    wipe(s_codex_code, sizeof(s_codex_code));
    error = enter_route(Route::ConnectCodex12, scene);
    if (error == ESP_OK) {
        error = enqueue(Work::CodexStart, codex_link.attempt_id);
    }
    return error == ESP_OK ? ESP_OK : show_codex_error(scene, error);
}

// scene: Active host frame, replaced only after the reusable selector is prepared.
// navigation: Caller and Done targets chosen by this host, not by the selector.
// Exception: Returns the original selector or navigation-control preparation error.
esp_err_t enter_look_route(SceneFrame &scene, const LookNavigationContext &navigation)
{
    esp_err_t error = open_look_screen(scene, navigation);
    if (error != ESP_OK) {
        return error;
    }
    wipe(&s_view, sizeof(s_view));
    s_route = Route::Look11;
    s_swipe_active = false;
    ++s_navigation_revision;
    error = prepare_action(OnboardingAction::None, false);
    if (error == ESP_OK) {
        error = attach_content(scene);
    }
    return error;
}

// destination: Explicit scene identity returned by a child screen.
// scene: Host-owned frame to replace through its normal route initialization.
// Exception: Rejects destinations this host does not implement; never guesses a route.
esp_err_t enter_host_destination(SceneId destination, SceneFrame &scene)
{
    Route route;
    switch (destination) {
    case SceneId::PairInvitation:
        route = Route::Invitation05;
        break;
    case SceneId::InstallApp:
        route = Route::Install06;
        break;
    case SceneId::Pairing:
        route = Route::Pairing07;
        break;
    case SceneId::PairingFailed:
        route = Route::Failed08;
        break;
    case SceneId::Online:
        route = Route::Online09;
        break;
    case SceneId::MakeItYours:
        route = Route::MakeItYours10;
        break;
    case SceneId::Settings:
        route = Route::Settings36;
        break;
    case SceneId::ConnectCodex:
        route = Route::ConnectCodex12;
        break;
    case SceneId::SetupComplete:
        route = Route::SetupComplete13;
        break;
    case SceneId::LookingForPc:
        route = Route::LookingForPc14;
        break;
    case SceneId::Connected:
        route = Route::Connected15;
        break;
    default:
        ESP_LOGE(kTag, "Unsupported host destination %u (%s)",
                 static_cast<unsigned>(destination), esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (route == Route::ConnectCodex12) {
        return begin_codex_pairing(scene);
    }
    return enter_route(route, scene);
}

// scene: Current route that displays a real local action/preparation error.
// error: Original operation error, not substituted by a successful presentation.
esp_err_t show_error(SceneFrame &scene, esp_err_t error)
{
    s_visible_error = error;
    return enter_route(Route::Failed08, scene);
}

// scene: Screen 07 remains active while its real backend state changes.
// message: Flash-owned status text, retained through complete frame retirement.
// Exception: Returns the original title, caption or content preparation error.
esp_err_t show_pairing_progress(SceneFrame &scene, const char *message)
{
    static constexpr char title[] = "Pairing...";
    ++s_text_generation;
    wipe(&s_view.qr, sizeof(s_view.qr));
    s_view.qr_bounds = kSessionQrBounds;
    esp_err_t error = set_scene_title(scene, {title, sizeof(title) - 1U});
    if (error == ESP_OK) {
        error = prepare_caption(message, 222, s_view.lines[0]);
        s_view.lines[0].color = scene.definition->title_color;
        s_view.line_count = 1;
    }
    return error == ESP_OK ? attach_content(scene) : error;
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
    s_codex_attempt = 0;
    s_last_codex_change_us = -1;
    s_codex_idle_started_at_us = -1;
    s_codex_owned = false;
    s_codex_error_visible = false;
    s_codex_visible_error = ESP_OK;
    wipe(s_codex_code, sizeof(s_codex_code));
    s_hello_completed_at_us = -1;
    s_local_failure = false;
    s_online_link = false;
    s_online_presentation_current = false;
    s_first_boot_persisted = false;
    // Release builds never admit the temporary diagnostic route.
    s_pairing_debug_dismissed = !kPairingDebugEnabled;
    reset_look_screen();
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
// Exception: Returns infrastructure/route errors; optional look-write failures remain
// explicit operation results in the canonical snapshot and the reusable error view.
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
    if (have_result && result.command.work == Work::SaveLook) {
        if (result.publication_error != ESP_OK) {
            return result.publication_error;
        }
        // Persistence is optional at this orchestrator boundary. The worker's real
        // write result is already canonical; failed publication is still fatal.
        if (result.error == ESP_OK) {
#if CODY_ENABLE_PEARL_JELLY
            const esp_err_t selection_error = select_jelly_skin(result.command.look_save.requested_id);
            if (selection_error != ESP_OK) {
                return selection_error;
            }
#else
            ESP_LOGE(kTag, "Look completion without a renderer (%s)", esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
            return ESP_ERR_NOT_SUPPORTED;
#endif
            changed = true;
        } else {
            ESP_LOGE(kTag, "Optional look write failed; display continues: request=%lu (%s)",
                     static_cast<unsigned long>(result.command.look_save.request_id), esp_err_to_name(result.error));
        }
        if (result.command.generation == s_generation && s_route == Route::Look11) {
            s_command_pending = false;
            scene.content.control_revision = ++s_navigation_revision;
            const auto completion = complete_look_save(scene, result.command.look_save.request_id, result.error);
            if (completion.presentation_error != ESP_OK) {
                return completion.presentation_error;
            }
            const esp_err_t content_error = attach_look_content(scene, s_navigation_revision);
            if (content_error != ESP_OK) {
                return content_error;
            }
            changed = true;
        }
        // A reset may retire the view while NVS is busy. The copied request and
        // canonical outcome survive it; never resurrect a retired screen.
        wipe(&result, sizeof(result));
        return ESP_OK;
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
    if (s_route == Route::Idle16) {
        wipe(&result, sizeof(result));
        if (s_codex_idle_started_at_us < 0) {
            s_codex_idle_started_at_us = active_time_us;
        }
        if (active_time_us - s_codex_idle_started_at_us < kCodexIdleHoldUs) {
            return ESP_OK;
        }
        LUNASHELL::ConnectivityObservation observation{};
        esp_err_t error = LUNASHELL::DeviceSnapshotService::instance()
            .readConnectivityObservation(observation);
        if (error != ESP_OK) {
            changed = true;
            return show_codex_error(scene, error);
        }
        const auto &codex_link = observation.radios.codex_link;
        if (!codex_link.data_valid || codex_link.data_stale ||
            codex_link.attempt_id != s_codex_attempt ||
            codex_link.state != CodexState::Paired) {
            changed = true;
            return show_codex_error(
                scene,
                codex_link.last_error != ESP_OK
                    ? codex_link.last_error
                    : ESP_ERR_INVALID_STATE
            );
        }
        error = enter_route(Route::LookingForPc14, scene);
        if (error == ESP_OK) {
            error = enqueue(Work::CodexDiscover, codex_link.attempt_id);
        }
        changed = true;
        return error == ESP_OK ? ESP_OK : show_codex_error(scene, error);
    }
    if (!scene.valid || scene.definition == nullptr) {
        ESP_LOGE(kTag, "Active route has no prepared scene (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    if (scene.content.control_revision != s_navigation_revision) {
        scene.content.control_revision = s_navigation_revision;
        changed = true;
    }
    if (s_route == Route::Look11) {
        wipe(&result, sizeof(result));
        return update_look_text(scene, s_navigation_revision, changed);
    }
    uint16_t preceding = scene.definition->title_visible ? scene.title.text.length : 0;
    bool caption_changed = false;
    for (uint8_t index = 0; index < s_view.line_count; ++index) {
        const uint16_t visible = scene_content_characters(scene, s_view.lines[index].text.length, preceding);
        caption_changed = caption_changed || visible != s_view.visible_characters[index];
        preceding += s_view.lines[index].text.length;
    }
    if (s_view.action_visible && s_view.action.caption.valid) {
        const uint16_t visible = scene_content_characters(scene, s_view.action.caption.text.length, preceding);
        caption_changed = caption_changed || visible != s_view.action_visible_characters;
    }
    if (caption_changed) {
        const esp_err_t caption_error = attach_content(scene);
        if (caption_error != ESP_OK) {
            return caption_error;
        }
        changed = true;
    }
    LUNASHELL::ConnectivityObservation observation;
    esp_err_t error = LUNASHELL::DeviceSnapshotService::instance().readConnectivityObservation(observation);
    if (error != ESP_OK) {
        wipe(&result, sizeof(result));
        return error;
    }
    const auto &pairing = observation.radios.pairing;
    const auto &codex_link = observation.radios.codex_link;
    if (have_result && result.command.generation == s_generation) {
        s_command_pending = false;
        scene.content.control_revision = ++s_navigation_revision;
        changed = true;
        if (result.error != ESP_OK) {
            const bool codex_work = result.command.work == Work::CodexStart ||
                result.command.work == Work::CodexCopyCode ||
                result.command.work == Work::CodexDiscover;
            const bool expired_codex_code = result.command.work == Work::CodexCopyCode &&
                result.error == ESP_ERR_INVALID_STATE && codex_link.data_valid &&
                codex_link.attempt_id == result.command.attempt &&
                codex_link.state == CodexState::Paired;
            // A QR copy can race credentials/cancellation: follow the newer real
            // state instead of treating an expired session as a renderer crash.
            const bool expired_qr = result.command.work == Work::SessionQr &&
                result.error == ESP_ERR_INVALID_STATE && pairing.data_valid &&
                (pairing.state != PairState::ReadyToScan || pairing.attempt_id != result.command.attempt ||
                 pairing.provisioning_session_id != result.command.session);
            if (codex_work && !expired_codex_code) {
                error = show_codex_error(scene, result.error);
            } else if (!expired_qr && !expired_codex_code) {
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
            // The session QR now belongs to screen 07, not an intermediate route.
            static constexpr char title[] = "Scan to pair";
            // A retried QR copy may complete while the local error scene is active.
            error = s_route == Route::Pairing07 ? ESP_OK : enter_route(Route::Pairing07, scene);
            if (error == ESP_OK) {
                ++s_text_generation;
                error = set_scene_title(scene, {title, sizeof(title) - 1U});
            }
            if (error == ESP_OK) {
                s_local_failure = false;
                s_view.qr = result.qr;
                s_view.qr_bounds = kSessionQrBounds;
                s_view.line_count = 0;
                s_session = result.command.session;
                error = attach_content(scene);
            }
        } else if (result.command.work == Work::Start || result.command.work == Work::Retry) {
            s_local_failure = false;
            s_attempt = result.command.attempt + 1U; // Backend admission increments exactly once.
            s_pairing_owned = true;
            s_last_pairing_change_us = -1;
        } else if (result.command.work == Work::CompleteFirstBoot) {
            s_local_failure = false;
            s_first_boot_persisted = true;
        } else if (result.command.work == Work::CodexStart) {
            s_codex_error_visible = false;
            s_codex_attempt = result.command.attempt + 1U;
            s_codex_owned = true;
            s_last_codex_change_us = -1;
            wipe(s_codex_code, sizeof(s_codex_code));
        } else if (result.command.work == Work::CodexCopyCode &&
                   codex_link.data_valid && !codex_link.data_stale &&
                   codex_link.attempt_id == result.command.attempt &&
                   codex_link.state == CodexState::Pairing) {
            std::memcpy(s_codex_code, result.codex_code, sizeof(s_codex_code));
            error = enter_route(Route::ConnectCodex12, scene);
        } else if (result.command.work == Work::CodexDiscover) {
            s_codex_error_visible = false;
        }
        wipe(&result, sizeof(result));
        if (error != ESP_OK) {
            return error;
        }
    } else {
        wipe(&result, sizeof(result));
    }
    if (s_codex_owned) {
        if (s_command_pending && codex_link.data_valid &&
            codex_link.attempt_id != s_codex_attempt) {
            return ESP_OK;
        }
        if (!codex_link.data_valid || codex_link.data_stale ||
            codex_link.attempt_id != s_codex_attempt) {
            changed = true;
            return show_codex_error(
                scene,
                codex_link.last_error != ESP_OK
                    ? codex_link.last_error
                    : ESP_ERR_INVALID_STATE
            );
        }
        if (codex_link.changed_at_us != s_last_codex_change_us) {
            changed = true;
            s_last_codex_change_us = codex_link.changed_at_us;
            if (codex_link.state == CodexState::Paired &&
                s_route == Route::ConnectCodex12) {
                s_codex_error_visible = false;
                error = enter_route(Route::SetupComplete13, scene);
            } else if (codex_link.state == CodexState::Connected &&
                       s_route == Route::LookingForPc14) {
                error = enter_route(Route::Connected15, scene);
            } else if (codex_link.state == CodexState::Discovering &&
                       s_route == Route::Connected15) {
                error = enter_route(Route::LookingForPc14, scene);
            } else if ((codex_link.state == CodexState::Failed ||
                        codex_link.state == CodexState::TimedOut) &&
                       !s_codex_error_visible) {
                error = show_codex_error(
                    scene,
                    codex_link.last_error != ESP_OK
                        ? codex_link.last_error
                        : ESP_ERR_INVALID_STATE
                );
            } else if (codex_link.state == CodexState::Idle) {
                error = show_codex_error(scene, ESP_ERR_INVALID_STATE);
            }
            if (error != ESP_OK) {
                return error;
            }
        }
        if (codex_link.state == CodexState::Pairing &&
            s_codex_code[0] == '\0' && !s_command_pending) {
            error = enqueue(Work::CodexCopyCode, codex_link.attempt_id);
            if (error != ESP_OK) {
                changed = true;
                return show_codex_error(scene, error);
            }
        }
        if (s_route == Route::ConnectCodex12 ||
            s_route == Route::SetupComplete13 ||
            s_route == Route::LookingForPc14 ||
            s_route == Route::Connected15) {
            return ESP_OK;
        }
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
                    const char *message = pairing.state == PairState::Connecting ? "Connecting to Wi-Fi" :
                        pairing.state == PairState::Completing ? "Finishing setup" :
                        pairing.state == PairState::Cancelling ? "Closing pairing" : "Starting Bluetooth";
                    error = show_pairing_progress(scene, message);
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
        if (s_view.qr.side != 0) {
            // Never leave an old session QR visible while a replacement encodes.
            changed = true;
            error = show_pairing_progress(scene, "Preparing pairing QR");
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
            wifi.last_error == ESP_OK && wifi.connected && wifi.ip_acquired && wifi.ip_address[0] != '\0';
        const auto &ble = observation.radios.ble;
        const bool ble_restarted = ble.data_valid && !ble.data_stale &&
            ble.health == LUNASHELL::DeviceHealth::Ready && ble.last_error == ESP_OK &&
            ble.advertising && ble.device_name[0] != '\0';
        if (online && !s_first_boot_persisted) {
            if (s_local_failure && s_failed_work == Work::CompleteFirstBoot) {
                return ESP_OK; // Retry remains an explicit action on the visible error route.
            }
            if (!s_command_pending) {
                changed = true;
                error = s_route == Route::Pairing07 ? ESP_OK : enter_route(Route::Pairing07, scene);
                if (error == ESP_OK) {
                    error = prepare_action(OnboardingAction::None, false);
                }
                if (error == ESP_OK) {
                    error = show_pairing_progress(scene, "Saving setup");
                }
                if (error == ESP_OK) {
                    error = enqueue(Work::CompleteFirstBoot, pairing.attempt_id);
                }
            }
        } else if (online && s_first_boot_persisted && !ble_restarted) {
            if (s_route != Route::Pairing07 || s_view.line_count == 0 || s_view.action_visible) {
                changed = true;
                error = enter_route(Route::Pairing07, scene);
                if (error == ESP_OK) {
                    error = prepare_action(OnboardingAction::None, false);
                }
                if (error == ESP_OK) {
                    error = show_pairing_progress(scene, "Waiting for Bluetooth");
                }
            }
#if CONFIG_LUNASHELL_BUILD_MODE_DEBUG
        } else if (online && s_first_boot_persisted && !s_pairing_debug_dismissed) {
            if (s_route != Route::DebugText) {
                changed = true;
                error = enter_pairing_debug(observation, scene);
            }
#endif
        } else if (!online && s_route != Route::Online09) {
            // Manager completion alone is not an online route. Keep the pending
            // presentation until the same canonical observation has a live IP.
            if (s_route != Route::Pairing07 || s_view.line_count == 0 || s_view.action_visible) {
                changed = true;
                error = enter_route(Route::Pairing07, scene);
                if (error == ESP_OK) {
                    error = prepare_action(OnboardingAction::None, false);
                }
                if (error == ESP_OK) {
                    error = show_pairing_progress(scene, "Waiting for Wi-Fi");
                }
            }
        } else if (s_pairing_debug_dismissed &&
                   (s_route != Route::Online09 || !s_online_presentation_current || online != s_online_link)) {
            changed = true;
            error = enter_route(Route::Online09, scene);
            s_online_link = online;
            s_online_presentation_current = true;
            if (error == ESP_OK && !online) {
                // TextSpan is borrowed by later frames, not copied by set_scene_title.
                static constexpr char title[] = "No connection";
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

// No parameters: only routes with a real previous destination expose Back.
bool previous_navigation_available()
{
    return s_route == Route::Install06 ||
        (s_view.action_visible && s_view.action_id == OnboardingAction::Cancel);
}

// No parameters: forward actions retain their backend admission and enabled state.
bool next_navigation_available()
{
    return s_view.action_visible && s_view.action_id != OnboardingAction::Cancel;
}

// No parameters: hidden lateral arrows do not retain invisible touch targets.
bool previous_control_visible()
{
    return previous_navigation_available() && s_route != Route::Install06 && !uses_footer_action();
}

// No parameters: a captioned footer replaces the setup journey's right chevron.
bool next_control_visible()
{
    return next_navigation_available() && !uses_footer_action();
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
    if (s_command_pending || s_work_busy.load(std::memory_order_acquire) ||
        ((action_hit || cancel_hit) && !s_view.action.enabled)) {
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
        // Pairing ownership ends only after the persisted handoff and the user's
        // explicit Continue; later journey screens must not be forced back to 09.
        s_pairing_owned = false;
        return enter_route(Route::MakeItYours10, scene);
    }
    if (action_hit && s_route == Route::MakeItYours10) {
        return enter_route(Route::Settings36, scene);
    }
    if (action_hit && s_route == Route::Settings36) {
        const SceneId done = kCodexIntegrationEnabled
            ? SceneId::ConnectCodex : SceneId::SetupComplete;
        return enter_look_route(scene, {SceneId::Settings, done});
    }
    if (action_hit && s_route == Route::ConnectCodex12) {
        if (s_view.action_id != OnboardingAction::Retry) {
            return ESP_OK;
        }
        return begin_codex_pairing(scene);
    }
    if (action_hit && s_route == Route::SetupComplete13) {
        if (!kCodexIntegrationEnabled) {
            scene = {};
            reset_onboarding();
            return ESP_OK;
        }
        return enter_codex_idle(scene);
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
    if (s_route == Route::Failed08 && s_local_failure &&
        s_failed_work == Work::CompleteFirstBoot) {
        esp_err_t error = enter_route(Route::Pairing07, scene);
        if (error == ESP_OK) {
            error = prepare_action(OnboardingAction::None, false);
        }
        if (error == ESP_OK) {
            error = show_pairing_progress(scene, "Saving setup");
        }
        if (error == ESP_OK) {
            error = enqueue(Work::CompleteFirstBoot, pairing.attempt_id);
        }
        return error == ESP_OK ? ESP_OK : show_error(scene, error);
    }
    if (!pairing.data_valid || pairing.data_stale) {
        return show_error(scene, pairing.last_error != ESP_OK ? pairing.last_error : ESP_ERR_INVALID_STATE);
    }
    if (s_route == Route::Failed08 && s_local_failure && s_failed_work == Work::SessionQr &&
        pairing.state == PairState::ReadyToScan && pairing.attempt_id == s_attempt) {
        esp_err_t error = enqueue(Work::SessionQr, s_attempt, pairing.provisioning_session_id);
        if (error == ESP_OK) {
            error = enter_route(Route::Pairing07, scene);
        }
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
    if (s_route == Route::Pairing07) {
        // Retire the session's secret immediately; navigation still awaits Cancelled.
        const esp_err_t action_error = prepare_action(OnboardingAction::Cancel, false);
        return action_error == ESP_OK ? show_pairing_progress(scene, "Closing pairing") : action_error;
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
#if CONFIG_LUNASHELL_BUILD_MODE_DEBUG
    if (s_route == Route::DebugText) {
        consumed = true;
        if (!pressed_edge) {
            return ESP_OK;
        }
        s_pairing_debug_dismissed = true;
        return enter_route(Route::Online09, scene);
    }
#endif
    if (s_swipe_route != s_route || (s_swipe_active && s_swipe_rotation != rotation_degrees)) {
        s_swipe_active = false;
        cancel_look_touch();
    }
    const CODY2D::CapsuleControl back{kBackBounds, {}, NavigationData::top, NavigationData::bottom, true};
    const CODY2D::CapsuleControl next{kForwardBounds, {}, NavigationData::top, NavigationData::bottom, true};
    if (pressed_edge) {
        const bool footer_hit = uses_footer_action() && s_view.action_visible &&
            CODY2D::capsule_contains(s_view.action, point);
        if (footer_hit) {
            consumed = true;
            return activate_onboarding_navigation(scene, s_view.action_id == OnboardingAction::Cancel ?
                NavigationDirection::Previous : NavigationDirection::Next);
        }
        const bool back_hit = previous_control_visible() && CODY2D::capsule_contains(back, point);
        const bool next_hit = next_control_visible() && CODY2D::capsule_contains(next, point);
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
            cancel_look_touch();
            consumed = true;
            return activate_onboarding_navigation(scene,
                dx < 0 ? NavigationDirection::Next : NavigationDirection::Previous);
        }
    }
    if (s_route == Route::Look11) {
        LookTouchResult result;
        const esp_err_t error = handle_look_touch(scene, pressed ? point : s_swipe_last,
            pressed, pressed_edge, s_swipe_axis == GestureAxis::Horizontal, result);
        consumed = result.consumed;
        if (error != ESP_OK) {
            return error;
        }
        if (result.save_id != nullptr) {
            return enqueue_look_save(scene, result.save_id);
        }
        if (result.selection_confirmed) {
            const esp_err_t content_error = attach_look_content(scene, s_navigation_revision);
            if (content_error != ESP_OK) {
                return content_error;
            }
            consumed = true;
        }
        if (result.done_requested) {
            SceneId destination;
            const esp_err_t destination_error = look_exit_destination(LookExit::Done, destination);
            if (destination_error != ESP_OK) {
                return destination_error;
            }
            consumed = true;
            return enter_host_destination(destination, scene);
        }
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
    if (previous_control_visible()) {
        const bool enabled = !s_command_pending &&
            (s_view.action_id != OnboardingAction::Cancel || s_view.action.enabled);
        error = draw_navigation_chevron(NavigationDirection::Previous, enabled, strip, first_row, first_column);
    }
    if (error == ESP_OK && next_control_visible()) {
        error = draw_navigation_chevron(NavigationDirection::Next, !s_command_pending && s_view.action.enabled,
            strip, first_row, first_column);
    }
    return error;
}

} // namespace CODY

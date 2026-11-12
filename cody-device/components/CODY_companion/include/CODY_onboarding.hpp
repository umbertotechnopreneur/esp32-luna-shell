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

#include <cstdint>
#include <cstddef>
#include "CODY_scene.hpp"
#include "LUNASHELL_codex_events.hpp"
#include "LUNASHELL_speech_state.hpp"

namespace CODY {

struct WidgetVisibility;
enum class PreferenceId : uint8_t;

// No parameters: create one bounded action worker before the renderer starts.
esp_err_t onboarding_service_init();

// text: Complete bounded speech text, copied before this call returns.
// text_bytes: Exact nonzero UTF-8 byte length, at most kSpeechMaximumTextBytes.
// event_id: Exact authenticated event identity, at most 32 bytes.
// event_revision: Canonical event revision that produced the text.
// kind: Question event kind.
// state: Pending question or Delivered answer represented by the text.
// selected_id: Exact selected answer identity, empty when none.
// Exception: Returns busy, unavailable or invalid input without starting speech.
esp_err_t request_event_speech(const char *text, size_t text_bytes, const char *event_id,
    uint64_t event_revision, LUNASHELL::CodexEventKind kind, LUNASHELL::CodexEventState state,
    const char *selected_id);

struct EventSpeechResult {
    char event_id[33]{};
    char selected_id[13]{};
    uint64_t revision = 0;
    LUNASHELL::CodexEventKind kind = LUNASHELL::CodexEventKind::None;
    LUNASHELL::CodexEventState state = LUNASHELL::CodexEventState::None;
    esp_err_t error = ESP_OK;
    uint32_t speech_request_id = 0;
};

// result: Receives one exact completed worker admission receipt.
// Returns true once per receipt; worker errors are preserved in result.error.
bool take_event_speech_result(EventSpeechResult &result);

#if defined(CODY_DEVICE_UI_ONLY)
// content: Receives the About link's immutable heading and project QR.
// Exception: Returns the original optional QR preparation error.
esp_err_t prepare_about_project_link(SceneContent &content);
#endif

// No parameters: bind the existing Hello scene to the first-setup journey.
void begin_onboarding();

// No parameters: evaluate saved setup after the existing startup sequence completes.
void request_saved_setup_startup();

// scene: Display-owned startup frame, admitted after Wi-Fi and pairing services start.
// first_boot: Effective local setup setting from the existing persistence owner.
esp_err_t start_device_setup(SceneFrame &scene, bool first_boot);

// No parameters: true when the ordinary connection or activity route can show the dashboard.
bool onboarding_dashboard_active();

// No parameters: true when the native Tools menu may host a Codex child.
bool onboarding_codex_tools_active();

// No parameters: the short guide keeps both available navigation arrows visible.
bool onboarding_quick_guide_active();

// Bounded repaint for the saved Wi-Fi and Codex waiting glyphs.
inline constexpr DamageRect kConnectionActivityBounds{197, 116, 269, 204};

// now_us: Current monotonic time. Zero phase means no connection animation.
uint8_t onboarding_connection_activity_phase(int64_t now_us);

// now_us: Monotonic time used to recognize a settled saved-network startup failure.
bool onboarding_saved_wifi_startup_offline(int64_t now_us);

// phase: Active connection animation step, or zero when the glyph is absent.
// strip: Borrowed RGB565 tile, including its actual stride and byte order.
// first_row: Logical y origin of the tile.
// first_column: Logical x origin of the tile.
// Exception: Returns the original raster or tile validation failure.
esp_err_t draw_connection_activity_rows(uint8_t phase, CODY2D::Surface strip,
                                       uint16_t first_row, uint16_t first_column);

// No parameters: only stable routes without pending work admit the setup gesture.
bool can_open_setup_menu();

// No parameters: the Home entry is admitted only when its existing worker is free.
bool can_open_wifi_setup_from_home();

// scene: Display-owned scene receiving the explicitly requested Wi-Fi journey.
// Exception: Returns the original admission or route preparation failure.
esp_err_t open_wifi_setup_from_home(SceneFrame &scene);

// scene: Home-owned frame receiving the Bluetooth setup introduction.
// Exception: Returns the original admission or presentation error.
esp_err_t open_wifi_pairing_from_home(SceneFrame &scene);

// opacity: Last presented chrome visibility; hidden controls do not admit touch.
void set_home_wifi_chrome_opacity(uint8_t opacity);

// No parameters: true only while the Home-launched Wi-Fi journey owns the scene.
bool home_wifi_setup_active();

// No parameters: true while the local Wi-Fi password keyboard is shown.
bool onboarding_wifi_keyboard_active();

// No parameters: true while the current setup scene displays a QR code.
bool onboarding_qr_visible();

// scene: Display-worker-owned frame receiving the reversible setup menu.
// Exception: Returns the original scene/content preparation error.
esp_err_t open_setup_menu(SceneFrame &scene);

// scene: Stable route receiving Options after a two-second physical BOOT hold.
// Exception: Returns NOT_FINISHED while a device route is busy, or the original preparation error.
esp_err_t open_companion_options(SceneFrame &scene);

// scene: Return to the Tools section while preserving the route that opened Options.
// Exception: Returns the original route or content preparation error.
esp_err_t return_to_options_tools(SceneFrame &scene);

// scene: Return to the Device section after opening About from Options.
// Exception: Returns the original route or content preparation error.
esp_err_t return_to_options_device(SceneFrame &scene);

// scene: Options frame to return to ordinary dashboard and authenticated event routing.
esp_err_t open_device_dashboard(SceneFrame &scene);

// scene: Display-owned route retained until pairing cleanup has completed.
// Exception: Returns NOT_FINISHED while an owned operation closes or the original error.
esp_err_t open_device_home(SceneFrame &scene);

enum class WidgetOpenRequest : uint8_t {
    None, Home, Statistics, Visibility, Timer, CodexBrowser, Audio, About, ReturnFromOptions
};

// available: True after this device has received the broker's authenticated Events projection.
void set_onboarding_inbox_available(bool available);

// request_id: Exact question selected from the retired native Events list.
// Exception: Returns the original signed selection admission error.
esp_err_t open_onboarding_inbox_question(const char *request_id);

// No parameters: consumes one released Options selection owned by the display task.
WidgetOpenRequest take_widget_open_request();

// visibility: Exact validated draft selected by the local visibility editor.
// Exception: Returns invalid values or mailbox/route admission errors before persistence starts.
esp_err_t request_widget_visibility_save(const WidgetVisibility &visibility);

// error: Receives the original persistence result only for the current presentation generation.
// Unrelated mailbox results remain available to the onboarding route owner.
bool take_widget_visibility_save_result(esp_err_t &error);

// No parameters: consume one successfully changed local preference.
bool take_preference_confirmation();

// No parameters: queue a worker-owned read and apply of both persisted audio preferences.
// Exception: Returns NOT_FINISHED when the shared settings worker is busy, or its admission failure.
esp_err_t request_audio_preferences_refresh();

// preference: PlaybackVolume or MicrophoneGain identity.
// value: Explicit user selection, 0..100 percent or 0..30 dB in steps of three.
// Exception: Returns original validation or mailbox admission failure before persistence begins.
esp_err_t request_audio_preference(PreferenceId preference, uint32_t value);

// refresh: Receives true for a refresh; false identifies a saved preference.
// preference: Receives the saved identity when refresh is false.
// error: Receives the actual worker result, including partial independent failures.
// Call even after closing Audio so the shared worker's result cannot remain occupied.
bool take_audio_preference_result(bool &refresh, PreferenceId &preference, esp_err_t &error);

enum class LateralNavigation : uint8_t { None, Previous, Next };

// point: Acquired logical position on the existing circular lateral targets.
LateralNavigation lateral_navigation_at(CODY2D::Point point);

// point: Acquired logical position; true only for a lateral control present on this route.
bool onboarding_lateral_control_at(CODY2D::Point point);

// next: True when a forward destination exists. Back is always available here.
// strip: Borrowed RGB565 tile including the lateral regions.
// first_row: Logical y origin of the tile.
// first_column: Logical x origin of the tile.
// opacity: Arrow visibility against the dashboard background.
// Exception: Returns the first native drawing failure.
esp_err_t draw_lateral_navigation(bool next, CODY2D::Surface strip,
                                  uint16_t first_row, uint16_t first_column, uint8_t opacity = 255);

// No parameters: retire route identities and wipe the displayed session QR.
void reset_onboarding();

// No parameters: display-owner-only evidence of successful first-boot persistence in this journey.
bool onboarding_first_boot_completed();

// No parameters: cancel gesture recognition after an invalid acquired report.
void cancel_onboarding_touch();

// No parameters: preserve Home marquee position while its route is visually preempted.
void suspend_onboarding_command_hints();

// bounds: Receives the selected network label's clipped damage rectangle.
// offset: Receives its current pixel offset within the same retained frame.
// Returns false when no selected network label is visible.
bool onboarding_selected_label_damage(DamageRect &bounds, uint16_t &offset);

// scene: Current worker-owned scene, replaced only by a validated route.
// completed_scene: Last fully retired scene; typewriter evaluation alone is not completion.
// active_time_us: Existing animation clock, excluding pause and standby.
// inactivity_active_time_us: Awake route clock, including animation pause and excluding standby.
// changed: Receives whether a new scene/content revision requires a frame.
// scene_presented: Whether this scene was the last fully visible frame.
esp_err_t update_onboarding(SceneFrame &scene, const SceneFrame &completed_scene,
                            int64_t active_time_us, int64_t inactivity_active_time_us, bool &changed,
                            bool scene_presented = true);

// point: Acquired logical sample, including contacts owned by Debug controls.
// pressed: Current hardware contact state.
// pressed_edge: True only for the first pressed sample of this contact.
// inactivity_active_time_us: Awake route clock, including animation pause and excluding standby.
// Exception: Returns the original screen-activity clock error.
esp_err_t observe_onboarding_touch_activity(CODY2D::Point point, bool pressed,
                                            bool pressed_edge, int64_t inactivity_active_time_us);

// scene: Worker-owned route to update after an admitted action.
// point: Acquired logical sample, already mapped by the shared rotation owner.
// pressed: Current hardware contact state.
// pressed_edge: True only for the first pressed sample of this contact.
// rotation_degrees: Evaluated logical orientation; a change cancels in-flight gestures.
// consumed: True while an enabled OOBE control owns this contact.
// Exception: Returns the real route, persistence, snapshot or content error.
esp_err_t handle_onboarding_touch(SceneFrame &scene, CODY2D::Point point,
                                  bool pressed, bool pressed_edge,
                                  uint16_t rotation_degrees, bool &consumed);

// strip: Borrowed RGB565 tile; no display writes or allocations occur here.
// first_row: Logical y origin of the tile.
// first_column: Logical x origin of the tile.
// opacity: Arrow visibility against the scene background.
// background: Native RGB565 color of the currently rendered scene.
esp_err_t draw_onboarding_navigation(CODY2D::Surface strip, uint16_t first_row,
                                    uint16_t first_column, uint8_t opacity = 255, uint16_t background = 0);

} // namespace CODY

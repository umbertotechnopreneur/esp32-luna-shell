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

#include "CODY_scene.hpp"

namespace CODY {

// The host supplies both destinations; Done need not mean Back during setup.
struct LookNavigationContext {
    SceneId caller;
    SceneId done;
};
enum class LookExit : uint8_t { Back, Done };

struct LookTouchResult {
    bool consumed = false;
    bool changed = false;
    bool selection_confirmed = false;
    bool done_requested = false;
    // Flash-owned catalog identity. No pointer into touch or stack storage.
    const char *save_id = nullptr;
};

// Operation and presentation outcomes stay separate: a failed optional save
// is not a successful save, and is not automatically a broken display.
struct LookSaveCompletion {
    esp_err_t save_error = ESP_OK;
    esp_err_t presentation_error = ESP_OK;
};

// scene: Receives the prepared selector; call only between retired frames.
// navigation: Explicit caller and completion destinations, validated before entry.
// Exception: Returns the real catalog, destination, layout or font error.
esp_err_t open_look_screen(SceneFrame &scene, const LookNavigationContext &navigation);

// No parameters: retire the borrowed view after its last frame; does not cancel NVS work.
void reset_look_screen();

// No parameters: cancel only gesture state on route/orientation change.
void cancel_look_touch();

// scene: Current selector frame borrowing the module's fixed storage.
// control_revision: Host-owned revision of the shared lateral controls.
// Exception: Returns the real scene-content validation error.
esp_err_t attach_look_content(SceneFrame &scene, uint32_t control_revision);

// scene: Active selector after the host advances its clock; previous frame must be retired.
// control_revision: Host-owned revision of the shared lateral controls.
// changed: Preserves existing changes and reports newly visible typed prefixes or controls.
// Exception: Returns the real selector, caption, clock or scene-content error.
esp_err_t update_look_text(SceneFrame &scene, uint32_t control_revision, bool &changed);

// exit: Back uses the caller; Done uses the separately authored completion destination.
// destination: Receives a validated scene identity only when navigation is permitted.
// Exception: Returns ESP_ERR_INVALID_STATE while saving or outside the selector.
esp_err_t look_exit_destination(LookExit exit, SceneId &destination);

// scene: Current selector frame, never mutated while DMA owns it.
// point: Real logical contact position; release uses the last acquired pressed position.
// pressed: Actual contact state.
// pressed_edge: True only for the first pressed sample.
// horizontal_gesture: Host has assigned the gesture to horizontal navigation.
// result: Receives consumption, redraw and optional stable save identity.
// Exception: Returns the real view, catalog or content-layout error.
esp_err_t handle_look_touch(SceneFrame &scene, CODY2D::Point point, bool pressed,
                            bool pressed_edge, bool horizontal_gesture, LookTouchResult &result);

// scene: Current selector frame.
// id: Exact catalog identity requested by a real tap.
// request_id: Nonzero operation identity retained until completion.
// Exception: Returns a real state, identity, font or content error; performs no I/O.
esp_err_t begin_look_save(SceneFrame &scene, const char *id, uint32_t request_id);

// scene: Current selector frame, updated only by the display owner.
// request_id: Identity of the one pending operation.
// save_error: Original persistence result, retained independently of presentation errors.
// Exception: The returned presentation_error reports stale completion/layout failures.
LookSaveCompletion complete_look_save(SceneFrame &scene, uint32_t request_id, esp_err_t save_error);

} // namespace CODY

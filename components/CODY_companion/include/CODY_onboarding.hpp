#pragma once

#include "CODY_scene.hpp"

namespace CODY {

// No parameters: create one bounded action worker before the renderer starts.
esp_err_t onboarding_service_init();

// No parameters: bind the existing Hello scene to the first-setup journey.
void begin_onboarding();

// No parameters: retire route identities and wipe the displayed session QR.
void reset_onboarding();

// scene: Current worker-owned scene, replaced only by a validated route.
// completed_scene: Last fully retired scene; typewriter evaluation alone is not completion.
// active_time_us: Existing animation clock, excluding pause and standby.
// inactivity_active_time_us: Awake route clock, including animation pause and excluding standby.
// changed: Receives whether a new scene/content revision requires a frame.
esp_err_t update_onboarding(SceneFrame &scene, const SceneFrame &completed_scene,
                            int64_t active_time_us, int64_t inactivity_active_time_us, bool &changed);

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
esp_err_t draw_onboarding_navigation(CODY2D::Surface strip, uint16_t first_row,
                                    uint16_t first_column);

} // namespace CODY

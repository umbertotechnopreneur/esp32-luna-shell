#pragma once

#include "CODY2D_text.hpp"
#include "CODY_dirty_plan.hpp"

namespace CODY {

// Explicit IDs do not change if authoring declarations are reordered.
enum class SceneId : uint16_t { AnimationDemo = 0, Greeting = 1 };
enum class SceneElementId : uint8_t { Cloud, Title, Content };
enum class SceneIntroPhase : uint8_t { BitmapFadeOut, CloudFadeIn, Complete };

struct SceneIntroDefinition {
    uint16_t bitmap_fade_out_ms;
    uint16_t cloud_fade_in_ms;
};

struct SceneDefinition {
    SceneId id;
    const char *name;
    Transform cloud;
    SceneIntroDefinition intro;
    Channels face;
    const CODY2D::MonoFont *font;
    CODY2D::TextSpan title;
    uint16_t title_top;
    uint16_t title_color;
    uint16_t character_interval_ms;
    bool navigation_visible;
    bool readout_visible;
};

// Optional caller-owned content, e.g. text, a button or a QR in a later scene.
// The callback borrows immutable state; it may neither allocate nor submit DMA.
struct SceneContent {
    SceneElementId id = SceneElementId::Content;
    DamageRect bounds{};
    const void *state = nullptr;
    uint32_t revision = 0;
    // state: Immutable content owned by the caller through complete frame retirement.
    // strip: Borrowed tile clipped to this content slot; never submit a display transaction.
    // first_row: Logical y origin shared with cloud and title.
    // first_column: Logical x origin shared with cloud and title.
    esp_err_t (*draw_rows)(const void *state, CODY2D::Surface strip,
                          uint16_t first_row, uint16_t first_column) = nullptr;
};

struct SceneFrame {
    const SceneDefinition *definition = nullptr;
    CODY2D::TextLine title{};
    SceneContent content{};
    uint16_t visible_characters = 0;
    uint32_t character_remainder_us = 0;
    SceneIntroPhase intro_phase = SceneIntroPhase::Complete;
    uint32_t intro_elapsed_us = 0;
    bool valid = false;
};

struct ScenePresentation {
    bool boot_bitmap_visible = false;
    uint8_t opacity = 255;
    bool content_visible = true;
};

// frame: Valid prepared scene whose intro uses only active animation time.
// presentation: Receives the selected layer, opacity and content gate on success.
esp_err_t prepare_scene_presentation(const SceneFrame &frame, ScenePresentation &presentation);

// identity: Named generic scene, not a table position; demo has its own existing route.
// definition: Receives an immutable flash-owned definition on success only.
esp_err_t find_scene(SceneId identity, const SceneDefinition *&definition);

// definition: Flash-owned cloud/expression/title layout retained by this frame.
// frame: Receives a fresh typewriter and prepared text only after validation succeeds.
esp_err_t begin_scene(const SceneDefinition &definition, SceneFrame &frame);

// frame: Prepared reusable layout; geometry and optional content stay owned by it.
// title: Immutable ASCII text retained until all frames referencing it retire.
esp_err_t set_scene_title(SceneFrame &frame, CODY2D::TextSpan title);

// frame: Current prepared scene; unchanged if the request is invalid.
// active_delta_us: Nonnegative animation time, excluding pause and standby intervals.
esp_err_t advance_scene(SceneFrame &frame, int64_t active_delta_us);

// frame: Valid prepared scene whose content slot is updated after validation.
// content: Immutable optional content, wholly below the title and within the circle.
esp_err_t set_scene_content(SceneFrame &frame, const SceneContent &content);

// previous: Last completely displayed text/content state, not a partially written frame.
// current: Next prepared scene sharing the same stable scene identity.
// plan: Existing cloud damage; text/content changes are accumulated, never cleared.
esp_err_t plan_scene_damage(const SceneFrame &previous, const SceneFrame &current, DirtyPlan &plan);

// frame: Valid immutable text prefix and optional content for this frame.
// strip: Cloud-composited caller-owned RGB565 tile; this layer does not clear it.
// first_row: Logical tile y origin shared by all layers.
// first_column: Logical tile x origin shared by all layers.
esp_err_t draw_scene_rows(const SceneFrame &frame, CODY2D::Surface strip,
                          uint16_t first_row, uint16_t first_column);

} // namespace CODY

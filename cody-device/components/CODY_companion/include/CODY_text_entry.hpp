#pragma once

#include <array>
#include <cstdint>
#include "CODY2D_controls.hpp"
#include "CODY_scene.hpp"

namespace CODY {

// This bounded draft belongs to the display/input owner, never to telemetry.
constexpr size_t kTextEntryCapacity = 64;
enum class TextEntryMode : uint8_t { Letters, Numbers, Symbols };
enum class TextEntryResult : uint8_t { None, Changed, Submit };

struct TextEntry {
    char text[kTextEntryCapacity + 1]{};
    uint8_t length = 0;
    uint8_t limit = 0;
    int8_t pending_key = -1;
    uint8_t pending_index = 0;
    int64_t last_tap_us = 0;
    TextEntryMode mode = TextEntryMode::Letters;
    bool uppercase = false;
    bool secret = false;
    bool reveal = false;
};

struct TextEntryView {
    std::array<CODY2D::CapsuleControl, 15> keys{};
    std::array<std::array<char, 12>, 15> labels{};
    CODY2D::CapsuleControl field{};
    CODY2D::TextLine subtitle{};
    char visible_text[kTextEntryCapacity + 1]{};
    uint32_t revision = 0;
    bool valid = false;
};

struct TextEntryTouch {
    CODY2D::Point start{};
    CODY2D::Point last{};
    int8_t key = -1;
    uint16_t rotation = 0;
    bool active = false;
    bool moved = false;
};

// entry: Caller-owned draft, cleared including previous credentials.
// limit: Maximum admitted printable ASCII bytes, between 1 and 64.
// secret: Mask the draft by default and offer explicit visibility control.
// Exception: Rejects an invalid limit.
esp_err_t begin_text_entry(TextEntry &entry, uint8_t limit, bool secret);

// entry: Active draft whose complete text is replaced only after validation.
// text: NUL-terminated printable ASCII input; must fit the configured limit.
// Exception: Rejects an invalid draft, non-printable text or excessive length.
esp_err_t set_text_entry_text(TextEntry &entry, const char *text);

// entry: Caller-owned draft to erase after cancellation or final consumption.
void clear_text_entry(TextEntry &entry);

// entry: Active draft, updated only after a complete, stationary touch.
// key: Key index returned by text_entry_key_at.
// now_us: Monotonic touch-release time in microseconds.
// result: Receives the changed or submit outcome; submission does not connect.
// Exception: Rejects invalid input or a clock that moves backwards.
esp_err_t activate_text_entry_key(TextEntry &entry, uint8_t key, int64_t now_us, TextEntryResult &result);

// point: Logical touch coordinate after the existing orientation transform.
// Returns the key index, or -1 when outside all keys.
int text_entry_key_at(CODY2D::Point point);

// touch: Caller-owned gesture tracking; clear it when the route is hidden.
// entry: Active draft updated only after a stationary release on the same key.
// point: Acquired logical coordinate in the display owner's current orientation.
// pressed: Current hardware contact state.
// pressed_edge: True only on the first sample of a contact.
// rotation: Display owner's current logical rotation.
// now_us: Monotonic timestamp for multi-tap timing.
// result: Receives Changed or Submit for an admitted release.
// consumed: Receives whether the keyboard owns this gesture.
// Exception: Preserves input and monotonic clock errors from key activation.
esp_err_t handle_text_entry_touch(TextEntryTouch &touch, TextEntry &entry, CODY2D::Point point,
    bool pressed, bool pressed_edge, uint16_t rotation, int64_t now_us,
    TextEntryResult &result, bool &consumed);

// entry: Active draft; never retained by the renderer.
// title: Immutable, bounded ASCII title retained until frame retirement.
// placeholder: Optional immutable ASCII hint retained until frame retirement.
// submit_label: Immutable action caption retained until frame retirement.
// subtitle: Optional immutable instruction retained until frame retirement.
// font: Native font for the title, field and keys.
// view: Caller-owned presentation, retained without mutation until frame retirement.
// content: Receives the native renderer for a scene with its own title hidden; Back remains with the caller.
// Exception: Returns the original text or layout error.
esp_err_t prepare_text_entry_view(const TextEntry &entry, const char *title, const char *placeholder,
    const CODY2D::MonoFont &font, TextEntryView &view, SceneContent &content,
    const char *submit_label = "Done", const char *subtitle = nullptr);

// view: Retired presentation whose visible field may contain a revealed secret.
void clear_text_entry_view(TextEntryView &view);

} // namespace CODY

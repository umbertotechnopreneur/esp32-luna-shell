#include "CODY_text_entry.hpp"
#include "CODY_title_style.hpp"
#include "CODY_theme_generated.hpp"

#include <cstring>

namespace CODY {
namespace {

constexpr int64_t kMultiTapUs = 1000000;
constexpr uint16_t kInk = Theme::text_default;
constexpr const char *kLetters[] = {".-_@", "abc", "def", "ghi", "jkl", "mno", "pqrs", "tuv", "wxyz"};
constexpr const char *kNumbers[] = {"01", "2", "3", "4", "5", "6", "7", "8", "9"};
constexpr const char *kSymbols[] = {"!\"#$", "%&'(", ")*+,", "-./:", ";<=>", "?@[\\", "]^_`", "{|}~", "@_`"};

// key: Stable index in the three-column, five-row keypad.
// Returns a rectangle whose entire hit area fits the circular display.
CODY2D::Rect key_bounds(uint8_t key)
{
    if (key >= 12) return {static_cast<uint16_t>(104 + (key - 12) * 88), 340, 82, 43};
    return {static_cast<uint16_t>(73 + (key % 3) * 110),
        static_cast<uint16_t>(160 + (key / 3) * 45), 100, 39};
}

// entry: Active input mode.
// key: One of the nine character-group keys.
// Returns an immutable printable ASCII group.
const char *group_for(const TextEntry &entry, uint8_t key)
{
    if (entry.mode == TextEntryMode::Numbers) return kNumbers[key];
    if (entry.mode == TextEntryMode::Symbols) return kSymbols[key];
    return kLetters[key];
}

// entry: Draft whose pending multi-tap character is now committed.
void commit_pending(TextEntry &entry)
{
    entry.pending_key = -1;
    entry.pending_index = 0;
}

// font: Immutable native glyphs retained through frame retirement.
// Exception: Returns the original font or glyph error without measuring secret text.
esp_err_t validate_secret_font(const CODY2D::MonoFont &font)
{
    // Probe every allowed byte, independently of the draft. The renderer logs
    // rejected glyph bytes, so probing only the secret would disclose its content.
    static constexpr char printable[] =
        " !\"#$%&'()*+,-./0123456789:;<=>?@"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`"
        "abcdefghijklmnopqrstuvwxyz{|}~";
    static_assert(sizeof(printable) == 96, "Secret font validation covers printable ASCII");
    uint32_t width = 0;
    return CODY2D::measure_text_width(font,
        {printable, static_cast<uint16_t>(sizeof(printable) - 1U)}, width);
}

// font: Native glyphs retained by the caller.
// text: Persistent nonempty ASCII text.
// bounds: Full logical slot for the caption.
// line: Receives centered native text.
// Exception: Preserves measurement and glyph errors; rejects oversized captions.
esp_err_t centered_line(const CODY2D::MonoFont &font, const char *text,
    CODY2D::Rect bounds, CODY2D::TextLine &line)
{
    const auto length = std::strlen(text);
    if (length == 0 || length > kTextEntryCapacity || font.height > bounds.height) return ESP_ERR_INVALID_SIZE;
    CODY2D::TextSpan span{text, static_cast<uint16_t>(length)};
    uint32_t width = 0;
    const esp_err_t error = CODY2D::measure_text_width(font, span, width);
    if (error != ESP_OK) return error;
    if (width > bounds.width) return ESP_ERR_INVALID_SIZE;
    return CODY2D::prepare_text_line(font, span,
        {static_cast<int16_t>(bounds.x + (bounds.width - width) / 2),
         static_cast<int16_t>(bounds.y + (bounds.height - font.height) / 2)}, kInk, line);
}

// state: Retained, immutable view until the frame is retired.
// strip: Caller-owned native tile.
// first_row: Tile origin in logical display coordinates.
// first_column: Tile origin in logical display coordinates.
// Exception: Returns the first native drawing error.
esp_err_t draw_rows(const void *state, CODY2D::Surface strip, uint16_t first_row, uint16_t first_column)
{
    const auto *view = static_cast<const TextEntryView *>(state);
    if (view == nullptr || !view->valid) return ESP_ERR_INVALID_STATE;
    esp_err_t error = ESP_OK;
    if (view->subtitle.valid)
        error = draw_styled_subtitle_line(view->subtitle, strip, first_row, first_column, view->subtitle.text.length);
    if (error == ESP_OK) error = CODY2D::draw_capsule_rows(view->field, strip, first_row, first_column);
    for (const auto &key : view->keys) {
        if (error != ESP_OK) break;
        error = CODY2D::draw_capsule_rows(key, strip, first_row, first_column);
    }
    return error;
}

} // namespace

// entry: Caller-owned draft, including any secret that must be erased.
void clear_text_entry(TextEntry &entry)
{
    volatile char *bytes = entry.text;
    for (size_t i = 0; i < sizeof(entry.text); ++i) bytes[i] = 0;
    entry = {};
}

// entry: Caller-owned draft, cleared before reuse.
// limit: Maximum ASCII bytes, from one to the bounded capacity.
// secret: Whether to mask the field and expose Show/Hide.
// Exception: Rejects a limit outside the supported range.
esp_err_t begin_text_entry(TextEntry &entry, uint8_t limit, bool secret)
{
    if (limit == 0 || limit > kTextEntryCapacity) return ESP_ERR_INVALID_ARG;
    clear_text_entry(entry);
    entry.limit = limit;
    entry.secret = secret;
    return ESP_OK;
}

// entry: Active draft, modified only after the entire input has been validated.
// text: NUL-terminated printable ASCII input, no longer than the configured limit.
// Exception: Rejects non-printable, oversized or invalid input.
esp_err_t set_text_entry_text(TextEntry &entry, const char *text)
{
    if (text == nullptr || entry.limit == 0 || entry.limit > kTextEntryCapacity) return ESP_ERR_INVALID_ARG;
    size_t length = 0;
    while (length <= entry.limit && text[length] != '\0') {
        const auto byte = static_cast<unsigned char>(text[length]);
        if (byte < 32 || byte > 126) return ESP_ERR_INVALID_ARG;
        ++length;
    }
    if (length > entry.limit) return ESP_ERR_INVALID_SIZE;
    // memmove also supports a caller passing the draft's own buffer.
    std::memmove(entry.text, text, length);
    volatile char *remaining = entry.text + length;
    for (size_t i = length; i < sizeof(entry.text); ++i) *remaining++ = 0;
    entry.length = static_cast<uint8_t>(length);
    commit_pending(entry);
    return ESP_OK;
}

// point: Oriented touch coordinate in the same space as the drawn keys.
// Returns the matching key index, or -1.
int text_entry_key_at(CODY2D::Point point)
{
    for (uint8_t i = 0; i < 15; ++i) {
        CODY2D::CapsuleControl control{key_bounds(i)};
        control.corner_radius = 12;
        if (CODY2D::capsule_contains(control, point)) return i;
    }
    return -1;
}

// touch: Caller-owned contact state, discarded when the screen is hidden.
// entry: Active draft for released-key activation.
// point: Acquired coordinate in the current logical display orientation.
// pressed: Current contact state.
// pressed_edge: Whether this is the first pressed sample.
// rotation: Current display-owner orientation.
// now_us: Monotonic release timestamp.
// result: Receives the resulting edit or explicit submit.
// consumed: True for every sample belonging to a keypad gesture.
// Exception: Preserves the key handler's input or clock error.
esp_err_t handle_text_entry_touch(TextEntryTouch &touch, TextEntry &entry, CODY2D::Point point,
    bool pressed, bool pressed_edge, uint16_t rotation, int64_t now_us,
    TextEntryResult &result, bool &consumed)
{
    result = TextEntryResult::None;
    consumed = false;
    if (pressed_edge) {
        touch = {};
        const int key = text_entry_key_at(point);
        if (!pressed || key < 0) return ESP_OK;
        touch.start = point;
        touch.last = point;
        touch.key = static_cast<int8_t>(key);
        touch.rotation = rotation;
        touch.active = true;
    }
    if (!touch.active) return ESP_OK;
    consumed = true;
    if (touch.rotation != rotation) { touch = {}; return ESP_OK; }
    if (pressed) {
        touch.last = point;
        const int dx = point.x - touch.start.x;
        const int dy = point.y - touch.start.y;
        if (dx * dx + dy * dy > 12 * 12 || text_entry_key_at(point) != touch.key) touch.moved = true;
        return ESP_OK;
    }
    const int key = touch.key;
    const bool admitted = !touch.moved && text_entry_key_at(touch.last) == key;
    touch = {};
    return admitted ? activate_text_entry_key(entry, static_cast<uint8_t>(key), now_us, result) : ESP_OK;
}

// entry: Active bounded text draft.
// key: Released key index, from zero through fourteen.
// now_us: Monotonic touch-release timestamp.
// result: Receives the visible change or explicit submission.
// Exception: Rejects invalid draft, index or nonmonotonic timestamp.
esp_err_t activate_text_entry_key(TextEntry &entry, uint8_t key, int64_t now_us, TextEntryResult &result)
{
    result = TextEntryResult::None;
    if (key >= 15 || entry.limit == 0 || entry.limit > kTextEntryCapacity ||
        entry.length > entry.limit || now_us < 0 || now_us < entry.last_tap_us) return ESP_ERR_INVALID_ARG;
    if (key == 13 && !entry.secret) return ESP_OK;
    if (key < 9) {
        const char *group = group_for(entry, key);
        const auto count = std::strlen(group);
        const bool cycle = count > 1 && entry.pending_key == static_cast<int8_t>(key) && entry.length > 0 &&
            now_us - entry.last_tap_us <= kMultiTapUs;
        if (!cycle && entry.length == entry.limit) {
            commit_pending(entry);
            return ESP_OK;
        }
        entry.pending_index = cycle ? static_cast<uint8_t>((entry.pending_index + 1) % count) : 0;
        char character = group[entry.pending_index];
        if (entry.uppercase && character >= 'a' && character <= 'z') character -= 'a' - 'A';
        if (!cycle) ++entry.length;
        entry.text[entry.length - 1] = character;
        entry.text[entry.length] = '\0';
        if (count > 1) entry.pending_key = static_cast<int8_t>(key);
        else commit_pending(entry);
    } else {
        commit_pending(entry);
        switch (key) {
        case 9:
            if (entry.mode == TextEntryMode::Letters) entry.uppercase = !entry.uppercase;
            else entry.mode = entry.mode == TextEntryMode::Numbers ? TextEntryMode::Symbols : TextEntryMode::Numbers;
            break;
        case 10:
            if (entry.length < entry.limit) { entry.text[entry.length++] = ' '; entry.text[entry.length] = '\0'; }
            break;
        case 11:
            if (entry.length > 0) entry.text[--entry.length] = '\0';
            break;
        case 12:
            entry.mode = entry.mode == TextEntryMode::Letters ? TextEntryMode::Numbers : TextEntryMode::Letters;
            break;
        case 13:
            if (entry.secret) entry.reveal = !entry.reveal;
            break;
        case 14:
            result = TextEntryResult::Submit;
            break;
        default: break;
        }
    }
    entry.last_tap_us = now_us;
    if (result == TextEntryResult::None) result = TextEntryResult::Changed;
    return ESP_OK;
}

// view: Retired view, including any temporarily revealed text.
void clear_text_entry_view(TextEntryView &view)
{
    volatile char *bytes = view.visible_text;
    for (size_t i = 0; i < sizeof(view.visible_text); ++i) bytes[i] = 0;
    view = {};
}

// entry: Active bounded draft, copied only into the retained field presentation.
// title: Caller-owned ASCII heading retained through frame retirement.
// placeholder: Optional caller-owned hint retained through frame retirement.
// font: Caller-owned native font for all captions.
// view: Persistent frame storage; previous frame must have retired.
// content: Receives the renderer and its display damage envelope.
// submit_label: Caller-owned caption for the submission key.
// subtitle: Optional caller-owned instruction retained through frame retirement.
// Exception: Returns original native layout errors or invalid state.
esp_err_t prepare_text_entry_view(const TextEntry &entry, const char *title, const char *placeholder,
    const CODY2D::MonoFont &font, TextEntryView &view, SceneContent &content, const char *submit_label,
    const char *subtitle)
{
    content = {};
    if (title == nullptr || entry.limit == 0 || entry.limit > kTextEntryCapacity || entry.length > entry.limit)
        return ESP_ERR_INVALID_ARG;
    for (uint8_t i = 0; i < entry.length; ++i) {
        const auto byte = static_cast<unsigned char>(entry.text[i]);
        if (byte < 32 || byte > 126) return ESP_ERR_INVALID_ARG;
    }
    if (entry.text[entry.length] != '\0') return ESP_ERR_INVALID_ARG;
    const uint32_t revision = view.revision + 1;
    clear_text_entry_view(view);
    // A revealed secret may be measured/drawn only after every possible byte has
    // been checked independently, including the font's per-glyph metrics.
    esp_err_t error = entry.secret && entry.reveal && entry.length != 0 ? validate_secret_font(font) : ESP_OK;
    StyledTitle heading{};
    const size_t title_length = std::strlen(title);
    if (error == ESP_OK && (title_length == 0 || title_length > kTextEntryCapacity)) error = ESP_ERR_INVALID_SIZE;
    if (error == ESP_OK) error = prepare_styled_title({title, static_cast<uint16_t>(title_length)},
        {104, 73, 258, 25}, heading, 3, true);
    if (error == ESP_OK && subtitle != nullptr && subtitle[0] != '\0')
        error = centered_line(font, subtitle, {83, 98, 300, 25}, view.subtitle);
    view.field.bounds = {95, 125, 276, 31};
    view.field.top_color = Theme::text_entry_field_top;
    view.field.bottom_color = Theme::surface_pairing;
    view.field.border_color = Theme::controls_content_primary_border;
    view.field.corner_radius = 14;
    for (uint8_t i = 0; i < entry.length; ++i)
        view.visible_text[i] = entry.secret && !entry.reveal ? '*' : entry.text[i];
    const char *field_text = entry.length ? view.visible_text : (placeholder != nullptr ? placeholder : "");
    // Show the tail of long input while keeping complete glyphs inside the field.
    uint32_t width = 0;
    while (error == ESP_OK) {
        error = CODY2D::measure_text_width(font, {field_text, static_cast<uint16_t>(std::strlen(field_text))}, width);
        if (error != ESP_OK || width <= 250 || *field_text == '\0') break;
        ++field_text;
    }
    if (error == ESP_OK && *field_text != '\0') error = centered_line(font, field_text, view.field.bounds, view.field.caption);
    for (uint8_t i = 0; error == ESP_OK && i < 15; ++i) {
        const char *label = i < 9 ? group_for(entry, i) :
            i == 9 ? (entry.mode == TextEntryMode::Letters ? "Shift" :
                entry.mode == TextEntryMode::Numbers ? "#+=" : "123") :
            i == 10 ? "Space" : i == 11 ? "Del" : i == 12 ?
            (entry.mode == TextEntryMode::Letters ? "123" : "ABC") : i == 13 ?
            (entry.secret ? (entry.reveal ? "Hide" : "Show") : " ") : submit_label;
        if (label == nullptr || std::strlen(label) >= view.labels[i].size()) {
            clear_text_entry_view(view);
            return ESP_ERR_INVALID_SIZE;
        }
        auto &label_storage = view.labels[i];
        std::strncpy(label_storage.data(), label, label_storage.size() - 1);
        if (entry.mode == TextEntryMode::Letters && entry.uppercase && i < 9) {
            for (char &c : label_storage) if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        }
        auto &control = view.keys[i];
        control.bounds = key_bounds(i);
        control.corner_radius = 12;
        control.top_color = i == 14 ? Theme::controls_content_primary_top : Theme::controls_content_secondary_top;
        control.bottom_color = i == 14 ? Theme::controls_content_primary_bottom : Theme::controls_content_secondary_bottom;
        control.border_color = i == 14 ? Theme::controls_content_primary_border : Theme::controls_content_secondary_border;
        if (i == 9 && entry.mode == TextEntryMode::Letters && entry.uppercase) {
            control.top_color = Theme::controls_content_primary_top;
            control.bottom_color = Theme::controls_content_primary_bottom;
            control.border_color = Theme::controls_content_primary_border;
        }
        control.enabled = i != 13 || entry.secret;
        error = centered_line(font, label_storage.data(), control.bounds, control.caption);
    }
    if (error != ESP_OK) { clear_text_entry_view(view); return error; }
    view.revision = revision;
    view.valid = true;
    content = {};
    content.bounds = {73, 73, 393, 383};
    content.state = &view;
    content.revision = revision;
    content.draw_rows = draw_rows;
    content.heading = heading;
    return ESP_OK;
}

} // namespace CODY

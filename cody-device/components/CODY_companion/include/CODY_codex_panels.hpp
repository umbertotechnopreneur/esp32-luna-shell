#pragma once

#include <array>
#include "CODY2D_controls.hpp"
#include "CODY_chat_title_view.hpp"
#include "CODY_scene.hpp"
#include "LUNASHELL_device_snapshot.hpp"

namespace CODY {

enum class CodexPanelKind : uint8_t { Events, CurrentTurn, Reply };
enum class TurnGlyph : uint8_t { None, Shell, Web, Editing, Tool, Compacting };

struct CodexPanelView {
    LUNASHELL::CodexInboxPage page{};
    std::array<CODY2D::TextLine, 12> lines{};
    std::array<CODY2D::Rect, 12> line_boxes{};
    CODY2D::CapsuleControl reply_action{};
    ChatTitleView chat_title{};
    char subtitle[64]{};
    char detail[193]{};
    char elapsed[12]{};
    int64_t marquee_started_us = 0;
    uint32_t revision = 0;
    uint16_t marquee_offset = 0;
    uint8_t line_count = 0;
    CodexPanelKind kind = CodexPanelKind::Events;
    TurnGlyph glyph = TurnGlyph::None;
    bool inbox_current = false;
    bool valid = false;
};

// view: Persistent panel and last prepared page, borrowed through frame retirement.
// inbox: Canonical page from the current paired association.
// now_us: Monotonic clock used by the focused preview only.
// content: Receives the display callback and its bounded damage.
// Exception: Returns the original native text or geometry error.
esp_err_t prepare_codex_events(CodexPanelView &view, const LUNASHELL::CodexInboxSnapshot &inbox,
    int64_t now_us, SceneContent &content);

// observation: Authenticated activity for the currently followed chat.
// now_us: Monotonic time for the device's link lease.
bool current_turn_has_data(const LUNASHELL::CodexTechnicalObservation &observation, int64_t now_us);

// view: Persistent panel; text belongs to the display worker.
// observation: Coherent canonical activity and technical snapshots.
// now_us: Monotonic clock for title freshness and real elapsed compaction.
// content: Receives the display callback and its bounded damage.
// Exception: Returns the original native text or geometry error.
esp_err_t prepare_current_turn(CodexPanelView &view, const LUNASHELL::CodexTechnicalObservation &observation,
    int64_t now_us, SceneContent &content);

// view: Retains the prepared reply labels.
// title: Bounded title from the admitted Events row.
// pending: True after the user requests dismissal and before its authenticated confirmation.
// content: Receives immutable reply detail until the frame retires.
// Exception: Returns the original native text or geometry error.
esp_err_t prepare_codex_reply(CodexPanelView &view, const char *title, bool pending, SceneContent &content);

// point: Logical touch location on a retired Events screen.
int codex_event_row_at(CODY2D::Point point);

} // namespace CODY

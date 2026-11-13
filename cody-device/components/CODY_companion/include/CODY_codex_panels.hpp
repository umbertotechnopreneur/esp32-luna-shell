#pragma once

#include <array>
#include "CODY2D_controls.hpp"
#include "CODY_chat_title_view.hpp"
#include "CODY_notification_view.hpp"
#include "CODY_scene.hpp"
#include "LUNASHELL_device_snapshot.hpp"

namespace CODY {

enum class CodexPanelKind : uint8_t { Events = 0, CurrentTurn = 1 };
enum class TurnGlyph : uint8_t { None, Shell, Web, Editing, Tool, Compacting };

struct CodexPanelView {
    NotificationListPage page{};
    // Header, subtitle, remote-status footer and two captions per admitted row.
    std::array<CODY2D::TextLine, 3 + kNotificationPageSize * 2> lines{};
    std::array<CODY2D::Rect, 3 + kNotificationPageSize * 2> line_boxes{};
    ChatTitleView chat_title{};
    char notification_labels[kNotificationPageSize][64]{};
    char notification_titles[kNotificationPageSize][64]{};
    char activity_label[64]{};
    char subtitle[64]{};
    char notification_status[64]{};
    char detail[193]{};
    char status[32]{};
    char elapsed[12]{};
    uint32_t revision = 0;
    uint8_t line_count = 0;
    CodexPanelKind kind = CodexPanelKind::Events;
    TurnGlyph glyph = TurnGlyph::None;
    bool inbox_current = false;
    bool inbox_retained = false;
    bool valid = false;
};

// view: Persistent panel and last prepared page, borrowed through frame retirement.
// inbox: Coherent merged projection of local and authenticated remote records.
// now_us: Display clock; compact rows do not animate their captions.
// content: Receives the display callback and its bounded damage.
// Exception: Returns invalid bounds or the original native text/geometry error.
esp_err_t prepare_codex_events(CodexPanelView &view, const NotificationListPage &inbox,
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

// point: Logical touch location on a retired Events screen.
int codex_event_row_at(CODY2D::Point point);

} // namespace CODY

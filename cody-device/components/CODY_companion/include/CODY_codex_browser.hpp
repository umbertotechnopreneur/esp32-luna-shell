/* Copyright (c) 2026 Umberto Giacobbi */
#pragma once

#include <array>

#include "CODY2D_controls.hpp"
#include "CODY_scene.hpp"
#include "CODY_state_query.hpp"

namespace CODY {

// Stable catalog identities in the next-v1 namespace, distinct from SceneId.
enum class CodexBrowserRoute : uint8_t {
    Closed = 0, Hub = 101, Chats = 102, Turns = 103, Calls = 104, ActiveCalls = 105, PendingQuestions = 106,
};
enum class CodexBrowserAction : uint8_t {
    None, Chats, ActiveCalls, Events, Statistics, Resume, PreviousPage, NextPage, Chat, Turn, PendingQuestions,
};
enum class CodexBrowserDestination : uint8_t { None, Statistics, Events };

struct CodexBrowserControl {
    CodexBrowserAction action = CodexBrowserAction::None;
    uint64_t id = 0;

    // other: Control observed at the other end of the touch gesture.
    bool operator==(CodexBrowserControl other) const { return action == other.action && id == other.id; }
};

struct CodexBrowserRow {
    std::array<char, 129> label{};
    std::array<char, 64> detail{};
    CodexBrowserControl control{};
    CODY2D::ActionIcon icon = CODY2D::ActionIcon::None;
    uint16_t color = 0;
};

struct CodexBrowserPage {
    std::array<CodexBrowserRow, 3> rows{};
    std::array<char, 72> note{};
    Domain::SourceId source{};
    Domain::ChatId resume{};
    Domain::StateCursor next{};
    uint64_t generation = 0;
    uint8_t count = 0;
    uint8_t page_index = 0;
    uint8_t known_page_count = 0;
    bool previous = false;
};

// Stores only stable paging keys, never a second cache of domain rows. The
// largest list is bounded by 256 active plus 128 terminal calls, three per page.
struct CodexBrowserTrail {
    Domain::StateCursor cursor{};
    std::array<uint64_t, 127> previous{};
    uint64_t known_generation = 0;
    uint8_t count = 0;
    uint8_t known_page_count = 0;

    // No parameters: reset a stale generation or a new scope without an array copy.
    void reset() { cursor = {}; count = 0; known_generation = 0; known_page_count = 0; }
};

struct CodexBrowserLine {
    std::array<char, 129> text{};
    CODY2D::TextLine line{};
};

// Display-worker storage. All page/query scratch stays off the worker stack.
struct CodexBrowser {
    CodexBrowserRoute route = CodexBrowserRoute::Closed;
    CodexBrowserRoute turns_parent = CodexBrowserRoute::Chats;
    Domain::ChatId chat{};
    Domain::ChatId saving_bookmark{};
    Domain::TurnId turn{};
    CodexBrowserTrail chats{};
    CodexBrowserTrail turns{};
    CodexBrowserTrail records{}; // Calls or questions, reset on entry to either list.
    Domain::StateQueryResult observation{};
    CodexBrowserPage page{};
    CodexBrowserPage draft{};
    std::array<CodexBrowserLine, 8> lines{};
    CODY2D::TextLine paging_hint{};
    std::array<CODY2D::CapsuleControl, 6> controls{};
    std::array<CodexBrowserControl, 6> actions{};
    esp_err_t selection_error = ESP_OK;
    int64_t refresh_at_us = 0;
    uint32_t revision = 0;
    uint8_t line_count = 0;
    CodexBrowserRoute drawn_route = CodexBrowserRoute::Closed;
};

// browser: Display-owned route and projection.
void open_codex_browser(CodexBrowser &browser);

// browser: Display-owned route and cursor state. Only the current child closes.
void back_codex_browser(CodexBrowser &browser);

// browser: Display-owned query scratch and immutable frame content.
// now_us: Current receiver clock for refresh cadence.
// content: Frame output borrowed only until strip retirement.
// Exception: Returns a native rendering preparation error. Query failures render explicitly.
esp_err_t prepare_codex_browser(CodexBrowser &browser, int64_t now_us, SceneContent &content);

// browser: Projection belonging to the last fully retired frame.
// point: Logical touch location.
CodexBrowserControl codex_browser_control_at(const CodexBrowser &browser, CODY2D::Point point);

// browser: Display-owned route and pinned identities.
// control: Matched retired control, checked against the live query before navigation.
// Exception: Optional selection errors are displayed; a legacy destination is returned to its owner.
CodexBrowserDestination activate_codex_browser(CodexBrowser &browser, CodexBrowserControl control);

} // namespace CODY

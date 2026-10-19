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

#include "CODY_codex_event_presentation.hpp"
#include "CODY_approval_notice.hpp"
#include <vector>
#include "scene_data.hpp"
#include <cassert>
#include <cstring>

namespace Presentation = CODY::event_presentation;

// No parameters: regression scenarios use the actual shared firmware presentation model.
// Exception: Assertion failure identifies a broken semantic action, geometry or delivery contract.
void verify_shared_event_presentation()
{
    LUNASHELL::CodexEventSnapshot event{};
    event.kind = LUNASHELL::CodexEventKind::Question;
    event.state = LUNASHELL::CodexEventState::Pending;
    event.last_error = ESP_OK;
    std::strcpy(event.title, "Choose build mode");
    std::strcpy(event.body, "Review the request before choosing.");
    event.choice_count = 3;
    std::strcpy(event.choices[0].id, "debug");
    std::strcpy(event.choices[0].label, "Debug");
    std::strcpy(event.choices[0].detail, "Compile with diagnostics.");
    std::strcpy(event.choices[1].id, "release");
    std::strcpy(event.choices[1].label, "Release");
    event.choices[1].tone = LUNASHELL::CodexChoiceTone::Negative;
    std::strcpy(event.choices[2].id, "later");
    std::strcpy(event.choices[2].label, "Later");
    event.choices[2].tone = LUNASHELL::CodexChoiceTone::Info;
    Presentation::Navigation navigation{};
    Presentation::Context context{};
    context.online = true;
    Presentation::Plan plan{};
    assert(Presentation::prepare(event, navigation, context, 27, plan) == ESP_OK);
    assert(plan.route == Presentation::Route::DecisionNotice26 && !plan.back);
    assert(plan.action_count == 1 && plan.actions[0].id == Presentation::ActionId::Open);
    assert(!Presentation::hit_action(plan, false, 233, 390));
    assert(Presentation::hit_action(plan, true, 233, 390));
    assert(!Presentation::hit_action(plan, true, 113, 352));
    Presentation::Effect effect;
    assert(Presentation::activate(event, context, plan.actions[0], navigation, effect) == ESP_OK);
    assert(effect == Presentation::Effect::Render && navigation.stage == Presentation::EventStage::Choices);
    assert(Presentation::prepare(event, navigation, context, 27, plan) == ESP_OK);
    assert(plan.back && plan.action_count == 2 && plan.page_count == 2);
    assert(plan.actions[0].row && plan.actions[1].row);
    assert(plan.actions[0].bounds.width == 320 && plan.actions[1].bounds.y == 328);
    CODY2D::CapsuleControl control;
    CODY2D::TextLine second;
    assert(Presentation::prepare_control(plan.actions[1], CODY::SceneData::body_font, control, second) == ESP_OK);
    assert(control.icon == CODY2D::ActionIcon::None && !second.valid);
    assert(control.top_color == plan.actions[1].colors[0]);
    const Presentation::Action *released = nullptr;
    assert(Presentation::release(plan, navigation, true, 230, 280, 240, 290, released) == Presentation::Effect::None);
    assert(released == nullptr && navigation.choice_page == 0);
    assert(Presentation::release(plan, navigation, false, 230, 280, 230, 220, released) == Presentation::Effect::None);
    assert(Presentation::release(plan, navigation, true, 230, 280, 230, 220, released) == Presentation::Effect::Render);
    assert(released == nullptr && navigation.choice_page == 1);
    assert(Presentation::prepare(event, navigation, context, 27, plan) == ESP_OK);
    assert(plan.action_count == 1 && std::strcmp(plan.actions[0].choice_id, "later") == 0);
    assert(std::strcmp(plan.actions[0].label, "3. Later") == 0);
    assert(Presentation::scroll(plan, navigation, true, 0, 50));
    assert(Presentation::prepare(event, navigation, context, 27, plan) == ESP_OK);
    assert(!Presentation::hit_back(plan, false, 40, 233));
    assert(Presentation::hit_back(plan, true, 40, 233));
    assert(!Presentation::hit_back(plan, true, 10, 203));
    assert(Presentation::activate(event, context, plan.actions[1], navigation, effect) == ESP_OK);
    assert(std::strcmp(navigation.selected_id, "release") == 0);
    assert(navigation.stage == Presentation::EventStage::Confirm && effect == Presentation::Effect::Render);
    context.online = false;
    assert(Presentation::prepare(event, navigation, context, 27, plan) == ESP_OK);
    assert(plan.actions[0].id == Presentation::ActionId::Send && !plan.actions[0].enabled);
    assert(Presentation::prepare_control(plan.actions[0], CODY::SceneData::body_font, control, second) == ESP_OK);
    assert(plan.actions[0].tone == LUNASHELL::CodexChoiceTone::Negative);
    assert(control.icon == CODY2D::ActionIcon::None && control.top_color == 0x28A4);
    assert(!control.enabled && control.bounds.y == 352);
    assert(std::strcmp(plan.actions[0].label, "Send reply") == 0);
    assert(Presentation::activate(event, context, plan.actions[0], navigation, effect) == ESP_ERR_INVALID_STATE);
    context.online = true;
    assert(Presentation::prepare(event, navigation, context, 27, plan) == ESP_OK);
    assert(Presentation::activate(event, context, plan.actions[0], navigation, effect) == ESP_OK);
    assert(effect == Presentation::Effect::Reply);
    // An accepted local send does not claim the recipient acknowledged delivery.
    event.state = LUNASHELL::CodexEventState::Received;
    assert(Presentation::prepare(event, navigation, context, 27, plan) == ESP_OK);
    assert(plan.route == Presentation::Route::DecisionWaiting && plan.action_count == 0);
    event.state = LUNASHELL::CodexEventState::Expired;
    std::strcpy(event.selected_id, "release");
    assert(Presentation::prepare(event, navigation, context, 27, plan) == ESP_OK);
    assert(std::strcmp(plan.title, "Reply unconfirmed") == 0);
    assert(plan.actions[0].id == Presentation::ActionId::Dismiss);
    event.state = LUNASHELL::CodexEventState::Delivered;
    assert(Presentation::prepare(event, navigation, context, 27, plan) == ESP_OK);
    assert(plan.route == Presentation::Route::ReplySent31);
    const size_t title_length = 10;
    size_t narrative_length = title_length;
    for (uint8_t index = 0; index < plan.line_count; ++index) {
        if (!plan.lines[index].instant) { narrative_length += std::strlen(plan.lines[index].text); }
    }
    assert(!Presentation::controls_ready(plan, narrative_length - 1, title_length));
    assert(Presentation::controls_ready(plan, narrative_length, title_length));
    // Stable identity lookup rejects duplicates rather than guessing a card.
    std::strcpy(event.choices[2].id, "release");
    assert(Presentation::find_choice(event, "release") == nullptr);

    event = {};
    event.kind = LUNASHELL::CodexEventKind::ChecksFailed;
    event.state = LUNASHELL::CodexEventState::Pending;
    event.last_error = ESP_OK;
    event.failure_count = 6;
    navigation = {};
    navigation.stage = Presentation::EventStage::Failures;
    assert(Presentation::prepare(event, navigation, context, 27, plan) == ESP_OK);
    assert(!Presentation::scroll(plan, navigation, false, 0, -50));
    assert(!Presentation::scroll(plan, navigation, true, 45, -50));
    assert(Presentation::scroll(plan, navigation, true, 0, -50));
    assert(navigation.failure_page == 1);
    assert(Presentation::prepare(event, navigation, context, 27, plan) == ESP_OK);
    assert(!Presentation::scroll(plan, navigation, true, 0, -50));
    assert(Presentation::scroll(plan, navigation, true, 0, 50));
    assert(navigation.failure_page == 0);
    int64_t readable_at = -1;
    event.kind = LUNASHELL::CodexEventKind::ChecksPassed;
    assert(!Presentation::checks_passed_dismiss_due(event, context, false, 100, readable_at));
    assert(readable_at == -1);
    assert(!Presentation::checks_passed_dismiss_due(event, context, true, 100, readable_at));
    assert(!Presentation::checks_passed_dismiss_due(event, context, true, 3000099, readable_at));
    assert(Presentation::checks_passed_dismiss_due(event, context, true, 3000100, readable_at));
    context.ui_error = ESP_FAIL;
    assert(!Presentation::checks_passed_dismiss_due(event, context, true, 4000100, readable_at));

    // A typed ready response is neither a question answer nor evidence of passed checks.
    event = {};
    event.kind = LUNASHELL::CodexEventKind::ReplyReady;
    event.state = LUNASHELL::CodexEventState::Pending;
    event.last_error = ESP_OK;
    std::strcpy(event.title, "Reply ready");
    std::strcpy(event.body, "A different SIMULATED chat");
    std::strcpy(event.context, "Cody project");
    context = {};
    context.online = true;
    navigation = {};
    assert(Presentation::prepare(event, navigation, context, 27, plan) == ESP_OK);
    assert(plan.route == Presentation::Route::CodexNotice && !plan.back);
    assert(std::strcmp(plan.title, "Reply ready") == 0 && plan.title_color == 0x5FF9);
    assert(plan.line_count == 2 && std::strcmp(plan.lines[0].text, event.body) == 0);
    assert(plan.lines[0].top == 214 && plan.lines[1].top == 312 && plan.lines[1].instant);
    assert(plan.action_count == 1 && std::strcmp(plan.actions[0].label, "Got it") == 0);
    assert(plan.actions[0].id == Presentation::ActionId::Dismiss &&
        plan.actions[0].icon == CODY2D::ActionIcon::None);
    assert(Presentation::activate(event, context, plan.actions[0], navigation, effect) == ESP_OK &&
        effect == Presentation::Effect::Dismiss);
    readable_at = -1;
    assert(!Presentation::checks_passed_dismiss_due(event, context, true, 9000000, readable_at));
    assert(readable_at == -1);
    context.dismiss_admitted = true;
    assert(Presentation::prepare(event, navigation, context, 27, plan) == ESP_OK && !plan.actions[0].enabled);
    assert(Presentation::activate(event, context, plan.actions[0], navigation, effect) == ESP_ERR_INVALID_STATE);
    context.dismiss_admitted = false;
    event.state = LUNASHELL::CodexEventState::Expired;
    assert(Presentation::prepare(event, navigation, context, 27, plan) == ESP_OK &&
        std::strcmp(plan.title, "Request expired") == 0 && plan.title_color == 0xFFFF);
    event.kind = LUNASHELL::CodexEventKind::Notice;
    event.state = LUNASHELL::CodexEventState::Pending;
    event.context[0] = '\0';
    std::strcpy(event.body, event.title);
    assert(Presentation::prepare(event, navigation, context, 27, plan) == ESP_OK && plan.title[0] == '\0');
    assert(plan.line_count == 1 && std::strcmp(plan.lines[0].text, "Reply ready") == 0);
    // A full native title has its own bounded viewport and never becomes a permission control.
    event = {};
    event.kind = LUNASHELL::CodexEventKind::ApprovalOnPc;
    event.state = LUNASHELL::CodexEventState::Pending;
    event.last_error = ESP_OK;
    std::memset(event.body, 'T', 255);
    event.body[255] = '\0';
    context = {};
    context.online = true;
    navigation = {};
    assert(Presentation::prepare(event, navigation, context, 27, plan) == ESP_OK);
    assert(plan.route == Presentation::Route::ApprovalOnPc && plan.back && plan.action_count == 0);
    assert(std::strcmp(plan.title, "Approval needed") == 0 && plan.title_color == 0xFFFF);
    assert(plan.line_count == 2 && std::strcmp(plan.lines[0].text, "On your PC:") == 0);
    assert(plan.lines[0].pc_heading && Presentation::line_color(plan.lines[0], 0) == 0xFE07);
    assert(std::strcmp(plan.lines[1].text, "Continue on your PC") == 0 && plan.lines[1].top == 393);
    Presentation::Action approval_back;
    approval_back.id = Presentation::ActionId::Back;
    approval_back.enabled = true;
    assert(Presentation::activate(event, context, approval_back, navigation, effect) == ESP_OK);
    assert(effect == Presentation::Effect::Dismiss && !context.reply_admitted);
    CODY::ApprovalNoticeState approval;
    const auto &font = CODY::SceneData::body_font;
    assert(CODY::prepare_approval_notice(approval, font, event.body) == ESP_OK);
    assert(approval.length == 255 && std::strcmp(approval.chat, event.body) == 0);
    bool moved = false;
    assert(CODY::advance_approval_notice(approval, font, 0, true, moved) == ESP_OK);
    assert(CODY::advance_approval_notice(approval, font, 1500000, true, moved) == ESP_OK && moved);
    const uint16_t offset = approval.offset;
    assert(CODY::prepare_approval_notice(approval, font, event.body) == ESP_OK && approval.offset == offset);
    std::vector<uint16_t> pixels(466U * 466U, 0x1234);
    CODY2D::Surface surface{pixels.data(), 466, 466, 466, static_cast<uint32_t>(pixels.size())};
    assert(CODY::draw_approval_notice_rows(approval, surface, 0, 0) == ESP_OK);
    for (uint16_t y = 203; y < 263; ++y) {
        for (uint16_t x = 10; x < 70; ++x) { assert(pixels[y * 466U + x] == 0x1234); }
    }
    context.online = false;
    assert(Presentation::prepare(event, navigation, context, 27, plan) == ESP_OK);
    assert(std::strcmp(plan.lines[1].text, "Offline - check your PC") == 0);
    context.ui_error = ESP_ERR_INVALID_STATE;
    assert(Presentation::prepare(event, navigation, context, 27, plan) == ESP_OK);
    assert(std::strstr(plan.lines[1].text, "Error: 0x") == plan.lines[1].text);

}

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
    assert(plan.back && plan.action_count == 3);
    assert(plan.actions[0].card && plan.actions[1].card && !plan.actions[2].card);
    assert(plan.actions[2].bounds.y == 376 && plan.actions[2].bounds.height == 52);
    CODY2D::CapsuleControl control;
    CODY2D::TextLine second;
    assert(Presentation::prepare_control(plan.actions[2], CODY::SceneData::body_font, control, second) == ESP_OK);
    assert(control.icon == CODY2D::ActionIcon::Link && control.top_color == plan.actions[2].colors[0]);
    // The 25-pixel caption rounds its shared icon center one pixel above the even capsule center.
    assert(control.icon_center.y == 401);
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
    assert(control.icon == CODY2D::ActionIcon::Link && control.top_color == 0x28A4);
    assert(!control.enabled && control.bounds.y == 352 && control.icon_center.y == 387);
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
    assert(!Presentation::scroll(event, navigation, false, 0, -50));
    assert(!Presentation::scroll(event, navigation, true, 45, -50));
    assert(Presentation::scroll(event, navigation, true, 0, -50));
    assert(navigation.failure_page == 1);
    assert(!Presentation::scroll(event, navigation, true, 0, -50));
    assert(Presentation::scroll(event, navigation, true, 0, 50));
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
}

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

// SIMULATED input boundary. All presentation policy runs in the actual Cody model.
#include "CODY_codex_event_presentation.hpp"
#include "scene_data.hpp"
#include "esp_log.h"
#include <cstring>

#if defined(_WIN32)
#define CODY_HOST_EXPORT extern "C" __declspec(dllexport)
#else
#define CODY_HOST_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {
namespace Presentation = CODY::event_presentation;
struct Input {
    uint8_t kind;
    char state[12];
    char context[25];
    char title[25];
    char body[64];
    uint8_t choice_count;
    LUNASHELL::CodexEventChoice choices[3];
    char selected_id[13];
    uint8_t failure_count;
    char failures[6][49];
    bool progress_valid;
    uint16_t passed;
    uint16_t total;
    int32_t last_error;
    char request_id[33];
    uint8_t pending_question_count;
    uint8_t pending_entry_count;
    LUNASHELL::CodexPendingQuestion pending_questions[2];
};

// operation: Host operation whose actual firmware error must remain visible.
// error: Exact shared-model failure.
esp_err_t result(const char *operation, esp_err_t error)
{
    if (error != ESP_OK) { ESP_LOGE("CodyEventHost", "%s: %s (0x%x)", operation, esp_err_to_name(error), error); }
    return error;
}

// text: Fixed-size ABI field, rejected before any unbounded library access.
// Exception: Returns false for absent termination or non-printable ASCII.
template<size_t Size> bool valid_text(const char (&text)[Size])
{
    for (size_t index = 0; index < Size; ++index) {
        if (text[index] == '\0') { return true; }
        if (text[index] < 32 || text[index] > 126) { return false; }
    }
    return false;
}

// input: Explicit PC synthetic projection, never written into DeviceSnapshotService.
// event: Owner scratch populated only after validating the entire boundary.
// Exception: Returns INVALID_ARG for malformed identities, ASCII, states or counters.
esp_err_t project(const Input &input, LUNASHELL::CodexEventSnapshot &event)
{
    if (input.kind < 1 || input.kind > static_cast<uint8_t>(LUNASHELL::CodexEventKind::ReplyReady) ||
        input.choice_count > 3 || input.failure_count > 6 ||
        !valid_text(input.state) || !valid_text(input.context) || !valid_text(input.title) ||
        !valid_text(input.body) || !valid_text(input.selected_id) || !valid_text(input.request_id) ||
        input.pending_question_count > 128 || input.pending_entry_count > 2 ||
        input.pending_entry_count > input.pending_question_count) {
        return ESP_ERR_INVALID_ARG;
    }
    event = {};
    bool state_valid = false;
    for (uint8_t value = 0; value <= static_cast<uint8_t>(LUNASHELL::CodexEventState::Rejected); ++value) {
        const auto state = static_cast<LUNASHELL::CodexEventState>(value);
        if (std::strcmp(input.state, LUNASHELL::codexEventStateToken(state)) == 0) {
            event.state = state;
            state_valid = true;
            break;
        }
    }
    if (!state_valid) { return ESP_ERR_INVALID_ARG; }
    for (uint8_t index = 0; index < input.choice_count; ++index) {
        const auto &choice = input.choices[index];
        if (!valid_text(choice.id) || !valid_text(choice.label) || !valid_text(choice.detail) ||
            !LUNASHELL::validCodexChoiceId(choice.id) ||
            static_cast<uint8_t>(choice.tone) > static_cast<uint8_t>(LUNASHELL::CodexChoiceTone::Info)) { return ESP_ERR_INVALID_ARG; }
        for (uint8_t preceding = 0; preceding < index; ++preceding) {
            if (std::strcmp(choice.id, input.choices[preceding].id) == 0) { return ESP_ERR_INVALID_ARG; }
        }
    }
    for (uint8_t index = 0; index < input.failure_count; ++index) {
        if (!valid_text(input.failures[index])) { return ESP_ERR_INVALID_ARG; }
    }
    for (uint8_t index = 0; index < input.pending_entry_count; ++index) {
        const auto &entry = input.pending_questions[index];
        if (!valid_text(entry.request_id) || !valid_text(entry.context) || !valid_text(entry.title)) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    event.kind = static_cast<LUNASHELL::CodexEventKind>(input.kind);
    std::memcpy(event.context, input.context, sizeof(event.context));
    std::memcpy(event.title, input.title, sizeof(event.title));
    std::memcpy(event.body, input.body, sizeof(event.body));
    event.choice_count = input.choice_count;
    std::memcpy(event.choices, input.choices, sizeof(event.choices));
    std::memcpy(event.selected_id, input.selected_id, sizeof(event.selected_id));
    event.failure_count = input.failure_count;
    std::memcpy(event.failed_checks, input.failures, sizeof(event.failed_checks));
    event.progress_valid = input.progress_valid;
    event.passed = input.passed;
    event.total = input.total;
    event.last_error = input.last_error;
    std::memcpy(event.request_id, input.request_id, sizeof(event.request_id));
    event.pending_question_count = input.pending_question_count;
    event.pending_entry_count = input.pending_entry_count;
    std::memcpy(event.pending_questions, input.pending_questions, sizeof(event.pending_questions));
    return ESP_OK;
}
struct Owner {
    LUNASHELL::CodexEventSnapshot event{};
    Presentation::Plan plan{};
};
// This storage belongs only to the current host UI thread; no embedded task stack grows.
thread_local Owner s_owner;
}

// input: Bounded SIMULATED event ABI data.
// navigation: Local reversible choices; never a transport acknowledgement.
// context: Actual host freshness/errors/admissions.
// plan: Receives the same fully wrapped route/text/action plan used by firmware.
// Exception: Returns original projection or production presentation failure.
CODY_HOST_EXPORT int cody_host_event_prepare(const Input *input, const Presentation::Navigation *navigation,
    const Presentation::Context *context, Presentation::Plan *plan)
{
    if (!input || !navigation || !context || !plan || !valid_text(navigation->selected_id) ||
        static_cast<uint8_t>(navigation->stage) > static_cast<uint8_t>(Presentation::EventStage::Questions)) {
        return result("prepare", ESP_ERR_INVALID_ARG);
    }
    esp_err_t error = project(*input, s_owner.event);
    const auto &font = CODY::SceneData::body_font;
    const size_t capacity = (300U - font.width) / font.advance + 1U;
    if (error == ESP_OK) { error = Presentation::prepare(s_owner.event, *navigation, *context, capacity, *plan); }
    return result("prepare", error);
}

// input: Latest SIMULATED owner event, revalidated against the selected control.
// navigation: Local state receives accepted reversible transitions.
// context: Actual admission policy; transport remains in Python's existing endpoint owner.
// controls_visible: Drawing owner's completed narrative gate.
// x: Logical pointer x shared with raster orientation.
// y: Logical pointer y shared with raster orientation.
// effect: Receives explicit transport/render intent, never an assumed success.
// Exception: Returns original projection, shared plan or action failure.
CODY_HOST_EXPORT int cody_host_event_press(const Input *input, Presentation::Navigation *navigation,
    Presentation::Context *context, bool controls_visible, float x, float y, uint8_t *effect)
{
    if (!effect) { return result("press", ESP_ERR_INVALID_ARG); }
    *effect = static_cast<uint8_t>(Presentation::Effect::None);
    esp_err_t error = cody_host_event_prepare(input, navigation, context, &s_owner.plan);
    if (error != ESP_OK) { return error; }
    const auto *action = Presentation::hit_action(s_owner.plan, controls_visible, x, y);
    Presentation::Action back;
    if (Presentation::hit_back(s_owner.plan, controls_visible, x, y)) {
        back.id = Presentation::ActionId::Back;
        back.enabled = true;
        action = &back;
    }
    if (action == nullptr) { return ESP_OK; }
    Presentation::Effect decision;
    error = Presentation::activate(s_owner.event, *context, *action, *navigation, decision);
    if (error == ESP_OK) {
        Presentation::accept_navigation(*context, decision);
        *effect = static_cast<uint8_t>(decision);
    }
    return result("press", error);
}

// input: Latest isolated owner event, revalidated against the displayed route.
// navigation: Reversible page/choice state mutated only by the shared policy.
// context: Actual host freshness, errors and transport admission results.
// controls_visible: Drawing owner's complete reveal gate retained through the gesture.
// start_x: Logical contact origin x.
// start_y: Logical contact origin y.
// end_x: Logical release x in the same orientation.
// end_y: Logical release y in the same orientation.
// effect: Receives explicit transport or render intent, never inferred delivery.
// selected_request_id: Fixed 33-byte output, populated only by a queue-selection intent.
// Exception: Returns exact ABI, projection or shared action admission errors.
CODY_HOST_EXPORT int cody_host_event_release(const Input *input, Presentation::Navigation *navigation,
    Presentation::Context *context, bool controls_visible, int start_x, int start_y, int end_x, int end_y,
    uint8_t *effect, char *selected_request_id)
{
    if (!effect || !selected_request_id) { return result("release", ESP_ERR_INVALID_ARG); }
    *effect = static_cast<uint8_t>(Presentation::Effect::None);
    selected_request_id[0] = '\0';
    esp_err_t error = cody_host_event_prepare(input, navigation, context, &s_owner.plan);
    if (error != ESP_OK) { return error; }
    const Presentation::Action *action = nullptr;
    Presentation::Effect decision = Presentation::release(s_owner.plan, *navigation, controls_visible,
        start_x, start_y, end_x, end_y, action);
    Presentation::Action back;
    const int64_t delta_x = static_cast<int64_t>(end_x) - start_x;
    const int64_t delta_y = static_cast<int64_t>(end_y) - start_y;
    if (decision == Presentation::Effect::None && delta_x * delta_x + delta_y * delta_y < 144 &&
        Presentation::hit_back(s_owner.plan, controls_visible, start_x, start_y) &&
        Presentation::hit_back(s_owner.plan, controls_visible, end_x, end_y)) {
        back.id = Presentation::ActionId::Back;
        back.enabled = true;
        action = &back;
    }
    if (action != nullptr) {
        error = Presentation::activate(s_owner.event, *context, *action, *navigation, decision);
        if (error == ESP_OK && decision == Presentation::Effect::SelectQuestion) {
            std::memcpy(selected_request_id, action->request_id, sizeof(action->request_id));
        }
    }
    if (error == ESP_OK) {
        Presentation::accept_navigation(*context, decision);
        *effect = static_cast<uint8_t>(decision);
    }
    return result("release", error);
}

// input: Current SIMULATED readonly failure collection.
// navigation: Local page identity, with shared bounds and swipe admission.
// controls_visible: Complete narrative reveal gate.
// dx: Logical released horizontal displacement.
// dy: Logical released vertical displacement.
// changed: Receives whether an accepted page transition occurred.
// Exception: Returns actual ABI projection failure.
CODY_HOST_EXPORT int cody_host_event_scroll(const Input *input, Presentation::Navigation *navigation,
    bool controls_visible, int dx, int dy, bool *changed)
{
    if (!input || !navigation || !changed) { return result("scroll", ESP_ERR_INVALID_ARG); }
    Presentation::Context context{};
    const esp_err_t error = cody_host_event_prepare(input, navigation, &context, &s_owner.plan);
    if (error == ESP_OK) { *changed = Presentation::scroll(s_owner.plan, *navigation, controls_visible, dx, dy); }
    return result("scroll", error);
}

// plan: Complete prepared production plan.
// characters: Current typewriter budget excluding instantaneous lines and captions.
// title_length: Full overridden or authored native scene title length.
CODY_HOST_EXPORT bool cody_host_event_controls_ready(const Presentation::Plan *plan, uint32_t characters, uint32_t title_length)
{
    return plan && Presentation::controls_ready(*plan, characters, title_length);
}

// line: Shared line style, passed by value as a bounded 68-byte host-only projection.
// elapsed_us: Nonnegative host scene time in microseconds.
CODY_HOST_EXPORT uint16_t cody_host_event_line_color(Presentation::Line line, uint64_t elapsed_us)
{
    return Presentation::line_color(line, elapsed_us);
}

// text: Bounded printable ASCII event narrative from the host transport boundary.
// length: Exact byte count, excluding the trailing NUL.
// plan: Receives complete production wrapping without a Python text algorithm.
// Exception: Returns actual ASCII, bounds or viewport failure.
CODY_HOST_EXPORT int cody_host_event_wrap(const char *text, uint32_t length, Presentation::Plan *plan)
{
    if (!text || !plan || length >= 64 || std::strlen(text) != length) { return result("wrap", ESP_ERR_INVALID_ARG); }
    for (uint32_t index = 0; index < length; ++index) {
        if (text[index] < 32 || text[index] > 126) { return result("wrap", ESP_ERR_INVALID_ARG); }
    }
    *plan = {};
    uint16_t top = 196;
    const auto &font = CODY::SceneData::body_font;
    return result("wrap", Presentation::append_lines(*plan, text, top, (300U - font.width) / font.advance + 1U));
}

// input: Current SIMULATED readonly result.
// context: Exact admission/error state from the endpoint owner.
// controls_visible: Complete native narrative reveal gate.
// now_us: Pausable host presentation time.
// readable_at_us: UI-owned first readable timestamp, initially negative.
// due: Receives the same three-second dismissal decision used by firmware.
// Exception: Returns actual projection failure.
CODY_HOST_EXPORT int cody_host_event_dwell(const Input *input, const Presentation::Context *context,
    bool controls_visible, int64_t now_us, int64_t *readable_at_us, bool *due)
{
    if (!input || !context || !readable_at_us || !due) { return result("dwell", ESP_ERR_INVALID_ARG); }
    const esp_err_t error = project(*input, s_owner.event);
    if (error == ESP_OK) {
        *due = Presentation::checks_passed_dismiss_due(s_owner.event, *context, controls_visible, now_us, *readable_at_us);
    }
    return result("dwell", error);
}

// pixels: Existing logical SIMULATED RGB565 framebuffer owned by the host drawing backend.
// capacity: Exact full-canvas pixel count.
// actions: Complete shared-model controls, with production captions, styles and geometry.
// count: Number of controls in the current plan, bounded to one through three.
// Exception: Returns original shared control/text/raster failure.
CODY_HOST_EXPORT int cody_host_event_paint_actions(uint16_t *pixels, uint32_t capacity,
    const Presentation::Action *actions, uint32_t count)
{
    if (!pixels || !actions || capacity != 466U * 466U || count == 0 || count > 3) {
        return result("paint_actions", ESP_ERR_INVALID_ARG);
    }
    // This bounded scratch belongs only to the host call. All preparations succeed
    // before any pixel mutation; the Python drawing owner publishes only on ESP_OK.
    CODY2D::CapsuleControl controls[3]{};
    CODY2D::TextLine second_lines[3]{};
    for (uint32_t index = 0; index < count; ++index) {
        if (!valid_text(actions[index].label)) { return result("paint_actions", ESP_ERR_INVALID_ARG); }
        const esp_err_t error = Presentation::prepare_control(actions[index], CODY::SceneData::body_font,
            controls[index], second_lines[index]);
        if (error != ESP_OK) { return result("paint_actions", error); }
    }
    CODY2D::Surface surface{pixels, 466, 466, 466, capacity};
    for (uint32_t index = 0; index < count; ++index) {
        // The production capsule renderer already composites its first caption once.
        esp_err_t error = CODY2D::draw_capsule_rows(controls[index], surface, 0, 0);
        if (error == ESP_OK && second_lines[index].valid) {
            const auto &second = second_lines[index];
            error = CODY2D::draw_text_rows(second, second.text.length, surface, 0, 0);
        }
        if (error != ESP_OK) { return result("paint_actions", error); }
    }
    return ESP_OK;
}

// navigation: Reversible local stage receiving actual endpoint admission outcome.
// context: Actual result, retained so a failure yields visible explicit Retry.
// effect: Previously reviewed transport intent.
// error: Original endpoint error code, never silently converted into success.
// Exception: Returns INVALID_ARG for unexpected transport effect.
CODY_HOST_EXPORT int cody_host_event_result(Presentation::Navigation *navigation, Presentation::Context *context,
    uint8_t effect, int error)
{
    if (!navigation || !context || (effect != 2 && effect != 3 && effect != 5)) {
        return result("result", ESP_ERR_INVALID_ARG);
    }
    context->ui_error = error;
    context->retry = effect == 2 ? Presentation::EventRetry::Reply :
        effect == 3 ? Presentation::EventRetry::Dismiss : Presentation::EventRetry::None;
    if (error == ESP_OK) {
        if (effect == 2) { context->reply_admitted = true; navigation->stage = Presentation::EventStage::Waiting; }
        else if (effect == 3) { context->dismiss_admitted = true; }
    }
    return result("transport", error);
}

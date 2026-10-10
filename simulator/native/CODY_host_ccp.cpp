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

#include "LUNASHELL_codex_transaction.hpp"
#include "esp_log.h"
#include <cstring>
#include <new>

#if defined(_WIN32)
#define CODY_HOST_EXPORT extern "C" __declspec(dllexport)
#else
#define CODY_HOST_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {
struct HostTransaction {
    LUNASHELL::CodexEventPayload payload{};
    LUNASHELL::CodexEventPayload candidate{};
    char request_id[33]{};
    LUNASHELL::CodexEventState state = LUNASHELL::CodexEventState::None;
    int64_t expires_at_us = 0;
    esp_err_t last_error = ESP_OK;
    char dismissed_id[33]{};
    LUNASHELL::CodexEventState dismissed_state = LUNASHELL::CodexEventState::None;
    // No parameters: Exclusive per-link owner; Python serializes all endpoint access.
    LUNASHELL::CodexTransactionView view()
    {
        return {payload, request_id, state, expires_at_us, last_error};
    }
};
// operation: Actual synthetic-host boundary operation.
// error: Original portable firmware error.
esp_err_t hostError(const char *operation, esp_err_t error)
{
    if (error != ESP_OK) {
        ESP_LOGE("CodyHostCcp", "%s failed: %s (0x%x)", operation, esp_err_to_name(error), error);
    }
    return error;
}
} // namespace

// No parameters: Allocates only a synthetic host owner; no firmware snapshot exists here.
// Exception: Returns nullptr on allocation failure.
CODY_HOST_EXPORT void *cody_host_ccp_create()
{
    return new (std::nothrow) HostTransaction{};
}
// handle: Host owner being retired; nullptr is safe.
CODY_HOST_EXPORT void cody_host_ccp_destroy(void *handle)
{
    delete static_cast<HostTransaction *>(handle);
}
// handle: Exclusive synthetic host transaction.
// id: Original authenticated request identity.
// token: Original wire state.
// ttl: Original lifetime, checked before narrowing.
// data: Complete payload hexadecimal, or '-' for None/Unavailable.
// now_us: Explicit monotonic host time.
// Exception: Returns decoder or transition errors unchanged; no Python transition fallback.
CODY_HOST_EXPORT int32_t cody_host_ccp_event(void *handle, const char *id, const char *token,
    uint32_t ttl, const char *data, int64_t now_us)
{
    if (handle == nullptr || data == nullptr || ttl > 180) {
        return hostError("EVENT", ESP_ERR_INVALID_ARG);
    }
    auto &owner = *static_cast<HostTransaction *>(handle);
    LUNASHELL::CodexEventState state;
    if (!LUNASHELL::decodeCodexEventState(token, state)) {
        return hostError("EVENT state", ESP_ERR_INVALID_ARG);
    }
    const bool empty = state == LUNASHELL::CodexEventState::None || state == LUNASHELL::CodexEventState::Rejected;
    if (empty && std::strcmp(data, "-") != 0) {
        return hostError("EVENT absence", ESP_ERR_INVALID_ARG);
    }
    if (!empty) {
        const esp_err_t error = LUNASHELL::decodeCodexEventPayload(data, std::strlen(data), owner.candidate);
        if (error != ESP_OK) {
            return hostError("EVENT decode", error);
        }
        if (id != nullptr && std::strcmp(id, owner.dismissed_id) == 0) {
            return ESP_OK; // Owner repeats the retained real retirement observation.
        }
    }
    LUNASHELL::CodexTransactionEffect effect;
    return hostError("EVENT", LUNASHELL::applyCodexEvent(owner.view(), id, state,
        static_cast<uint16_t>(ttl), empty ? nullptr : &owner.candidate, now_us, effect));
}
// handle: Exclusive synthetic host transaction.
// id: Authenticated acknowledgement identity.
// choice: Authenticated semantic choice.
// status: Exact wire status.
// now_us: Explicit monotonic receive time.
// Exception: Returns original admission/recipient errors, including after admitted retirement.
CODY_HOST_EXPORT int32_t cody_host_ccp_ack(void *handle, const char *id, const char *choice,
    const char *status, int64_t now_us)
{
    if (handle == nullptr) {
        return hostError("REPLYACK", ESP_ERR_INVALID_ARG);
    }
    LUNASHELL::CodexTransactionEffect effect;
    return hostError("REPLYACK", LUNASHELL::applyCodexReplyAck(static_cast<HostTransaction *>(handle)->view(), id, choice, status, now_us, effect));
}
// handle: Exclusive synthetic host transaction.
// now_us: Explicit monotonic maintenance time.
// Exception: Returns INVALID_ARG for a missing owner or invalid monotonic value.
CODY_HOST_EXPORT int32_t cody_host_ccp_tick(void *handle, int64_t now_us)
{
    if (handle == nullptr || now_us < 0) {
        return hostError("expiry", ESP_ERR_INVALID_ARG);
    }
    LUNASHELL::CodexTransactionEffect effect;
    LUNASHELL::expireCodexTransaction(static_cast<HostTransaction *>(handle)->view(), now_us, effect);
    return ESP_OK;
}
// handle: Exclusive synthetic host question owner.
// id: Identity visible at explicit Send.
// choice: Authored semantic choice ID.
// fresh: Original owner-confirmed transport freshness.
// now_us: Explicit monotonic confirmation time.
// Exception: Returns original submission admission error.
CODY_HOST_EXPORT int32_t cody_host_ccp_submit(void *handle, const char *id, const char *choice, uint8_t fresh, int64_t now_us)
{
    if (handle == nullptr || fresh > 1) {
        return hostError("Send", ESP_ERR_INVALID_ARG);
    }
    return hostError("Send", LUNASHELL::submitCodexChoice(static_cast<HostTransaction *>(handle)->view(), id, choice, fresh != 0, now_us));
}
// handle: Exclusive synthetic host owner explicitly reset by commissioning/reset.
// Exception: Returns INVALID_ARG for a missing owner.
CODY_HOST_EXPORT int32_t cody_host_ccp_reset(void *handle)
{
    if (handle == nullptr) {
        return hostError("reset", ESP_ERR_INVALID_ARG);
    }
    *static_cast<HostTransaction *>(handle) = {};
    return ESP_OK;
}
// handle: Exclusive synthetic host transaction.
// id: Exact informational or terminal identity explicitly dismissed.
// Exception: Returns original admission failure; open questions remain visible.
CODY_HOST_EXPORT int32_t cody_host_ccp_dismiss(void *handle, const char *id)
{
    if (handle == nullptr) {
        return hostError("dismiss", ESP_ERR_INVALID_ARG);
    }
    auto &owner = *static_cast<HostTransaction *>(handle);
    const auto state = owner.state;
    const bool question = owner.payload.kind == LUNASHELL::CodexEventKind::Question;
    const esp_err_t error = LUNASHELL::dismissCodexTransaction(owner.view(), id);
    if (error != ESP_OK) {
        return hostError("dismiss", error);
    }
    std::memcpy(owner.dismissed_id, id, sizeof(owner.dismissed_id));
    owner.dismissed_state = question ? state : LUNASHELL::CodexEventState::None;
    return ESP_OK;
}
// handle: Exclusive synthetic host transaction.
// id: Writable 33-byte retained/current terminal identity output.
// state: Writable ordinal, None means no terminal observation is available.
// Exception: Returns INVALID_ARG for missing owner/output storage.
CODY_HOST_EXPORT int32_t cody_host_ccp_terminal(void *handle, char *id, uint32_t *state)
{
    if (handle == nullptr || id == nullptr || state == nullptr) {
        return hostError("terminal observation", ESP_ERR_INVALID_ARG);
    }
    const auto &owner = *static_cast<HostTransaction *>(handle);
    const bool current = owner.payload.kind == LUNASHELL::CodexEventKind::Question && LUNASHELL::codexEventIsTerminal(owner.state);
    const bool retained = LUNASHELL::codexEventIsTerminal(owner.dismissed_state);
    std::memcpy(id, current ? owner.request_id : owner.dismissed_id, 33);
    *state = static_cast<uint32_t>(current ? owner.state : (retained ? owner.dismissed_state : LUNASHELL::CodexEventState::None));
    return ESP_OK;
}
// handle: Exclusive synthetic host owner.
// payload: Host-only transport output with exact native layout.
// payload_size: Transport layout checked before a copy.
// id: Writable 33-byte identity output.
// state: Writable state ordinal output.
// deadline: Writable original monotonic deadline output.
// error: Writable actual transaction error output.
// Exception: Returns INVALID_SIZE or INVALID_ARG without exposing a partial transport.
CODY_HOST_EXPORT int32_t cody_host_ccp_read(void *handle, LUNASHELL::CodexEventPayload *payload,
    uint32_t payload_size, char *id, uint32_t *state, int64_t *deadline, int32_t *error)
{
    if (payload_size != sizeof(LUNASHELL::CodexEventPayload)) {
        return hostError("read layout", ESP_ERR_INVALID_SIZE);
    }
    if (handle == nullptr || payload == nullptr || id == nullptr || state == nullptr || deadline == nullptr || error == nullptr) {
        return hostError("read", ESP_ERR_INVALID_ARG);
    }
    const auto &owner = *static_cast<HostTransaction *>(handle);
    *payload = owner.payload;
    std::memcpy(id, owner.request_id, 33);
    *state = static_cast<uint32_t>(owner.state);
    *deadline = owner.expires_at_us;
    *error = owner.last_error;
    return ESP_OK;
}

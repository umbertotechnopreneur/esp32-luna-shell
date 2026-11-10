/* Copyright (c) 2026 Umberto Giacobbi */
#pragma once

#include "LUNASHELL_codex_state_status.hpp"

#include <cstddef>
#include <cstdint>

#include "lwip/sockets.h"

namespace LUNASHELL::codex_state {

// All methods use the existing CCP worker and its serialization mutex.

// socket: Bound CCP UDP descriptor.
// device: Canonical twelve-character lowercase station ID.
// peer: Exact active retained PC ID.
// key: Already persisted and read back active link key.
// local: Canonical local IPv4 address.
// now_us: Nonnegative receiver monotonic time.
void maintain(int socket, const char *device, const uint8_t *peer, const uint8_t *key,
              const char *local, uint64_t now_us);

// socket: Bound CCP UDP descriptor.
// payload: NUL-terminated CCP2 READY already bounded by ingress.
// sender: Socket-observed address, never sourced from packet fields.
// local: Current canonical station address.
// now_us: Nonnegative receiver monotonic time.
esp_err_t accept_ready(int socket, const char *payload, const sockaddr_in &sender,
                       const char *local, uint64_t now_us);

// socket: Bound CCP UDP descriptor.
// packet: Exact binary UDP packet bytes.
// length: UDP payload length after ingress budget admission.
// sender: Socket-observed source address.
// now_us: Nonnegative receiver monotonic time.
esp_err_t accept_packet(int socket, const uint8_t *packet, size_t length,
                        const sockaddr_in &sender, uint64_t now_us);

// clear_generation: True for explicit Forget or a persisted replacement key.
void disconnect(bool clear_generation);

// status: Bounded copy only; caller holds the CCP serialization mutex.
// now_us: Current receiver clock used to avoid stale liveness.
esp_err_t copy_status(CodexDomainStatus &status, uint64_t now_us);

} // namespace LUNASHELL::codex_state

#pragma once

#include <cstddef>
#include <cstdint>
#include "esp_err.h"

namespace CODY {

constexpr size_t kWhatsAppQrCapacity = 512;
enum class WhatsAppState : uint8_t { Unavailable, Idle, Connecting, Pairing, Connected, Stopping, Failed };

struct WhatsAppObservation {
    WhatsAppState state = WhatsAppState::Unavailable;
    uint32_t revision = 0;
    uint32_t message_sequence = 0;
    esp_err_t error = ESP_OK;
    bool running = false;
    int64_t qr_expires_us = 0;
    char qr[kWhatsAppQrCapacity + 1]{};
};

// No parameters: Start a single experimental WhatsApp session.
// Exception: Returns unavailable, busy, allocation, or task creation errors.
esp_err_t start_whatsapp();
// No parameters: Resume an already registered session without starting pairing.
esp_err_t resume_whatsapp();
// No parameters: Request flush and disconnect without erasing credentials.
// Exception: Returns the real admission error; completion remains asynchronous.
esp_err_t stop_whatsapp();
// result: Receives one coherent bounded owner snapshot; QR bytes are sensitive.
// Exception: Returns invalid input or the owner's current error.
esp_err_t read_whatsapp(WhatsAppObservation &result);
// sequence: Receives the bounded live-message sequence without copying QR data.
// received_us: Receives the monotonic time of the latest admitted message.
// Exception: Returns unavailable when the experimental owner is not built.
esp_err_t read_whatsapp_message(uint32_t &sequence, int64_t &received_us);

} // namespace CODY

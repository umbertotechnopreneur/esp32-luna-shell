#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CODY_WHATSAPP_PAIRING_QR = 1,
    CODY_WHATSAPP_PAIRING_EXPIRED = 2,
    CODY_WHATSAPP_CONNECTED = 3,
    CODY_WHATSAPP_DISCONNECTED = 4,
    CODY_WHATSAPP_LOGGED_OUT = 5,
    CODY_WHATSAPP_INCOMING = 6,
    CODY_WHATSAPP_UNDECRYPTABLE = 7,
} cody_whatsapp_notice_t;

/* The callback runs on the protocol executor. QR bytes are transient and must
 * be copied before it returns; other notices carry no payload. The callback and
 * context must remain valid until cody_whatsapp_run returns. */
typedef void (*cody_whatsapp_callback_t)(uint32_t notice, const uint8_t *payload,
                                        size_t size, uint64_t valid_seconds,
                                        uintptr_t context);

/* Reserve before launching the executor. Release only if task creation fails. */
int32_t cody_whatsapp_prepare(void);
void cody_whatsapp_release_unstarted(void);
/* Request a graceful disconnect without deleting or revoking the session. */
int32_t cody_whatsapp_stop(void);

/* Blocks on a dedicated, adequately sized task and never erases the store.
 * Returns one if resume_only finds no registered session, zero on a clean
 * stop, or a negative error. */
int32_t cody_whatsapp_run(cody_whatsapp_callback_t callback, uintptr_t context, bool resume_only);

#ifdef __cplusplus
}
#endif

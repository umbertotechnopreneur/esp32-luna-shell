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

#include "mem_alloc.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>

// destination: Caller-owned native text or phoneme buffer.
// capacity: Actual destination capacity, including its terminator.
// format: Constant printf format, checked by the compiler at call sites.
// ...: Format arguments whose types must match the format.
// Exception: Escapes to the active C guard on formatting failure or buffer overflow.
void luna_espeak_format(char *destination, size_t capacity, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    errno = 0;
    const int length = vsnprintf(destination, capacity, format, arguments);
    const int formatting_errno = errno;
    va_end(arguments);
    if (length < 0 || (size_t)length >= capacity) {
        const int cause = length < 0 ?
            (formatting_errno == 0 ? EILSEQ : formatting_errno) : EOVERFLOW;
        fprintf(stderr, "LUNASHELL_tts: native text formatting rejected (capacity=%u cause=%d)\n",
                (unsigned)capacity, cause);
        luna_espeak_fail(cause);
    }
}

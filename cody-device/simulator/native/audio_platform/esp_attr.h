#pragma once

// Host memory is owned by the process, not by ESP32 internal or external RAM.
#define EXT_RAM_BSS_ATTR
// Retained only for the lifetime of the simulated process, not across resets.
#define RTC_NOINIT_ATTR

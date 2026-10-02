#pragma once
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
// Register the flash-owned cue before the hardware owner initializes audio.
esp_err_t CODY_poc_prepare_boot(void);
// stage: Completed startup stage; timings use the real monotonic clock.
void CODY_poc_boot_checkpoint(const char *stage);
// Configure the existing logger mirror after the SD owner's startup stage.
esp_err_t CODY_poc_configure_diagnostics(void);
// Play the original eyes-open cue through the existing Luna codec route.
esp_err_t CODY_poc_play_eyes_open(void);
#ifdef __cplusplus
}
#endif

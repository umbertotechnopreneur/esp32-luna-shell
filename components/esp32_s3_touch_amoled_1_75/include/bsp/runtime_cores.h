#pragma once
#include "sdkconfig.h"

// Renderer and synchronous speech engine must never share a scheduler core.
enum {
    LUNASHELL_RENDER_CORE_ID = 1,
    LUNASHELL_TTS_CORE_ID = 0,
};

#if CONFIG_LUNASHELL_BUILD_ENABLE_TTS && CONFIG_FREERTOS_UNICORE
#error "Local speech requires separate renderer and synthesis cores"
#endif

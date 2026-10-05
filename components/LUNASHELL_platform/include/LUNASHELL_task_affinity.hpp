#pragma once

#include "sdkconfig.h"

namespace LUNASHELL::task_affinity {

// Audio acquisition, ESP-SR inference and its internal AFE task stay on CPU0.
// The native Cody frame owner stays on CPU1, including wake-word variants.
constexpr int kVoiceCoreId = 0;
constexpr int kCodyRenderCoreId = 1;

#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
#if CONFIG_FREERTOS_UNICORE
#error "Wake word requires a separate renderer core"
#endif
static_assert(kVoiceCoreId != kCodyRenderCoreId, "Voice must not share the Cody renderer core");
#endif

} // namespace LUNASHELL::task_affinity

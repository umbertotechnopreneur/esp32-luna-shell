#pragma once

#include <cstddef>
#include <cstdint>
#include "esp_err.h"

namespace LUNASHELL {

constexpr size_t kPreferredNameMaximum = 24;
struct CodyUserNameSnapshot;
enum class CodexNameOperation : uint8_t { Read, Configure };
using CodexNameExecutor = esp_err_t (*)(CodexNameOperation operation, const char *name,
                                       CodyUserNameSnapshot &result);

// executor: Sole Cody settings owner; invocation runs outside the CCP lock.
// Exception: Rejects null or replacement owners; registration performs no storage I/O.
esp_err_t registerCodexNameExecutor(CodexNameExecutor executor);

} // namespace LUNASHELL

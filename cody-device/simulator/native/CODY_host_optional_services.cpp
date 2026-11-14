// Optional host boundaries for services that are not attached to a Cody device.
#include "LUNASHELL_codex_debug.hpp"
#include "LUNASHELL_build_metadata.hpp"

namespace LUNASHELL {

// request: Destination remains unchanged; no simulated authenticated mailbox exists.
// Exception: Returns NOT_SUPPORTED and never invents an animation request.
esp_err_t takeCodexAnimation(CodexAnimationRequest &request)
{
    (void)request;
    return ESP_ERR_NOT_SUPPORTED;
}

// request: Original caller identity; no simulated mailbox has admitted it.
// result: Original render outcome, never turned into an acknowledgement.
// Exception: Returns NOT_SUPPORTED without acknowledging a remote command.
esp_err_t completeCodexAnimation(const CodexAnimationRequest &request, esp_err_t result)
{
    (void)request;
    (void)result;
    return ESP_ERR_NOT_SUPPORTED;
}

// request: Destination remains unchanged; no simulated authenticated mailbox exists.
// Exception: Returns NOT_SUPPORTED and never fabricates a debug request.
esp_err_t takeCodexDebugScreen(CodexDebugScreenRequest &request)
{
    (void)request;
    return ESP_ERR_NOT_SUPPORTED;
}

// request: Original caller identity; no simulated mailbox has admitted it.
// result: Original render outcome, never turned into an acknowledgement.
// channel_count: Original renderer count, not returned to a physical device.
// Exception: Returns NOT_SUPPORTED without acknowledging a remote command.
esp_err_t completeCodexDebugScreen(const CodexDebugScreenRequest &request, esp_err_t result,
    uint8_t channel_count)
{
    (void)request;
    (void)result;
    (void)channel_count;
    return ESP_ERR_NOT_SUPPORTED;
}

// No parameters: host code has no device firmware-build metadata to report.
// Returns explicit Unavailable fields with static lifetime, never host identity.
const FirmwareBuildMetadata &firmware_build_metadata()
{
    static constexpr FirmwareBuildMetadata unavailable{};
    return unavailable;
}

} // namespace LUNASHELL

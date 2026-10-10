#include "LUNASHELL_standby_resources.hpp"
#include "esp_log.h"

namespace LUNASHELL::power_policy {
namespace {
constexpr char kTag[] = "LUNASHELL_standby";
}

// component: Diagnostic name of the optional resource owner.
// target: Whether the resource should apply its standby policy.
// status: Last confirmed transition state, retained while the owner evaluates policy.
// transition: Owner operation returning its real transition error.
// reassert: Call the owner even if its cached applied state already matches.
// Exception: Returns the original transition error; only unused build-off owners are optional.
esp_err_t transition_standby_resource(
    const char *component,
    bool target,
    StandbyResourceStatus &status,
    StandbyTransition transition,
    bool reassert
)
{
    if (status.unsupported ||
        (!reassert && status.consistent && status.applied == target)) {
        return ESP_OK;
    }
    const esp_err_t error = transition(target);
    if (error == ESP_ERR_NOT_SUPPORTED) {
        // A build-off implementation is only classified as unavailable from
        // the known active baseline. NOT_SUPPORTED after a successful
        // suspension or an earlier failure cannot prove the hardware state.
        if (status.consistent && !status.applied && target) {
            status.unsupported = true;
            ESP_LOGI(
                kTag,
                "%s standby transition unavailable in this build profile",
                component
            );
            return ESP_OK;
        }
        status.consistent = false;
        ESP_LOGE(
            kTag,
            "%s transition to %s became unavailable after lifecycle use",
            component,
            target ? "standby" : "active"
        );
        return error;
    }
    if (error != ESP_OK) {
        // Component APIs attempt their own rollback, but their return value
        // cannot prove that a nested rollback succeeded. Force the outer
        // recovery to re-assert its target instead of trusting stale state.
        status.consistent = false;
        ESP_LOGE(
            kTag,
            "%s transition to %s failed (%s)",
            component,
            target ? "standby" : "active",
            esp_err_to_name(error)
        );
        return error;
    }
    status.applied = target;
    status.consistent = true;
    status.unsupported = false;
    return ESP_OK;
}

} // namespace LUNASHELL::power_policy

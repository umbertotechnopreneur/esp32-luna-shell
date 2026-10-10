#include "LUNASHELL_standby_resources.hpp"
#include "esp_log.h"

namespace LUNASHELL::power_policy {
namespace {
constexpr char kTag[] = "LUNASHELL_standby";

// owner: Borrowed legacy function pointer, valid until the synchronous call returns.
// target: Requested owner state.
// Exception: Returns the original hardware owner's error.
esp_err_t call_legacy_owner(void *owner, bool target)
{
    return (*static_cast<StandbyTransition *>(owner))(target);
}
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
    if (component == nullptr || transition == nullptr) {
        ESP_LOGE(kTag, "Legacy resource transition rejected: invalid component or owner operation");
        return ESP_ERR_INVALID_ARG;
    }
    return transition_standby_resource(component, target, status, &transition,
        call_legacy_owner, reassert);
}

// component: Diagnostic identity of the owner.
// target: Whether the resource should apply its standby policy.
// status: Last confirmed transition state, retained while policy evaluates.
// owner: Borrowed synchronous hardware or synthetic resource context.
// transition: Owner operation returning its real transition error.
// reassert: Call the owner even when its cached state already matches.
// Exception: Returns original failure; only unused build-off owners are optional.
esp_err_t transition_standby_resource(const char *component, bool target,
    StandbyResourceStatus &status, void *owner, StandbyOwnerTransition transition,
    bool reassert)
{
    if (component == nullptr || owner == nullptr || transition == nullptr) {
        ESP_LOGE(kTag, "Resource transition rejected: invalid owner arguments");
        return ESP_ERR_INVALID_ARG;
    }
    if (status.unsupported ||
        (!reassert && status.consistent && status.applied == target)) {
        return ESP_OK;
    }
    const esp_err_t error = transition(owner, target);
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

// target: Requested panel-off state.
// applied: Borrowed last confirmed state.
// consistent: Borrowed state confidence, invalidated before recovery.
// display_enabled: Whether NOT_SUPPORTED is a genuine build-off panel.
// owner: Borrowed synchronous hardware or synthetic panel context.
// transition: Panel operation retaining its actual failure cause.
// rollback_error: Receives the independently reported local rollback cause.
// Exception: Preserves the original failure even after successful rollback.
esp_err_t transition_standby_display(bool target, bool &applied, bool &consistent,
    bool display_enabled, void *owner, StandbyOwnerTransition transition,
    esp_err_t &rollback_error)
{
    rollback_error = ESP_OK;
    if (owner == nullptr || transition == nullptr) {
        ESP_LOGE(kTag, "Display transition rejected: invalid owner arguments");
        return ESP_ERR_INVALID_ARG;
    }
    if (consistent && applied == target) {
        return ESP_OK;
    }
    const esp_err_t error = transition(owner, target);
    if (!display_enabled && error == ESP_ERR_NOT_SUPPORTED) {
        applied = false;
        consistent = true;
        return ESP_OK;
    }
    if (error == ESP_OK) {
        applied = target;
        consistent = true;
        return ESP_OK;
    }
    consistent = false;
    ESP_LOGE(kTag, "Display transition to %s failed (%s)", target ? "standby" : "active",
        esp_err_to_name(error));
    rollback_error = transition(owner, !target);
    if (rollback_error == ESP_OK || (!display_enabled && rollback_error == ESP_ERR_NOT_SUPPORTED)) {
        applied = !target;
        consistent = true;
    } else {
        ESP_LOGE(kTag, "Display rollback to %s failed (%s)", target ? "active" : "standby",
            esp_err_to_name(rollback_error));
    }
    return error;
}

} // namespace LUNASHELL::power_policy

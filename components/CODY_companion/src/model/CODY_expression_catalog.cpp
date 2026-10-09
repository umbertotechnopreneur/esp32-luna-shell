#include "CODY_expression_catalog.hpp"

#include <cstring>
#include <iterator>
#include "cloud_rig_data.hpp"
#include "scene_data.hpp"
#include "esp_log.h"

namespace CODY {
namespace {
constexpr char kTag[] = "CODY_expressions";

// The catalog lists only implemented combinations. Atlas plans do not imply a preset.
constexpr ExpressionDefinition kExpressions[] = {
    {ExpressionId::Calm, "calm", "Calm", Data::ExpressionPresets::Calm},
    {ExpressionId::Curious, "curious", "Curious", Data::ExpressionPresets::Curious},
    {ExpressionId::Happy, "happy", "Happy", Data::ExpressionPresets::Happy},
    {ExpressionId::Focused, "focused", "Focused", Data::ExpressionPresets::Focused},
    {ExpressionId::Stressed, "stressed", "Stressed", Data::ExpressionPresets::Stressed},
    {ExpressionId::Sleeping, "sleeping", "Sleeping", Data::ExpressionPresets::Sleeping},
    {ExpressionId::Drowsy, "drowsy", "Drowsy", Data::ExpressionPresets::Drowsy},
    {ExpressionId::Embarrassed, "embarrassed", "Embarrassed", Data::ExpressionPresets::Embarrassed},
    {ExpressionId::Worried, "worried", "Worried", Data::ExpressionPresets::Worried},
    {ExpressionId::Afraid, "afraid", "Afraid", Data::ExpressionPresets::Afraid},
    {ExpressionId::BootSleeping, "boot.sleep", "Boot sleeping preview", Data::sleep},
    {ExpressionId::BootWaking, "boot.waking", "Boot waking preview", Data::waking},
    {ExpressionId::BootReady, "boot.ready", "Boot ready preview", Data::ready},
};

constexpr AtlasScreenDefinition kAtlasScreens[] = {
    {AtlasScreenId::Sleeping01, "01", "Sleeping", ExpressionId::BootSleeping},
    {AtlasScreenId::Waking02, "02", "Waking up", ExpressionId::BootWaking},
    {AtlasScreenId::Ready03, "03", "Ready", ExpressionId::BootReady},
    {AtlasScreenId::Greeting04, "04", "Hi, I'm Cody!", ExpressionId::Happy, SceneId::Greeting},
    {AtlasScreenId::PairInvitation05, "05", "Let's pair", ExpressionId::Happy, SceneId::PairInvitation},
    {AtlasScreenId::InstallApp06, "06", "Get pairing app", ExpressionId::Worried, SceneId::InstallApp},
    {AtlasScreenId::ScanToPair06Bis, "06-bis", "Link your phone", ExpressionId::Calm, SceneId::ScanToPair},
    {AtlasScreenId::Pairing07, "07", "Pairing...", ExpressionId::Calm, SceneId::Pairing},
    {AtlasScreenId::PairingFailed08, "08", "Try again", ExpressionId::Afraid, SceneId::PairingFailed},
    {AtlasScreenId::Online09, "09", "You're online.", ExpressionId::Happy, SceneId::Online},
    {AtlasScreenId::MakeItYours10, "10", "Make it yours", ExpressionId::Calm, SceneId::MakeItYours},
    {AtlasScreenId::CodyLook11, "11", "Cody's look", ExpressionId::Happy, SceneId::CodyLook},
    {AtlasScreenId::ConnectCodex12, "12", "Connect Codex", ExpressionId::Happy, SceneId::ConnectCodex},
    {AtlasScreenId::SetupComplete13, "13", "You're all set.", ExpressionId::Happy, SceneId::SetupComplete},
    {AtlasScreenId::LookingForPc14, "14", "Looking for your PC...", ExpressionId::Calm, SceneId::LookingForPc},
    {AtlasScreenId::Connected15, "15", "Connected", ExpressionId::Happy, SceneId::Connected},
    {AtlasScreenId::Idle16, "16", "Idle", ExpressionId::Calm},
    {AtlasScreenId::Sleep35A, "35A", "Sleep", ExpressionId::Sleeping},
    {AtlasScreenId::Wake35B, "35B", "Wake", ExpressionId::Drowsy},
    {AtlasScreenId::Settings36, "36", "Settings", ExpressionId::Calm, SceneId::Settings},
    {AtlasScreenId::CodexChoice40, "40", "Connect Codex?", ExpressionId::Calm, SceneId::CodexChoice},
    {AtlasScreenId::SetupMenu41, "41", "Setup", ExpressionId::Calm, SceneId::SetupMenu},
    {AtlasScreenId::NetworkWait42, "42", "No connection", ExpressionId::Afraid, SceneId::NetworkWait},
#if CONFIG_LUNASHELL_BUILD_MODE_DEBUG
    {AtlasScreenId::PairingDebug39, "39", "Pairing debug", ExpressionId::Calm, SceneId::DebugText},
#endif
};

// Three additional entries are the separate authored boot preview namespace.
static_assert(std::size(kExpressions) == Data::expression_preset_count + 3,
    "Every authored expression preset must have an explicit runtime catalog identity");

// first: Immutable non-null catalog key.
// second: Immutable non-null catalog key whose identity must stay unambiguous.
constexpr bool same_key(const char *first, const char *second)
{
    size_t offset = 0;
    while (first[offset] != '\0' || second[offset] != '\0') {
        if (first[offset] != second[offset]) {
            return false;
        }
        ++offset;
    }
    return true;
}

// No parameters: reject duplicate identities, unresolved references, and conflicting scene faces.
constexpr bool catalog_is_unambiguous()
{
    for (size_t first = 0; first < std::size(kExpressions); ++first) {
        for (size_t second = first + 1; second < std::size(kExpressions); ++second) {
            if (kExpressions[first].id == kExpressions[second].id ||
                same_key(kExpressions[first].key, kExpressions[second].key)) {
                return false;
            }
        }
    }
    for (size_t first = 0; first < std::size(kAtlasScreens); ++first) {
        for (size_t second = first + 1; second < std::size(kAtlasScreens); ++second) {
            if (kAtlasScreens[first].id == kAtlasScreens[second].id ||
                same_key(kAtlasScreens[first].key, kAtlasScreens[second].key)) {
                return false;
            }
        }
        bool expression_found = false;
        for (const auto &expression : kExpressions) {
            if (expression.id == kAtlasScreens[first].expression) {
                expression_found = true;
                break;
            }
        }
        if (!expression_found) {
            return false;
        }
        if (kAtlasScreens[first].scene.has_value()) {
            size_t matching_scene_count = 0;
            for (const auto &scene : SceneData::definitions) {
                if (scene.id != *kAtlasScreens[first].scene) {
                    continue;
                }
                ++matching_scene_count;
                // A routed screen and its named expression must select the same face.
                for (const auto &expression : kExpressions) {
                    if (expression.id != kAtlasScreens[first].expression) {
                        continue;
                    }
                    for (size_t channel = 0; channel < kFaceChannelCount; ++channel) {
                        if (expression.face.values[channel] != scene.face.values[channel]) {
                            return false;
                        }
                    }
                }
            }
            if (matching_scene_count != 1) {
                return false;
            }
        }
    }
    return true;
}

static_assert(catalog_is_unambiguous(), "Expression catalog identities or atlas references are ambiguous");
} // namespace

// No parameters: immutable views retain stable semantic IDs across table reorderings.
ExpressionCatalogView expression_catalog()
{
    return {kExpressions, std::size(kExpressions), kAtlasScreens, std::size(kAtlasScreens)};
}

// identity: Stable expression identity, never a table position.
// definition: Cleared before lookup so a failed selection cannot reuse another expression.
esp_err_t find_expression(ExpressionId identity, const ExpressionDefinition *&definition)
{
    definition = nullptr;
    for (const auto &expression : kExpressions) {
        if (expression.id == identity) {
            definition = &expression;
            return ESP_OK;
        }
    }
    ESP_LOGE(kTag, "Expression lookup rejected: id=%u (%s)",
        static_cast<unsigned>(identity), esp_err_to_name(ESP_ERR_NOT_FOUND));
    return ESP_ERR_NOT_FOUND;
}

// key: Exact expression key; null and empty keys are invalid arguments.
// definition: Cleared before lookup and assigned only when one catalog key matches.
esp_err_t find_expression(const char *key, const ExpressionDefinition *&definition)
{
    definition = nullptr;
    if (key == nullptr || key[0] == '\0') {
        ESP_LOGE(kTag, "Expression lookup rejected: key=%s (%s)",
            key != nullptr ? key : "<null>", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    for (const auto &expression : kExpressions) {
        if (std::strcmp(expression.key, key) == 0) {
            definition = &expression;
            return ESP_OK;
        }
    }
    ESP_LOGE(kTag, "Expression lookup rejected: key=%s (%s)", key, esp_err_to_name(ESP_ERR_NOT_FOUND));
    return ESP_ERR_NOT_FOUND;
}

// identity: Stable screen identity from the implemented atlas subset.
// definition: Cleared before lookup so an unknown screen cannot reuse another mapping.
esp_err_t find_atlas_screen(AtlasScreenId identity, const AtlasScreenDefinition *&definition)
{
    definition = nullptr;
    for (const auto &screen : kAtlasScreens) {
        if (screen.id == identity) {
            definition = &screen;
            return ESP_OK;
        }
    }
    ESP_LOGE(kTag, "Atlas lookup rejected: id=%u (%s)",
        static_cast<unsigned>(identity), esp_err_to_name(ESP_ERR_NOT_FOUND));
    return ESP_ERR_NOT_FOUND;
}

// key: Exact atlas key with its suffix; omitted suffixes have no automatic meaning.
// definition: Cleared before lookup and assigned only for an implemented exact key.
esp_err_t find_atlas_screen(const char *key, const AtlasScreenDefinition *&definition)
{
    definition = nullptr;
    if (key == nullptr || key[0] == '\0') {
        ESP_LOGE(kTag, "Atlas lookup rejected: key=%s (%s)",
            key != nullptr ? key : "<null>", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    for (const auto &screen : kAtlasScreens) {
        if (std::strcmp(screen.key, key) == 0) {
            definition = &screen;
            return ESP_OK;
        }
    }
    ESP_LOGE(kTag, "Atlas lookup rejected: key=%s (%s)", key, esp_err_to_name(ESP_ERR_NOT_FOUND));
    return ESP_ERR_NOT_FOUND;
}

// identity: Selected expression combination whose base does not own rhythmic clocks.
// face: Updated only after successful lookup and authored rig-range validation.
esp_err_t expression_channels(ExpressionId identity, Channels &face)
{
    const ExpressionDefinition *definition = nullptr;
    esp_err_t result = find_expression(identity, definition);
    if (result != ESP_OK) {
        return result;
    }
    result = validate_rig_pose(definition->face, Data::placement);
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "Expression pose rejected: key=%s (%s)", definition->key, esp_err_to_name(result));
        return result;
    }
    face = definition->face;
    return ESP_OK;
}

} // namespace CODY

#pragma once

#include <cstddef>
#include <optional>
#include "CODY_rig.hpp"

namespace CODY {

// Explicit values belong to the expression namespace, independent of atlas screens.
enum class ExpressionId : uint16_t {
    Calm = 1,
    Happy = 2,
    Sleeping = 3,
    Drowsy = 4,
    Embarrassed = 5,
    Worried = 6,
    Afraid = 7,
    Curious = 8,
    Focused = 9,
    Stressed = 10,
    BootSleeping = 101,
    BootWaking = 102,
    BootReady = 103,
};

// Text keys preserve the atlas suffixes; the numeric values are internal typed IDs.
enum class AtlasScreenId : uint16_t {
    Sleeping01 = 1,
    Waking02 = 2,
    Ready03 = 3,
    Greeting04 = 4,
    PairInvitation05 = 5,
    InstallApp06 = 6,
    ScanToPair06Bis = 61,
    Pairing07 = 7,
    PairingFailed08 = 8,
    Online09 = 9,
    MakeItYours10 = 10,
    CodyLook11 = 11,
    ConnectCodex12 = 12,
    SetupComplete13 = 13,
    LookingForPc14 = 14,
    Connected15 = 15,
    Idle16 = 16,
    Settings36 = 36,
    PairingDebug39 = 39,
    CodexChoice40 = 40,
    SetupMenu41 = 41,
    NetworkWait42 = 42,
    Sleep35A = 350,
    Wake35B = 351,
};

enum class SceneId : uint16_t;

struct ExpressionDefinition {
    ExpressionId id;
    const char *key;
    const char *name;
    Channels face;
};

struct AtlasScreenDefinition {
    AtlasScreenId id;
    const char *key;
    const char *name;
    ExpressionId expression;
    std::optional<SceneId> scene = std::nullopt;
};

struct ExpressionCatalogView {
    const ExpressionDefinition *expressions;
    size_t expression_count;
    const AtlasScreenDefinition *atlas_screens;
    size_t atlas_screen_count;
};

// No parameters: expose the immutable implemented catalog for named enumeration.
// Table positions are traversal only; use the typed IDs or exact keys for selection.
ExpressionCatalogView expression_catalog();

// identity: Stable expression identity; unknown values are rejected explicitly.
// definition: Receives the immutable entry on success and nullptr on failure.
esp_err_t find_expression(ExpressionId identity, const ExpressionDefinition *&definition);

// key: Exact case-sensitive expression key, such as "calm" or "boot.waking".
// definition: Receives the immutable entry on success and nullptr on failure.
esp_err_t find_expression(const char *key, const ExpressionDefinition *&definition);

// identity: Exact implemented atlas screen identity, separate from expression IDs.
// definition: Receives the immutable screen mapping on success and nullptr on failure.
esp_err_t find_atlas_screen(AtlasScreenId identity, const AtlasScreenDefinition *&definition);

// key: Exact implemented atlas key, including "11", "16", "35A" and "35B".
// An omitted leading zero or an ambiguous "35" is rejected.
// definition: Receives the immutable screen mapping on success and nullptr on failure.
esp_err_t find_atlas_screen(const char *key, const AtlasScreenDefinition *&definition);

// identity: Stable selected combination, independent from breathing and blinking.
// face: Receives valid authored base channels on success and remains unchanged on failure.
esp_err_t expression_channels(ExpressionId identity, Channels &face);

} // namespace CODY

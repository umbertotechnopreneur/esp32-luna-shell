/* VBWR B
 *
 * Project: ESP32 Luna Shell
 * Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
 * Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
 *
 * VibeWare: Human intent, AI execution, and plenty of tokens
 * Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
 *
 * Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
 * Human guidance: Umberto Giacobbi; requested VibeWare branding.
 *
 * Copyright (c) 2026 Umberto Giacobbi
 *
 * VBWR E */

#include "../../components/LUNASHELL_platform/include/LUNASHELL_scene_standby_policy.hpp"
#include "../../components/LUNASHELL_platform/include/LUNASHELL_standby_policy.hpp"

using LUNASHELL::power_policy::SceneStandbyOverride;
using LUNASHELL::power_policy::scene_standby_expired;
using LUNASHELL::power_policy::scene_standby_timeout_seconds;

// Entry grants the scene interval even when the configured global timeout expired.
constexpr SceneStandbyOverride pairing{60, 120000000};
static_assert(scene_standby_timeout_seconds(30, pairing) == 60);
static_assert(scene_standby_timeout_seconds(0, pairing) == 60);
static_assert(!scene_standby_expired(179999999, 1000000, pairing));
static_assert(scene_standby_expired(180000000, 1000000, pairing));

// Real activity extends the same interval; an ordinary frame does not modify entry.
static_assert(!scene_standby_expired(209999999, 150000000, pairing));
static_assert(scene_standby_expired(210000000, 150000000, pairing));
constexpr SceneStandbyOverride next_scene{60, 180000000};
static_assert(!scene_standby_expired(239999999, 150000000, next_scene));
static_assert(scene_standby_expired(240000000, 150000000, next_scene));

// Exit inherits the latest configured value, including an explicitly disabled timeout.
constexpr SceneStandbyOverride inherited{};
static_assert(scene_standby_timeout_seconds(30, inherited) == 30);
static_assert(scene_standby_timeout_seconds(120, inherited) == 120);
static_assert(scene_standby_timeout_seconds(0, inherited) == 0);
static_assert(!scene_standby_expired(240000000, 0, inherited));

// Full uint32 second authoring remains safe at the explicit ESP timer boundary.
constexpr SceneStandbyOverride maximum{UINT32_MAX, 0};
static_assert(!scene_standby_expired(4294967294999999LL, 0, maximum));
static_assert(scene_standby_expired(4294967295000000LL, 0, maximum));
static_assert(!scene_standby_expired(119999999, 0, pairing));

using LUNASHELL::power_policy::StandbySleep;
using LUNASHELL::power_policy::select_standby_sleep;
using LUNASHELL::power_policy::select_standby_audio;

static_assert(!select_standby_audio(false, false));
static_assert(!select_standby_audio(false, true));
static_assert(select_standby_audio(true, false));
static_assert(!select_standby_audio(true, true));

// Release permits deep sleep only when no retained service needs the CPU/radio.
static_assert(select_standby_sleep(true, false, false, true) == StandbySleep::Deep);
static_assert(select_standby_sleep(true, true, false, true) == StandbySleep::Software);
static_assert(select_standby_sleep(true, false, true, true) == StandbySleep::Software);
static_assert(select_standby_sleep(true, true, true, true) == StandbySleep::Software);
// Debug and a missing physical touch wake stay in the existing software path.
static_assert(select_standby_sleep(false, false, false, true) == StandbySleep::Software);
static_assert(select_standby_sleep(false, true, true, true) == StandbySleep::Software);
static_assert(select_standby_sleep(true, false, false, false) == StandbySleep::Software);

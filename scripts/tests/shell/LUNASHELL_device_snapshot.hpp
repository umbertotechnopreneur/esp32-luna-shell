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

#pragma once
#include <cstdint>
namespace LUNASHELL {
enum class DeviceOperatingState { Active, EnteringStandby, Standby, Waking };
struct PowerManagementSnapshot {
    DeviceOperatingState state = DeviceOperatingState::Active;
    uint32_t transition_sequence = 0;
};
struct DeviceSnapshot { PowerManagementSnapshot power; };
class DeviceSnapshotService {
public:
    DeviceSnapshot current;
    int read_error = 0;
    static DeviceSnapshotService &instance() { static DeviceSnapshotService service; return service; }
    // power: Bounded fixture transport; unchanged when the injected read fails.
    int readPowerState(PowerManagementSnapshot &power) const {
        if (read_error != 0) return read_error;
        power = current.power;
        return 0;
    }
};
}

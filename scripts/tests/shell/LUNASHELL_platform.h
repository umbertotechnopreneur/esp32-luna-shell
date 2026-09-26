#pragma once
#include "brookesia/system_core.hpp"
constexpr int ESP_OK = 0;
constexpr int ESP_FAIL = -1;
inline void LUNASHELL_power_manager_note_ui_standby_result(uint32_t sequence, int result) {
    shell_test::fixture.acknowledgements.emplace_back(sequence, result);
}

/* Copyright (c) 2026 Umberto Giacobbi */
#pragma once

#include "CODY_settings_model.hpp"

namespace CODY::SettingsState {

enum class Field : uint8_t { FirstBoot, BirthIntro, FirstBootMusic, TourStep, SetupProgress };

// settings: Complete successfully loaded and validated storage projection.
void publish_loaded(const OnboardingSettings &settings);

// error: Original initialization/read/default-write failure; never replaced by defaults.
void publish_load_failure(esp_err_t error);

// field: Persisted field whose explicit write and readback just completed.
// value: Exact requested boolean or validated tour identity.
// error: Original write/readback result; failed writes leave confirmed values unchanged.
void publish_write(Field field, uint32_t value, esp_err_t error);

} // namespace CODY::SettingsState

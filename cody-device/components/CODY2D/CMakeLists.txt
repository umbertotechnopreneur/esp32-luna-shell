# VBWR B
#
# Project: ESP32 Luna Shell
# Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
# Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
#
# VibeWare: Human intent, AI execution, and plenty of tokens
# Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
#
# Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
# Human guidance: Umberto Giacobbi; requested VibeWare branding.
#
# Copyright (c) 2026 Umberto Giacobbi
#
# VBWR E

idf_component_register(
    SRCS "src/CODY2D.cpp" "src/CODY2D_paths.cpp" "src/CODY2D_text.cpp" "src/CODY2D_controls.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_common log
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

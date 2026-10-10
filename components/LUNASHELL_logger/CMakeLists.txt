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
    SRCS "src/LUNASHELL_logger.c" "src/LUNASHELL_logger_usb.c"
    INCLUDE_DIRS "include"
    REQUIRES log freertos esp_timer heap esp_driver_usb_serial_jtag esp_rom esp_app_format esp_system esp_hw_support
)

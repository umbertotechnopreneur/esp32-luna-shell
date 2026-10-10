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

if(CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE AND
   CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART)
    idf_component_register(
        SRCS
            "src/LUNASHELL_tof400f.c"
            "src/LUNASHELL_tof400f_protocol.c"
        INCLUDE_DIRS "include"
    )
    # These components are already discovered by the full project. Link them
    # only for the enabled driver so the default-OFF component stays empty.
    idf_component_optional_requires(PUBLIC esp_common)
    idf_component_optional_requires(
        PRIVATE
        esp_driver_uart
        esp_timer
        freertos
        log
    )
else()
    idf_component_register()
endif()

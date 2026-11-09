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

# Keep the product identity for the existing CCP, pairing and power owners.
# The native image contains the device panel, not the legacy character runtime.
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    if(CONFIG_LUNASHELL_CODY_RENDER_PEARL_JELLY OR
       CONFIG_LUNASHELL_BUILD_ENABLE_TTS OR
       CONFIG_LUNASHELL_ACTIVITY_MODEL OR
       CONFIG_LUNASHELL_ACTIVITY_CAPTURE)
        message(FATAL_ERROR
            "The Cody device panel excludes character rendering, TTS and activity models. Select cody-companion with Feature none or wake-word and its current defaults.")
    endif()
    if(NOT CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD AND
       NOT CONFIG_LUNASHELL_BUILD_AUDIO_INPUT_DRIVER_NONE)
        message(FATAL_ERROR
            "The lightweight Cody device panel requires AUDIO_INPUT_DRIVER_NONE. Select Feature wake-word to enable microphone handoff to Codex.")
    endif()
endif()

idf_component_register(
    SRCS
        "src/runtime/CODY_device_ui.cpp"
        "src/rendering/CODY_pixel_avatar.cpp"
        "src/scenes/CODY_pixel_demo.cpp"
        "../LUNASHELL_boot/src/ui/VIBEWARE_boot_animation.c"
        "../../assets/branding/VibeWare/generated/vibeware_boot_data.c"
        "src/runtime/CODY_device_model.cpp"
        "src/runtime/CODY_device_effects.cpp"
        "src/runtime/CODY_local_timer.cpp"
        "src/model/CODY_battery_alerts.cpp"
        "src/scenes/CODY_scene.cpp"
        "src/scenes/CODY_onboarding.cpp"
        "src/scenes/CODY_device_view.cpp"
        "src/scenes/CODY_codex_panels.cpp"
        "src/scenes/CODY_system_info.cpp"
        "src/scenes/CODY_local_timer_view.cpp"
        "src/scenes/CODY_screensaver.cpp"
        "src/scenes/CODY_reset_countdown.cpp"
        "src/scenes/CODY_reset_countdown_screen.cpp"
        "src/scenes/CODY_technical_panels.cpp"
        "src/scenes/CODY_home_dashboard.cpp"
        "src/scenes/CODY_home_identity.cpp"
        "../../assets/cody/home-dashboard-fonts/generated/home_dashboard_fonts.cpp"
        "src/runtime/CODY_settings.cpp"
        "src/runtime/CODY_codex_audio.cpp"
        "../../assets/cody/audio/codex-actions/generated/codex_audio_assets.cpp"
        "src/scenes/CODY_account_dashboard.cpp"
        "src/scenes/CODY_token_history.cpp"
        "src/scenes/CODY_widget_visibility_view.cpp"
        "src/diagnostics/CODY_debug_views.cpp"
        "../../assets/cody/scenes/generated/scene_font.cpp"
        "../../assets/cody/scenes/generated/scene_body_font.cpp"
    INCLUDE_DIRS "include" "../../assets/cody/personality/generated"
        "../../assets/cody/audio/codex-actions/generated"
    PRIV_INCLUDE_DIRS "../../assets/cody/scenes/generated"
        "../LUNASHELL_boot/include"
        "../../assets/branding/VibeWare/generated"
        "../../assets/cody/home-dashboard-fonts/generated"
        "../../assets/cody/rig/generated"
    REQUIRES CODY2D LUNASHELL_platform heap
    PRIV_REQUIRES esp32_s3_touch_amoled_1_75 esp_timer esp_system esp_app_format
)

if(NOT CMAKE_BUILD_EARLY_EXPANSION AND CONFIG_LUNASHELL_BUILD_PROFILE_CODY_COMPANION)
    # optional_requires matches admitted managed identities, not registry aliases.
    idf_component_optional_requires(PRIVATE espressif__qrcode)
endif()

if(NOT CMAKE_BUILD_EARLY_EXPANSION AND CONFIG_LUNASHELL_CODY_RELAY_PROBE)
    idf_component_optional_requires(PRIVATE esp_http_client mbedtls tcp_transport esp-tls)
    target_sources(${COMPONENT_LIB} PRIVATE "src/runtime/CODY_relay_probe.cpp")
endif()

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    CODY_DEVICE_UI_ONLY=1
    CODY_ENABLE_PEARL_JELLY=0
    CODY_JELLY_SIMD_AVAILABLE=0
)

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    # The shared PCM owner reads int16_t samples in place. IDF's binary embed
    # helper omits section alignment, so keep the authored bytes in an aligned section.
    set(cody_boot_pcm "${PROJECT_DIR}/assets/cody/audio/codex-actions/generated/boot-rise.pcm16le")
    set(cody_boot_assembly "${CMAKE_CURRENT_BINARY_DIR}/cody_device_boot_rise.S")
    file(GENERATE OUTPUT "${cody_boot_assembly}" CONTENT
".section .rodata.cody_device_boot_rise,\"a\",@progbits
.balign 4
.global _binary_cody_device_boot_rise_start
_binary_cody_device_boot_rise_start:
.incbin \"${cody_boot_pcm}\"
.global _binary_cody_device_boot_rise_end
_binary_cody_device_boot_rise_end:
")
    set_source_files_properties("${cody_boot_assembly}" PROPERTIES OBJECT_DEPENDS "${cody_boot_pcm}")
    target_sources(${COMPONENT_LIB} PRIVATE "${cody_boot_assembly}")
endif()

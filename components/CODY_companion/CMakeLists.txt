set(cody_jelly_sources "")
if(CONFIG_LUNASHELL_CODY_RENDER_PEARL_JELLY)
    list(APPEND cody_jelly_sources
        "src/rendering/CODY_jelly.cpp"
        "src/rendering/CODY_jelly_simd_s3.S"
        "src/diagnostics/CODY_jelly_simd_validation.cpp"
        "../../assets/cody/appearance/jelly-scanline/generated/cody_jelly_data.cpp"
    )
endif()

set(cody_onboarding_sources "")
set(cody_debug_sources "")
if(CONFIG_LUNASHELL_BUILD_MODE_DEBUG AND CONFIG_LUNASHELL_BUILD_PROFILE_CODY_COMPANION)
    list(APPEND cody_debug_sources "src/diagnostics/CODY_debug_views.cpp")
endif()
if(CONFIG_LUNASHELL_BUILD_PROFILE_CODY_COMPANION)
    list(APPEND cody_onboarding_sources "src/scenes/CODY_onboarding.cpp" "src/scenes/CODY_look_screen.cpp")
endif()

idf_component_register(
    SRCS
        "src/runtime/CODY_companion.cpp"
        "src/scenes/CODY_scene.cpp"
        "src/scenes/CODY_technical_panels.cpp"
        "src/runtime/CODY_lifecycle.cpp"
        "src/model/CODY_expression_catalog.cpp"
        "src/model/CODY_connection_personality.cpp"
        "src/runtime/CODY_settings.cpp"
        "src/runtime/CODY_capture_sound.cpp"
        "src/runtime/CODY_personality_audio.cpp"
        "src/runtime/CODY_codex_audio.cpp"
        "../../assets/cody/personality-icons/audio/personality_audio.cpp"
        "../../assets/cody/audio/codex-actions/generated/codex_audio_assets.cpp"
        "src/scenes/CODY_typewriter_audio.cpp"
        "src/diagnostics/CODY_screenshot.cpp"
        "src/diagnostics/CODY_status_overlay.cpp"
        "src/diagnostics/CODY_debug_hud.cpp"
        "src/rendering/CODY_damage.cpp"
        "src/rendering/CODY_personality.cpp"
        "../../assets/cody/personality-icons/generated/personality_icons.cpp"
        "src/rendering/CODY_dirty_plan.cpp"
        "src/model/CODY_rig.cpp"
        "src/model/CODY_response.cpp"
        "src/model/CODY_petting.cpp"
        "src/model/CODY_happiness.cpp"
        "src/model/CODY_wellbeing.cpp"
        "src/model/CODY_response_inputs.cpp"
        "src/diagnostics/CODY_benchmark.cpp"
        "src/diagnostics/CODY_raster_benchmark.cpp"
        "src/demos/CODY_demo.cpp"
        "src/runtime/CODY_boot_cues.cpp"
        "../LUNASHELL_boot/src/ui/VIBEWARE_boot_animation.c"
        "../../assets/branding/VibeWare/generated/vibeware_boot_data.c"
        "../../assets/cody/boot/generated/cody_sleep_pearl.cpp"
        "../../assets/cody/boot/generated/cody_waking_pearl.cpp"
        "../../assets/cody/boot/generated/cody_ready_pearl.cpp"
        "../../assets/cody/scenes/generated/scene_font.cpp"
        "../../assets/cody/scenes/generated/scene_body_font.cpp"
        ${cody_jelly_sources}
        ${cody_onboarding_sources}
        ${cody_debug_sources}
    INCLUDE_DIRS "include" "../../assets/cody/personality/generated"
        "../../assets/cody/audio/codex-actions/generated"
    PRIV_INCLUDE_DIRS "../../assets/cody/boot/generated" "../../assets/cody/rig/generated" "../../assets/cody/runtime/generated"
        "../../assets/cody/scenes/generated"
        "../../assets/cody/appearance/jelly-scanline/generated"
        "../../assets/cody/personality-icons/generated"
        "../../assets/cody/personality-icons/audio"
        "../LUNASHELL_boot/include"
        "../../assets/branding/VibeWare/generated"
    REQUIRES CODY2D LUNASHELL_platform heap
    PRIV_REQUIRES esp32_s3_touch_amoled_1_75 esp_timer esp_system
)

if(NOT CMAKE_BUILD_EARLY_EXPANSION AND CONFIG_LUNASHELL_BUILD_PROFILE_CODY_COMPANION)
    # optional_requires matches admitted managed identities, not registry aliases.
    idf_component_optional_requires(PRIVATE espressif__qrcode)
endif()

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
if(CONFIG_LUNASHELL_CODY_RENDER_PEARL_JELLY)
    if(NOT IDF_TARGET STREQUAL "esp32s3")
        message(FATAL_ERROR "The native Pearl SIMD kernel requires ESP32-S3")
    endif()
    target_compile_definitions(${COMPONENT_LIB} PUBLIC CODY_ENABLE_PEARL_JELLY=1)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC CODY_JELLY_SIMD_AVAILABLE=1)
else()
    target_compile_definitions(${COMPONENT_LIB} PUBLIC CODY_ENABLE_PEARL_JELLY=0)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC CODY_JELLY_SIMD_AVAILABLE=0)
endif()
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    # IDF's binary embed emits byte-aligned sections; PCM16 requires aligned samples
    # regardless of the size/order of preceding font and artwork resources.
    set(cody_boot_pcm_source "${CMAKE_CURRENT_BINARY_DIR}/cody_boot_pcm.S")
    configure_file("src/runtime/CODY_boot_pcm.S.in" "${cody_boot_pcm_source}" @ONLY)
    set_source_files_properties("${cody_boot_pcm_source}" PROPERTIES OBJECT_DEPENDS
        "${PROJECT_DIR}/assets/cody/runtime/generated/cody-audio_ready.pcm16le;${PROJECT_DIR}/assets/cody/runtime/generated/cody-eyes_open.pcm16le")
    target_sources(${COMPONENT_LIB} PRIVATE "${cody_boot_pcm_source}")
endif()

set(cody_jelly_sources "")
if(CONFIG_LUNASHELL_CODY_RENDER_PEARL_JELLY)
    list(APPEND cody_jelly_sources
        "src/rendering/CODY_jelly.cpp"
        "../../assets/cody/appearance/jelly-scanline/generated/cody_jelly_data.cpp"
    )
endif()

set(cody_onboarding_sources "")
if(CONFIG_LUNASHELL_BUILD_PROFILE_CODY_COMPANION)
    list(APPEND cody_onboarding_sources "src/scenes/CODY_onboarding.cpp" "src/scenes/CODY_look_screen.cpp")
endif()

idf_component_register(
    SRCS
        "src/runtime/CODY_companion.cpp"
        "src/scenes/CODY_scene.cpp"
        "src/runtime/CODY_lifecycle.cpp"
        "src/model/CODY_expression_catalog.cpp"
        "src/runtime/CODY_settings.cpp"
        "src/runtime/CODY_capture_sound.cpp"
        "src/scenes/CODY_typewriter_audio.cpp"
        "src/diagnostics/CODY_screenshot.cpp"
        "src/diagnostics/CODY_status_overlay.cpp"
        "src/diagnostics/CODY_debug_hud.cpp"
        "src/rendering/CODY_damage.cpp"
        "src/rendering/CODY_dirty_plan.cpp"
        "src/model/CODY_rig.cpp"
        "src/model/CODY_response.cpp"
        "src/model/CODY_response_inputs.cpp"
        "src/diagnostics/CODY_benchmark.cpp"
        "src/demos/CODY_demo.cpp"
        "src/runtime/CODY_boot_cues.cpp"
        "../../assets/cody/boot/generated/cody_sleep_pearl.cpp"
        "../../assets/cody/boot/generated/cody_waking_pearl.cpp"
        "../../assets/cody/boot/generated/cody_ready_pearl.cpp"
        "../../assets/cody/scenes/generated/scene_font.cpp"
        "../../assets/cody/scenes/generated/scene_body_font.cpp"
        ${cody_jelly_sources}
        ${cody_onboarding_sources}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "../../assets/cody/boot/generated" "../../assets/cody/rig/generated" "../../assets/cody/runtime/generated"
        "../../assets/cody/scenes/generated"
        "../../assets/cody/appearance/jelly-scanline/generated"
    REQUIRES CODY2D LUNASHELL_platform heap
    PRIV_REQUIRES esp32_s3_touch_amoled_1_75 esp_timer esp_system
)

if(NOT CMAKE_BUILD_EARLY_EXPANSION AND CONFIG_LUNASHELL_BUILD_PROFILE_CODY_COMPANION)
    # optional_requires matches admitted managed identities, not registry aliases.
    idf_component_optional_requires(PRIVATE espressif__qrcode)
endif()

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
if(CONFIG_LUNASHELL_CODY_RENDER_PEARL_JELLY)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC CODY_ENABLE_PEARL_JELLY=1)
else()
    target_compile_definitions(${COMPONENT_LIB} PUBLIC CODY_ENABLE_PEARL_JELLY=0)
endif()
target_add_binary_data(${COMPONENT_LIB} "${PROJECT_DIR}/assets/cody/runtime/generated/cody-audio_ready.pcm16le" BINARY RENAME_TO cody_audio_ready)
target_add_binary_data(${COMPONENT_LIB} "${PROJECT_DIR}/assets/cody/runtime/generated/cody-eyes_open.pcm16le" BINARY RENAME_TO cody_eyes_open)

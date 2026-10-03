set(cody_jelly_sources "")
if(CONFIG_LUNASHELL_CODY_RENDER_PEARL_JELLY)
    list(APPEND cody_jelly_sources
        "CODY_jelly.cpp"
        "../../assets/cody/experiments/jelly-scanline/generated/cody_jelly_data.cpp"
    )
endif()

idf_component_register(
    SRCS
        "CODY_companion.cpp"
        "CODY_scene.cpp"
        "CODY_settings.cpp"
        "CODY_debug_hud.cpp"
        "CODY_damage.cpp"
        "CODY_dirty_plan.cpp"
        "CODY_rig.cpp"
        "CODY_response.cpp"
        "CODY_response_inputs.cpp"
        "CODY_benchmark.cpp"
        "CODY_demo.cpp"
        "CODY_poc.cpp"
        "../../assets/cody/boot/generated/cody_sleep_blue.cpp"
        "../../assets/cody/boot/generated/cody_waking_blue.cpp"
        "../../assets/cody/boot/generated/cody_ready_blue.cpp"
        "../../assets/cody/scenes/generated/scene_font.cpp"
        ${cody_jelly_sources}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "../../assets/cody/boot/generated" "../../assets/cody/rig/generated" "../../assets/cody/poc/generated"
        "../../assets/cody/scenes/generated"
        "../../assets/cody/experiments/jelly-scanline/generated"
    REQUIRES CODY2D LUNASHELL_platform heap
    PRIV_REQUIRES esp32_s3_touch_amoled_1_75 esp_timer
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
if(CONFIG_LUNASHELL_CODY_RENDER_PEARL_JELLY)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC CODY_ENABLE_PEARL_JELLY=1)
else()
    target_compile_definitions(${COMPONENT_LIB} PUBLIC CODY_ENABLE_PEARL_JELLY=0)
endif()
target_add_binary_data(${COMPONENT_LIB} "${PROJECT_DIR}/assets/cody/poc/generated/cody-audio_ready.pcm16le" BINARY RENAME_TO cody_audio_ready)
target_add_binary_data(${COMPONENT_LIB} "${PROJECT_DIR}/assets/cody/poc/generated/cody-eyes_open.pcm16le" BINARY RENAME_TO cody_eyes_open)

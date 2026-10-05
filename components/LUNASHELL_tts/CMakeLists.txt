if(CMAKE_BUILD_EARLY_EXPANSION)
    idf_component_register(REQUIRES LUNASHELL_platform esp_timer esp_system heap freertos)
    return()
endif()
if(NOT CONFIG_LUNASHELL_BUILD_ENABLE_TTS)
    message(FATAL_ERROR "TTS component must be excluded from disabled profile roots")
endif()
if(NOT CONFIG_IDF_TARGET_ESP32S3 OR NOT CONFIG_SPIRAM OR
   NOT CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY OR
   NOT CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM OR
   NOT CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311)
    message(FATAL_ERROR "TTS requires ESP32-S3, PSRAM with external BSS and explicitly selected ES8311 output")
endif()
if(CONFIG_FREERTOS_UNICORE)
    message(FATAL_ERROR "TTS must execute on a separate core from the renderer")
endif()
if(CONFIG_LUNASHELL_TTS_BOOT_TEST_ITALIAN AND
   (NOT CONFIG_LUNASHELL_TTS_DATA_SD OR NOT CONFIG_LUNASHELL_TTS_LANGUAGE_ITALIAN))
    message(FATAL_ERROR "Italian boot speech requires the explicitly selected SD/Italian package")
endif()
if(CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD OR CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_AFE)
    message(FATAL_ERROR "TTS plus wake-word/AFE is not an admitted clock/ownership combination")
endif()
if(CONFIG_LUNASHELL_TTS_DATA_FLASH)
    if(NOT CONFIG_LUNASHELL_TTS_LANGUAGE_ENGLISH OR
       CONFIG_LUNASHELL_TTS_LANGUAGE_ITALIAN OR CONFIG_LUNASHELL_TTS_LANGUAGE_MANDARIN OR
       CONFIG_LUNASHELL_TTS_LANGUAGE_CANTONESE)
        message(FATAL_ERROR "Flash TTS supports English only")
    endif()
    set(tts_backend flash)
elseif(CONFIG_LUNASHELL_TTS_DATA_SD AND CONFIG_LUNASHELL_BUILD_ENABLE_REMOVABLE_STORAGE)
    if(NOT CONFIG_MBEDTLS_SHA256_C)
        message(FATAL_ERROR "SD speech package verification requires IDF SHA-256 support")
    endif()
    set(tts_backend sd)
else()
    message(FATAL_ERROR "Select exactly one supported TTS storage backend; SD requires its owner")
endif()
if(tts_backend STREQUAL "sd" AND
   (NOT CONFIG_LUNASHELL_TTS_LANGUAGE_ENGLISH OR NOT CONFIG_LUNASHELL_TTS_LANGUAGE_ITALIAN OR
    NOT CONFIG_LUNASHELL_TTS_LANGUAGE_MANDARIN OR NOT CONFIG_LUNASHELL_TTS_LANGUAGE_CANTONESE))
    message(FATAL_ERROR "The reviewed SD package requires English, Italian, Mandarin and Cantonese")
endif()

set(tts_vendor "${CMAKE_CURRENT_LIST_DIR}/../../third_party/espeak-ng")
set(tts_core_names common dictionary encoding error espeak_api ieee80 intonation
    klatt langopts mnemonics numbers phoneme phonemelist readclause setlengths
    soundicon spect speech ssml synth_mbrola synthdata synthesize tr_languages
    translate translateword voices wavegen)
set(tts_sources src/LUNASHELL_tts.cpp src/native_assets.c src/native_memory.c src/native_engine.c src/native_text.c)
foreach(name IN LISTS tts_core_names)
    list(APPEND tts_sources "${tts_vendor}/libespeak-ng/${name}.c")
endforeach()
foreach(name case categories ctype proplist scripts tostring)
    list(APPEND tts_sources "${tts_vendor}/ucd/${name}.c")
endforeach()
set(tts_generated "${CMAKE_CURRENT_BINARY_DIR}/tts-${tts_backend}")
set(tts_generated_source "${tts_generated}/tts_assets.cpp")
set(tts_package_outputs)
if(tts_backend STREQUAL "sd")
    foreach(path phondata phontab phonindex intonations en_dict it_dict cmn_dict yue_dict
                 lang/gmw/en lang/roa/it lang/sit/cmn lang/sit/yue manifest.json)
        list(APPEND tts_package_outputs "${tts_generated}/sd-root/LUNASHELL/tts/espeak-ng-data/${path}")
    endforeach()
endif()
file(GLOB_RECURSE tts_asset_inputs "${CMAKE_CURRENT_LIST_DIR}/../../assets/tts/espeak-ng/*")
idf_build_get_property(tts_python PYTHON)
add_custom_command(OUTPUT "${tts_generated_source}"
    COMMAND "${tts_python}" "${CMAKE_CURRENT_LIST_DIR}/../../scripts/generators/generate_tts_assets.py"
        --backend "${tts_backend}" --output "${tts_generated}"
    BYPRODUCTS ${tts_package_outputs}
    DEPENDS ${tts_asset_inputs} "${CMAKE_CURRENT_LIST_DIR}/../../scripts/generators/generate_tts_assets.py"
    VERBATIM)
list(APPEND tts_sources "${tts_generated_source}")
idf_component_register(SRCS ${tts_sources}
    LDFRAGMENTS linker.lf
    INCLUDE_DIRS include
    PRIV_INCLUDE_DIRS src "${tts_vendor}" "${tts_vendor}/libespeak-ng" "${tts_vendor}/ucd/include"
    REQUIRES LUNASHELL_platform esp_timer esp_system heap freertos)

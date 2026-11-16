# Experimental CMake-first Rust component, included only by explicit opt-in.
# The environment contract follows the esp-rs ESP-IDF component template and
# reuses the enclosing project's sdkconfig and compiler rather than building a
# second, independently configured ESP-IDF.
# esp-idf-hal 0.47 compiles its ESP32-S3 peripheral modules even when the
# WhatsApp client does not use those peripherals. List the corresponding IDF 6
# driver components so esp-idf-sys sees both their headers and component cfgs.
idf_component_register(
    SRCS "placeholder.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES pthread driver esp_adc sdmmc esp_http_client esp-tls nvs_flash spi_flash
        bt esp_timer lwip mbedtls vfs
        esp_driver_gpio esp_driver_gptimer esp_driver_i2c esp_driver_i2s
        esp_driver_ledc esp_driver_pcnt esp_driver_rmt esp_driver_sdmmc
        esp_driver_sdspi esp_driver_spi esp_driver_touch_sens esp_driver_twai esp_driver_uart
        esp_driver_usb_serial_jtag
)

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    if(NOT CONFIG_IDF_TARGET_ESP32S3 OR NOT IDF_VERSION_MAJOR EQUAL 6 OR NOT IDF_VERSION_MINOR EQUAL 0)
        message(FATAL_ERROR "Cody WhatsApp experiment requires ESP32-S3 and ESP-IDF 6.0.x")
    endif()
    include(ExternalProject)
    idf_build_get_property(whatsapp_sdkconfig SDKCONFIG)
    idf_build_get_property(whatsapp_idf_path IDF_PATH)
    set(whatsapp_target xtensa-esp32s3-espidf)
    set(whatsapp_target_dir "${CMAKE_CURRENT_BINARY_DIR}/cargo")
    # The device build selects its profile with LUNASHELL_BUILD_MODE; it does
    # not set CMAKE_BUILD_TYPE when invoking ESP-IDF.
    if(LUNASHELL_BUILD_MODE STREQUAL "Debug")
        set(whatsapp_profile debug)
    else()
        set(whatsapp_profile release)
    endif()
    set(whatsapp_library "${whatsapp_target_dir}/${whatsapp_target}/${whatsapp_profile}/libcody_whatsapp_prototype.a")
    # Passing these lists on the ExternalProject command line exceeds cmd.exe's
    # length limit on Windows. Generate their evaluated values in the build tree
    # and let the short CMake script put them directly in Cargo's environment.
    set(whatsapp_includes_file "${CMAKE_CURRENT_BINARY_DIR}/cargo-includes.txt")
    set(whatsapp_links_file "${CMAKE_CURRENT_BINARY_DIR}/cargo-link-libraries.txt")
    file(GENERATE OUTPUT "${whatsapp_includes_file}"
        CONTENT "$<TARGET_PROPERTY:${COMPONENT_LIB},INCLUDE_DIRECTORIES>")
    file(GENERATE OUTPUT "${whatsapp_links_file}"
        CONTENT "$<TARGET_PROPERTY:${COMPONENT_LIB},LINK_LIBRARIES>")
    ExternalProject_Add(cody_whatsapp_cargo
        PREFIX "${CMAKE_CURRENT_BINARY_DIR}/external"
        SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}"
        BINARY_DIR "${CMAKE_CURRENT_LIST_DIR}"
        DOWNLOAD_COMMAND ""
        CONFIGURE_COMMAND ""
        BUILD_COMMAND "${CMAKE_COMMAND}"
            "-DWHATSAPP_INCLUDES_FILE=${whatsapp_includes_file}"
            "-DWHATSAPP_LINKS_FILE=${whatsapp_links_file}"
            "-DWHATSAPP_SDKCONFIG=${whatsapp_sdkconfig}"
            "-DWHATSAPP_IDF_PATH=${whatsapp_idf_path}"
            "-DWHATSAPP_COMPILER=${CMAKE_C_COMPILER}"
            "-DWHATSAPP_TARGET=${whatsapp_target}"
            "-DWHATSAPP_TARGET_DIR=${whatsapp_target_dir}"
            "-DWHATSAPP_PROFILE=${whatsapp_profile}"
            "-DWHATSAPP_SOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}"
            -P "${CMAKE_CURRENT_LIST_DIR}/build_cargo.cmake"
        INSTALL_COMMAND ""
        BUILD_ALWAYS TRUE
        BUILD_BYPRODUCTS "${whatsapp_library}"
    )
    add_prebuilt_library(cody_whatsapp_rust "${whatsapp_library}")
    add_dependencies(cody_whatsapp_rust cody_whatsapp_cargo)
    target_link_libraries(${COMPONENT_LIB} PRIVATE cody_whatsapp_rust)
endif()

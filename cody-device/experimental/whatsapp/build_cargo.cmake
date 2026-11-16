# Run Cargo with the enclosing ESP-IDF build's metadata without placing the
# potentially very long include and link lists on a Windows cmd.exe command line.
file(READ "${WHATSAPP_INCLUDES_FILE}" whatsapp_includes)
file(READ "${WHATSAPP_LINKS_FILE}" whatsapp_links)
set(ENV{CARGO_CMAKE_BUILD_INCLUDES} "${whatsapp_includes}")
set(ENV{CARGO_CMAKE_BUILD_LINK_LIBRARIES} "${whatsapp_links}")
set(ENV{CARGO_CMAKE_BUILD_SDKCONFIG} "${WHATSAPP_SDKCONFIG}")
set(ENV{CARGO_CMAKE_BUILD_ESP_IDF} "${WHATSAPP_IDF_PATH}")
set(ENV{CARGO_CMAKE_BUILD_COMPILER} "${WHATSAPP_COMPILER}")
set(ENV{RUSTFLAGS} "--cfg espidf_time64")
set(ENV{MCU} "esp32s3")

# Bindgen needs the Xtensa-enabled libclang and Windows must also find its
# sibling DLLs. Resolve the active `esp` toolchain instead of hard-coding a
# user profile or mutating the machine environment. Honour explicit overrides.
if(CMAKE_HOST_WIN32)
    if(NOT DEFINED ENV{LIBCLANG_PATH} OR "$ENV{LIBCLANG_PATH}" STREQUAL "")
        execute_process(
            COMMAND rustup run esp rustc --print sysroot
            RESULT_VARIABLE whatsapp_rustup_result
            OUTPUT_VARIABLE whatsapp_rust_sysroot
            ERROR_VARIABLE whatsapp_rustup_error
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(NOT whatsapp_rustup_result STREQUAL "0" OR whatsapp_rust_sysroot STREQUAL "")
            message(FATAL_ERROR "Could not locate the esp Rust toolchain: ${whatsapp_rustup_error}")
        endif()
        file(TO_CMAKE_PATH "${whatsapp_rust_sysroot}" whatsapp_rust_sysroot)
        set(whatsapp_clang_dir "${whatsapp_rust_sysroot}/xtensa-esp32-elf-clang/esp-clang/bin")
        if(NOT EXISTS "${whatsapp_clang_dir}/libclang.dll")
            message(FATAL_ERROR "esp toolchain has no libclang.dll in ${whatsapp_clang_dir}; set LIBCLANG_PATH to the Xtensa-enabled library")
        endif()
        set(ENV{LIBCLANG_PATH} "${whatsapp_clang_dir}")
    elseif(IS_DIRECTORY "$ENV{LIBCLANG_PATH}")
        file(TO_CMAKE_PATH "$ENV{LIBCLANG_PATH}" whatsapp_clang_dir)
    else()
        get_filename_component(whatsapp_clang_dir "$ENV{LIBCLANG_PATH}" DIRECTORY)
    endif()
    # esp-idf-sys otherwise may replace an explicit path with its Unix symlink.
    if(NOT DEFINED ENV{ESP_IDF_ESPUP_CLANG_SYMLINK})
        set(ENV{ESP_IDF_ESPUP_CLANG_SYMLINK} "ignore")
    endif()
    set(ENV{PATH} "${whatsapp_clang_dir};$ENV{PATH}")
endif()

set(whatsapp_profile_arg)
if(WHATSAPP_PROFILE STREQUAL "release")
    list(APPEND whatsapp_profile_arg --release)
elseif(NOT WHATSAPP_PROFILE STREQUAL "debug")
    message(FATAL_ERROR "Unsupported WhatsApp Cargo profile: ${WHATSAPP_PROFILE}")
endif()

execute_process(
    COMMAND cargo +esp build --target "${WHATSAPP_TARGET}"
        --target-dir "${WHATSAPP_TARGET_DIR}" ${whatsapp_profile_arg}
        -Zbuild-std=std,panic_abort
    WORKING_DIRECTORY "${WHATSAPP_SOURCE_DIR}"
    RESULT_VARIABLE whatsapp_cargo_result
)
if(NOT whatsapp_cargo_result STREQUAL "0")
    message(FATAL_ERROR "WhatsApp Cargo build failed: ${whatsapp_cargo_result}")
endif()

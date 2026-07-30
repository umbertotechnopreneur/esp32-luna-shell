cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(distanziamoci)

# `main` creates the LittleFS target before qsee_app can replace the raw
# Brookesia staging target. Add the final cross-component dependency here,
# after ESP-IDF has configured every UI component.
if(CONFIG_QSEE_BUILD_ENABLE_UI)
    if(NOT TARGET littlefs_littlefs_data_bin)
        message(FATAL_ERROR "LittleFS image target was not created")
    endif()
    if(NOT TARGET qsee_prepare_runtime_app_package)
        message(FATAL_ERROR "QSee runtime package preparation target was not created")
    endif()
    add_dependencies(
        littlefs_littlefs_data_bin
        qsee_prepare_runtime_app_package
    )
endif()

# Keep the Brookesia C++ runtime compact under the firmware's -O2 build,
# matching the upstream System Super example integration.
idf_build_get_property(brookesia_build_components BUILD_COMPONENTS)
foreach(brookesia_component ${brookesia_build_components})
    if(brookesia_component MATCHES "brookesia")
        set(brookesia_library "__idf_${brookesia_component}")
        if(TARGET ${brookesia_library})
            get_target_property(brookesia_library_type ${brookesia_library} TYPE)
            if(NOT brookesia_library_type STREQUAL "INTERFACE_LIBRARY")
                target_compile_options(
                    ${brookesia_library}
                    PRIVATE
                        -fno-ipa-cp-clone
                        -fno-ipa-sra
                )
            endif()
        endif()
    endif()
endforeach()

cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(distanziamoci)

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

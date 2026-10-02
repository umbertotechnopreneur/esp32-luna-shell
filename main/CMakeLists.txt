set(LUNASHELL_main_srcs)
set(LUNASHELL_main_requires
    LUNASHELL_logger
    LUNASHELL_platform
)

if(CONFIG_LUNASHELL_BUILD_ENABLE_REMOVABLE_STORAGE)
    list(APPEND LUNASHELL_main_requires fatfs)
endif()
if(CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY OR CONFIG_LUNASHELL_BUILD_ENABLE_TOUCH)
    list(APPEND LUNASHELL_main_requires esp32_s3_touch_amoled_1_75)
endif()
if(CONFIG_LUNASHELL_BUILD_PROFILE_CODY_COMPANION)
    list(APPEND LUNASHELL_main_requires CODY_companion)
endif()
if(CONFIG_LUNASHELL_BUILD_ENABLE_UI)
    list(APPEND LUNASHELL_main_srcs "main.cpp")
    list(APPEND LUNASHELL_main_requires
        littlefs
        brookesia_hal_adaptor
        brookesia_gui_lvgl
        brookesia_lib_utils
        brookesia_system_core
        esp32_s3_touch_amoled_1_75
        LUNASHELL_app
    )
    if(CONFIG_LUNASHELL_BUILD_ENABLE_BOOT_UI OR CONFIG_LUNASHELL_BUILD_ENABLE_BOOT_AUDIO)
        list(APPEND LUNASHELL_main_requires LUNASHELL_boot)
    endif()
else()
    list(APPEND LUNASHELL_main_srcs "main_headless.cpp")
endif()

idf_component_register(
    SRCS ${LUNASHELL_main_srcs}
    INCLUDE_DIRS "."
    REQUIRES ${LUNASHELL_main_requires}
)

# ESP-IDF discovers the component graph before Kconfig values are available.
# Attach profile-specific public dependencies in the normal evaluation pass so
# the full build receives their include and link interfaces.
if(CONFIG_LUNASHELL_BUILD_ENABLE_REMOVABLE_STORAGE)
    idf_component_optional_requires(PUBLIC fatfs)
endif()
if(CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY OR CONFIG_LUNASHELL_BUILD_ENABLE_TOUCH)
    idf_component_optional_requires(PUBLIC esp32_s3_touch_amoled_1_75)
endif()
if(CONFIG_LUNASHELL_BUILD_PROFILE_CODY_COMPANION)
    idf_component_optional_requires(PUBLIC CODY_companion)
endif()
if(CONFIG_LUNASHELL_BUILD_ENABLE_UI)
    idf_component_optional_requires(
        PUBLIC
        littlefs
        brookesia_hal_adaptor
        brookesia_gui_lvgl
        brookesia_lib_utils
        brookesia_system_core
        esp32_s3_touch_amoled_1_75
        LUNASHELL_app
    )
    if(CONFIG_LUNASHELL_BUILD_ENABLE_BOOT_UI OR CONFIG_LUNASHELL_BUILD_ENABLE_BOOT_AUDIO)
        idf_component_optional_requires(PUBLIC LUNASHELL_boot)
    endif()
endif()

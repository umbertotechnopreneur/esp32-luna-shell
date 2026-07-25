idf_component_register(
    SRCS
        "qsee_adaptive_command_bar.cpp"
        "qsee_app.cpp"
        "qsee_background_asset.cpp"
        "qsee_edge_navigation.cpp"
        "qsee_navigation.cpp"
        "qsee_status_bar.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        brookesia_core
        esp_hw_support
        qsee_boot
        qsee_logger
        qsee_platform
        qrcode
    WHOLE_ARCHIVE
)

set_source_files_properties(
    qsee_adaptive_command_bar.cpp
    qsee_app.cpp
    qsee_background_asset.cpp
    qsee_edge_navigation.cpp
    qsee_navigation.cpp
    qsee_status_bar.cpp
    PROPERTIES COMPILE_FLAGS "-Wno-missing-field-initializers"
)

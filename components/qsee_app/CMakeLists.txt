idf_component_register(
    SRCS
        "qsee_app.cpp"
        "qsee_navigation.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        brookesia_core
        qsee_boot
        qsee_logger
        qsee_platform
    WHOLE_ARCHIVE
)

set_source_files_properties(
    qsee_app.cpp
    qsee_navigation.cpp
    PROPERTIES COMPILE_FLAGS "-Wno-missing-field-initializers"
)

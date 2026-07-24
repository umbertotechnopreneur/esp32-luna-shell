idf_component_register(
    SRCS
        "qsee_app.cpp"
        "qsee_navigation.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        brookesia_core
        qsee_logger
    WHOLE_ARCHIVE
)

set_source_files_properties(
    qsee_app.cpp
    qsee_navigation.cpp
    PROPERTIES COMPILE_FLAGS "-Wno-missing-field-initializers"
)

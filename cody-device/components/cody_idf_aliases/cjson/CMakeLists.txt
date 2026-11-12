# ESP-SR's public CMake requires the short name cjson, while its locked
# registry dependency is espressif__cjson. With the IDF component manager
# disabled to preserve other profiles, expose that dependency through a normal
# interface component instead of modifying the managed source.
idf_component_register(REQUIRES espressif__cjson)

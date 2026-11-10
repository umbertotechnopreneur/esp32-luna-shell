idf_component_register(
    SRCS "src/CODY_state_receive.cpp"
    INCLUDE_DIRS "include"
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

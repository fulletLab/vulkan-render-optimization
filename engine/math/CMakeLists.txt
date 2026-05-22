add_library(projectunity_math INTERFACE)
add_library(ProjectUnity::Math ALIAS projectunity_math)

target_include_directories(projectunity_math
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(projectunity_math INTERFACE cxx_std_20)

add_library(projectunity_core STATIC
    src/Log.cpp
)

add_library(ProjectUnity::Core ALIAS projectunity_core)

target_include_directories(projectunity_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

projectunity_configure_target(projectunity_core)

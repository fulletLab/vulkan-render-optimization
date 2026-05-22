add_library(projectunity_debug STATIC
    src/DebugDraw.cpp
    include/projectunity/debug/DebugDraw.hpp
)

add_library(ProjectUnity::Debug ALIAS projectunity_debug)

target_include_directories(projectunity_debug
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(projectunity_debug
    PUBLIC
        ProjectUnity::Core
        ProjectUnity::Math
)

projectunity_configure_target(projectunity_debug)

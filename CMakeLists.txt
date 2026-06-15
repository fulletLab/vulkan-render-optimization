cmake_minimum_required(VERSION 3.26)

project(ProjectUnity
    VERSION 0.1.0
    DESCRIPTION "Modular C++ engine and Unity-like editor foundation"
    LANGUAGES C CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(ProjectUnityOptions)

if(PROJECTUNITY_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(engine/core)
add_subdirectory(engine/math)
add_subdirectory(engine/debug)
add_subdirectory(engine/terrain)
add_subdirectory(engine/scene)
add_subdirectory(engine/scripting)
add_subdirectory(engine/assets)
add_subdirectory(engine/renderer)

if(PROJECTUNITY_BUILD_EDITOR)
    add_subdirectory(editor/viewport)
    add_subdirectory(editor/app)
endif()

if(PROJECTUNITY_BUILD_TESTS)
    add_subdirectory(tests)
endif()

include(FetchContent)

FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

add_library(projectunity_scene STATIC
    src/Scene.cpp
    src/SceneSerialization.cpp
)

add_library(ProjectUnity::Scene ALIAS projectunity_scene)

target_include_directories(projectunity_scene
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(projectunity_scene
    PUBLIC
        ProjectUnity::Core
        ProjectUnity::Math
    PRIVATE
        nlohmann_json::nlohmann_json
)

projectunity_configure_target(projectunity_scene)

add_executable(projectunity_scene_tests
    scene_tests.cpp
)

target_link_libraries(projectunity_scene_tests
    PRIVATE
        ProjectUnity::Scene
)

target_compile_definitions(projectunity_scene_tests
    PRIVATE
        PROJECTUNITY_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

projectunity_configure_target(projectunity_scene_tests)

add_test(NAME projectunity_scene_tests COMMAND projectunity_scene_tests)

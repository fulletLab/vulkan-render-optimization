add_executable(projectunity_editor_viewport_tests
    editor_viewport_tests.cpp
)

target_include_directories(projectunity_editor_viewport_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/editor/viewport/src
)

target_link_libraries(projectunity_editor_viewport_tests
    PRIVATE
        ProjectUnity::EditorViewport
)

projectunity_configure_target(projectunity_editor_viewport_tests)

add_test(NAME projectunity_editor_viewport_tests COMMAND projectunity_editor_viewport_tests)

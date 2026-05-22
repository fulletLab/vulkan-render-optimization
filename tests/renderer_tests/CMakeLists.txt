add_executable(projectunity_renderer_tests
    renderer_tests.cpp
)

target_link_libraries(projectunity_renderer_tests
    PRIVATE
        ProjectUnity::Renderer
)

projectunity_configure_target(projectunity_renderer_tests)

add_test(NAME projectunity_renderer_tests COMMAND projectunity_renderer_tests)

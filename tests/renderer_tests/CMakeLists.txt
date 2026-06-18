add_executable(projectunity_renderer_tests
    renderer_tests.cpp
)

find_package(Vulkan REQUIRED)

target_include_directories(projectunity_renderer_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/engine/renderer/src
)

target_link_libraries(projectunity_renderer_tests
    PRIVATE
        ProjectUnity::Renderer
        Vulkan::Vulkan
)

projectunity_configure_target(projectunity_renderer_tests)

add_test(NAME projectunity_renderer_tests COMMAND projectunity_renderer_tests)

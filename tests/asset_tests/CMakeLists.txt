add_executable(projectunity_asset_tests
    asset_tests.cpp
)
add_executable(projectunity_sidecar_texture_tests
    sidecar_texture_tests.cpp
)

target_link_libraries(projectunity_asset_tests
    PRIVATE
        ProjectUnity::Assets
)
target_link_libraries(projectunity_sidecar_texture_tests
    PRIVATE
        ProjectUnity::Assets
)

target_compile_definitions(projectunity_asset_tests
    PRIVATE
        PROJECTUNITY_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)

projectunity_configure_target(projectunity_asset_tests)
projectunity_configure_target(projectunity_sidecar_texture_tests)

add_test(NAME projectunity_asset_tests COMMAND projectunity_asset_tests)
add_test(NAME projectunity_sidecar_texture_tests COMMAND projectunity_sidecar_texture_tests)

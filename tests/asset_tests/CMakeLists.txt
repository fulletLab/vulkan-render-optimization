add_executable(projectunity_asset_tests
    asset_tests.cpp
)

target_link_libraries(projectunity_asset_tests
    PRIVATE
        ProjectUnity::Assets
)

target_compile_definitions(projectunity_asset_tests
    PRIVATE
        PROJECTUNITY_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)

projectunity_configure_target(projectunity_asset_tests)

add_test(NAME projectunity_asset_tests COMMAND projectunity_asset_tests)

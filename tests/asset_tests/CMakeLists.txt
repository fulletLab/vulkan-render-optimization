add_executable(projectunity_asset_tests
    asset_tests.cpp
)
add_executable(projectunity_sidecar_texture_tests
    sidecar_texture_tests.cpp
)
add_executable(projectunity_spatial_batch_tests
    spatial_batch_tests.cpp
)
add_executable(projectunity_material_graph_tests
    material_graph_tests.cpp
)
add_executable(projectunity_generated_model_tests
    generated_model_tests.cpp
)

target_link_libraries(projectunity_asset_tests
    PRIVATE
        ProjectUnity::Assets
)
target_link_libraries(projectunity_sidecar_texture_tests
    PRIVATE
        ProjectUnity::Assets
)
target_link_libraries(projectunity_spatial_batch_tests
    PRIVATE
        ProjectUnity::Assets
)
target_link_libraries(projectunity_material_graph_tests
    PRIVATE
        ProjectUnity::Assets
)
target_link_libraries(projectunity_generated_model_tests
    PRIVATE
        ProjectUnity::Assets
)

target_compile_definitions(projectunity_asset_tests
    PRIVATE
        PROJECTUNITY_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)

projectunity_configure_target(projectunity_asset_tests)
projectunity_configure_target(projectunity_sidecar_texture_tests)
projectunity_configure_target(projectunity_spatial_batch_tests)
projectunity_configure_target(projectunity_material_graph_tests)
projectunity_configure_target(projectunity_generated_model_tests)

add_test(NAME projectunity_asset_tests COMMAND projectunity_asset_tests)
add_test(NAME projectunity_sidecar_texture_tests COMMAND projectunity_sidecar_texture_tests)
add_test(NAME projectunity_spatial_batch_tests COMMAND projectunity_spatial_batch_tests)
add_test(NAME projectunity_material_graph_tests COMMAND projectunity_material_graph_tests)
add_test(NAME projectunity_generated_model_tests COMMAND projectunity_generated_model_tests)

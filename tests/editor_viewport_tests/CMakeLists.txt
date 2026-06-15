find_package(Qt6 6.5 QUIET COMPONENTS Widgets)

get_target_property(PROJECTUNITY_EDITOR_TEST_QT_CORE_LOCATION Qt6::Core IMPORTED_LOCATION_DEBUG)
if(NOT PROJECTUNITY_EDITOR_TEST_QT_CORE_LOCATION)
    get_target_property(PROJECTUNITY_EDITOR_TEST_QT_CORE_LOCATION Qt6::Core IMPORTED_LOCATION)
endif()
get_filename_component(PROJECTUNITY_EDITOR_TEST_QT_BIN_DIR "${PROJECTUNITY_EDITOR_TEST_QT_CORE_LOCATION}" DIRECTORY)
get_filename_component(PROJECTUNITY_EDITOR_TEST_QT_PREFIX_DIR "${PROJECTUNITY_EDITOR_TEST_QT_BIN_DIR}/.." ABSOLUTE)

function(projectunity_configure_editor_viewport_test_environment target_name)
    set_tests_properties(${target_name} PROPERTIES
        ENVIRONMENT
            "QT_PLUGIN_PATH=${PROJECTUNITY_EDITOR_TEST_QT_PREFIX_DIR}/plugins;QT_QPA_PLATFORM=offscreen"
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${PROJECTUNITY_EDITOR_TEST_QT_BIN_DIR}"
    )
endfunction()

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
projectunity_configure_editor_viewport_test_environment(projectunity_editor_viewport_tests)

add_executable(projectunity_runtime_cook_tests
    runtime_cook_tests.cpp
)

target_include_directories(projectunity_runtime_cook_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/editor/viewport/src
)

target_link_libraries(projectunity_runtime_cook_tests
    PRIVATE
        ProjectUnity::EditorViewport
)

projectunity_configure_target(projectunity_runtime_cook_tests)

add_test(NAME projectunity_runtime_cook_tests COMMAND projectunity_runtime_cook_tests)
projectunity_configure_editor_viewport_test_environment(projectunity_runtime_cook_tests)

add_executable(projectunity_viewport_picking_tests
    viewport_picking_tests.cpp
)

target_link_libraries(projectunity_viewport_picking_tests
    PRIVATE
        ProjectUnity::EditorViewport
)

projectunity_configure_target(projectunity_viewport_picking_tests)

add_test(NAME projectunity_viewport_picking_tests COMMAND projectunity_viewport_picking_tests)
projectunity_configure_editor_viewport_test_environment(projectunity_viewport_picking_tests)

add_executable(projectunity_project_browser_tests
    project_browser_tests.cpp
    ${PROJECT_SOURCE_DIR}/editor/app/src/ProjectAssetTreeWidget.cpp
    ${PROJECT_SOURCE_DIR}/editor/app/src/ProjectBrowserWidget.cpp
)

target_include_directories(projectunity_project_browser_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/editor/app/include
)

target_link_libraries(projectunity_project_browser_tests
    PRIVATE
        ProjectUnity::Core
        ProjectUnity::Assets
        Qt6::Widgets
)

projectunity_configure_target(projectunity_project_browser_tests)

add_test(NAME projectunity_project_browser_tests COMMAND projectunity_project_browser_tests)
projectunity_configure_editor_viewport_test_environment(projectunity_project_browser_tests)

add_executable(projectunity_scene_hierarchy_tests
    scene_hierarchy_tests.cpp
    ${PROJECT_SOURCE_DIR}/editor/app/src/SceneHierarchyWidget.cpp
)

target_include_directories(projectunity_scene_hierarchy_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/editor/app/include
)

target_link_libraries(projectunity_scene_hierarchy_tests
    PRIVATE
        ProjectUnity::Core
        ProjectUnity::Assets
        ProjectUnity::Scene
        Qt6::Widgets
)

projectunity_configure_target(projectunity_scene_hierarchy_tests)

add_test(NAME projectunity_scene_hierarchy_tests COMMAND projectunity_scene_hierarchy_tests)
projectunity_configure_editor_viewport_test_environment(projectunity_scene_hierarchy_tests)

add_executable(projectunity_viewport_render_world_hlod_tests
    viewport_render_world_hlod_tests.cpp
)

target_include_directories(projectunity_viewport_render_world_hlod_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/editor/viewport/src
)

target_link_libraries(projectunity_viewport_render_world_hlod_tests
    PRIVATE
        ProjectUnity::EditorViewport
)

projectunity_configure_target(projectunity_viewport_render_world_hlod_tests)

add_test(NAME projectunity_viewport_render_world_hlod_tests COMMAND projectunity_viewport_render_world_hlod_tests)
projectunity_configure_editor_viewport_test_environment(projectunity_viewport_render_world_hlod_tests)

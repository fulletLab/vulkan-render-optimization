include(ProjectUnityDependencies)

if(DEFINED ENV{QT_PREFIX} AND NOT "$ENV{QT_PREFIX}" STREQUAL "")
    list(PREPEND CMAKE_PREFIX_PATH "$ENV{QT_PREFIX}")
endif()

if(DEFINED ENV{QT_ROOT} AND NOT "$ENV{QT_ROOT}" STREQUAL "")
    list(PREPEND CMAKE_PREFIX_PATH "$ENV{QT_ROOT}")
endif()

find_package(Qt6 6.5 QUIET COMPONENTS Widgets Concurrent OpenGLWidgets)

if(NOT Qt6_FOUND)
    message(FATAL_ERROR
        "ProjectUnity editor requires Qt 6.5 or newer with the Widgets, Concurrent, and OpenGLWidgets modules. "
        "Set QT_PREFIX or QT_ROOT to the Qt install prefix, pass -DCMAKE_PREFIX_PATH=<qt-prefix>, "
        "or use the dev-core preset for engine core/tests without Qt."
    )
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

projectunity_resolve_qtads(PROJECTUNITY_QTADS_TARGET)
projectunity_resolve_imgui(PROJECTUNITY_IMGUI_TARGET)

qt_add_executable(projectunity_editor
    src/main.cpp
    src/EditorApp.cpp
    src/EditorQualitySettings.cpp
    src/MainWindow.cpp
    src/MainWindowAssets.cpp
    src/MainWindowCullingProfile.cpp
    src/MainWindowGameObjects.cpp
    src/MainWindowLighting.cpp
    src/MainWindowOptimization.cpp
    src/MainWindowPanels.cpp
    src/MainWindowPlay.cpp
    src/MainWindowProfiler.cpp
    src/MainWindowSmoke.cpp
    src/MainWindowTerrain.cpp
    src/TilemapEditorWidget.cpp
    src/ViewportTuningImGuiWindow.cpp
    src/ProjectAssetTreeWidget.cpp
    src/ProjectBrowserWidget.cpp
    src/SceneHierarchyWidget.cpp
    include/projectunity/editor/EditorApp.hpp
    include/projectunity/editor/EditorQualitySettings.hpp
    include/projectunity/editor/MainWindow.hpp
    include/projectunity/editor/ProjectAssetTreeWidget.hpp
    include/projectunity/editor/ProjectBrowserWidget.hpp
    include/projectunity/editor/SceneHierarchyWidget.hpp
    include/projectunity/editor/SettingsDialog.hpp
    include/projectunity/editor/TilemapEditorWidget.hpp
    include/projectunity/editor/ViewportTuningImGuiWindow.hpp
    src/SettingsDialog.cpp
    resources/editor_resources.qrc
)

add_executable(ProjectUnity::Editor ALIAS projectunity_editor)

target_include_directories(projectunity_editor
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(projectunity_editor
    PRIVATE
        ProjectUnity::Core
        ProjectUnity::Assets
        ProjectUnity::Math
        ProjectUnity::Renderer
        ProjectUnity::Scene
        ProjectUnity::Scripting
        ProjectUnity::EditorViewport
        Qt6::Concurrent
        Qt6::OpenGLWidgets
        Qt6::Widgets
        ${PROJECTUNITY_IMGUI_TARGET}
        ${PROJECTUNITY_QTADS_TARGET}
)

projectunity_configure_target(projectunity_editor)

target_compile_definitions(projectunity_editor
    PRIVATE
        PROJECTUNITY_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        PROJECTUNITY_BUILD_DIR="${CMAKE_BINARY_DIR}"
        PROJECTUNITY_BUILD_CONFIG="$<CONFIG>"
        PROJECTUNITY_CMAKE_COMMAND="${CMAKE_COMMAND}"
)

add_dependencies(projectunity_editor ProjectUnityGameScripts)

get_target_property(PROJECTUNITY_QT_CORE_LOCATION Qt6::Core IMPORTED_LOCATION_DEBUG)
if(NOT PROJECTUNITY_QT_CORE_LOCATION)
    get_target_property(PROJECTUNITY_QT_CORE_LOCATION Qt6::Core IMPORTED_LOCATION)
endif()
get_filename_component(PROJECTUNITY_QT_BIN_DIR "${PROJECTUNITY_QT_CORE_LOCATION}" DIRECTORY)
get_filename_component(PROJECTUNITY_QT_PREFIX_DIR "${PROJECTUNITY_QT_BIN_DIR}/.." ABSOLUTE)

set_target_properties(projectunity_editor PROPERTIES
    WIN32_EXECUTABLE TRUE
)

if(WIN32)
    set(PROJECTUNITY_WINDEPLOYQT "${PROJECTUNITY_QT_BIN_DIR}/windeployqt.exe")
    if(NOT EXISTS "${PROJECTUNITY_WINDEPLOYQT}")
        message(FATAL_ERROR "windeployqt was not found at ${PROJECTUNITY_WINDEPLOYQT}")
    endif()

    add_custom_command(TARGET projectunity_editor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:${PROJECTUNITY_QTADS_TARGET}>"
            "$<TARGET_FILE_DIR:projectunity_editor>"
        COMMAND "${PROJECTUNITY_WINDEPLOYQT}"
            "$<IF:$<CONFIG:Debug>,--debug,--release>"
            --no-compiler-runtime
            --no-translations
            "$<TARGET_FILE:projectunity_editor>"
        COMMENT "Deploying Qt and ADS runtime dependencies for ProjectUnity Editor"
        VERBATIM
    )
endif()

if(PROJECTUNITY_BUILD_TESTS)
    add_test(NAME projectunity_editor_smoke COMMAND projectunity_editor --smoke-test)
    set_tests_properties(projectunity_editor_smoke PROPERTIES
        ENVIRONMENT
            "QT_PLUGIN_PATH=${PROJECTUNITY_QT_PREFIX_DIR}/plugins;QT_QPA_PLATFORM=offscreen"
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${PROJECTUNITY_QT_BIN_DIR};PATH=path_list_prepend:$<TARGET_FILE_DIR:${PROJECTUNITY_QTADS_TARGET}>"
    )
endif()

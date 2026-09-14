find_package(Qt6 6.5 QUIET COMPONENTS Widgets)

if(NOT Qt6_FOUND)
    message(FATAL_ERROR
        "ProjectUnity player requires Qt 6.5 or newer with the Widgets module. "
        "Set QT_PREFIX or QT_ROOT to the Qt install prefix, pass -DCMAKE_PREFIX_PATH=<qt-prefix>, "
        "or disable PROJECTUNITY_BUILD_PLAYER."
    )
endif()

qt_add_executable(projectunity_player
    src/main.cpp
)

add_executable(ProjectUnity::Player ALIAS projectunity_player)

target_link_libraries(projectunity_player
    PRIVATE
        ProjectUnity::Assets
        ProjectUnity::Core
        ProjectUnity::EditorViewport
        ProjectUnity::Renderer
        ProjectUnity::Scene
        ProjectUnity::Scripting
        Qt6::Widgets
)

projectunity_configure_target(projectunity_player)

target_compile_definitions(projectunity_player
    PRIVATE
        PROJECTUNITY_BUILD_CONFIG="$<CONFIG>"
)

add_dependencies(projectunity_player ProjectUnityGameScripts)

get_target_property(PROJECTUNITY_QT_CORE_LOCATION Qt6::Core IMPORTED_LOCATION_DEBUG)
if(NOT PROJECTUNITY_QT_CORE_LOCATION)
    get_target_property(PROJECTUNITY_QT_CORE_LOCATION Qt6::Core IMPORTED_LOCATION)
endif()
get_filename_component(PROJECTUNITY_QT_BIN_DIR "${PROJECTUNITY_QT_CORE_LOCATION}" DIRECTORY)

set_target_properties(projectunity_player PROPERTIES
    WIN32_EXECUTABLE TRUE
)

if(WIN32)
    set(PROJECTUNITY_WINDEPLOYQT "${PROJECTUNITY_QT_BIN_DIR}/windeployqt.exe")
    if(NOT EXISTS "${PROJECTUNITY_WINDEPLOYQT}")
        message(FATAL_ERROR "windeployqt was not found at ${PROJECTUNITY_WINDEPLOYQT}")
    endif()

    add_custom_command(TARGET projectunity_player POST_BUILD
        COMMAND "${PROJECTUNITY_WINDEPLOYQT}"
            "$<IF:$<CONFIG:Debug>,--debug,--release>"
            --no-translations
            "$<TARGET_FILE:projectunity_player>"
        COMMENT "Deploying Qt runtime dependencies for ProjectUnity Player"
        VERBATIM
    )
endif()

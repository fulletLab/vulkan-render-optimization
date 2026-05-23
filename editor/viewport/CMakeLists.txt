find_package(Qt6 6.5 QUIET COMPONENTS Widgets)
include(ProjectUnityDependencies)

if(NOT Qt6_FOUND)
    message(FATAL_ERROR
        "ProjectUnity editor viewport requires Qt 6.5 or newer with the Widgets module. "
        "Set QT_PREFIX or QT_ROOT to the Qt install prefix, pass -DCMAKE_PREFIX_PATH=<qt-prefix>, "
        "or use the dev-core preset for engine core/tests without Qt."
    )
endif()

projectunity_resolve_tinygizmo(PROJECTUNITY_TINYGIZMO_TARGET)
projectunity_resolve_im3d(PROJECTUNITY_IM3D_TARGET)

add_library(projectunity_editor_viewport STATIC
    src/Im3dDebugDrawBackend.cpp
    src/TinyGizmoBackend.cpp
    src/ViewportWidgetCamera.cpp
    src/ViewportWidget.cpp
    src/ViewportWidgetDraw.cpp
    src/ViewportLabelGeometry.cpp
    src/ViewportWidgetRenderer.cpp
    src/ViewportWidgetSelfTest.cpp
    include/projectunity/editor/IEditorDebugDrawBackend.hpp
    include/projectunity/editor/IEditorGizmoBackend.hpp
    include/projectunity/editor/ViewportWidget.hpp
)

add_library(ProjectUnity::EditorViewport ALIAS projectunity_editor_viewport)

target_include_directories(projectunity_editor_viewport
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(projectunity_editor_viewport
    PUBLIC
        ProjectUnity::Core
        ProjectUnity::Assets
        ProjectUnity::Debug
        ProjectUnity::Math
        ProjectUnity::Renderer
        ProjectUnity::Scene
        ${PROJECTUNITY_IM3D_TARGET}
        ${PROJECTUNITY_TINYGIZMO_TARGET}
        Qt6::Widgets
)

projectunity_configure_target(projectunity_editor_viewport)

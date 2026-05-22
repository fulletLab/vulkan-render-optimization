# Phase 4 Status

## Scope

Phase 4 covers:

- tinygizmo dependency integration after license review.
- Move, Rotate, and Scale gizmos.
- Local and Global transform space selection.
- Scene View gizmo drawing and input routing.
- Q/W/E/R/F viewport shortcuts kept working with gizmos.
- Scene transform edits reflected in Inspector and Game View.

## Status

Status: COMPLETA

## Implemented

- Pinned tinygizmo FetchContent integration at commit `99c1c418d169774b0b8052b57fd1680b4c4de444`.
- `IEditorGizmoBackend` interface with editor-neutral frame, transform, and mesh structures.
- `TinyGizmoBackend` concrete implementation that owns tinygizmo state and quaternion conversion.
- QPainter rendering of tinygizmo world-space triangles inside the Scene View surface.
- Scene View mouse routing that gives gizmos priority over entity picking while dragging.
- Toolbar Hand/Move/Rotate/Scale actions connected to the viewport.
- Toolbar Local/Global actions connected to gizmo space state.
- Move, Rotate, and Scale edits written through `scene::Scene::setTransform`.
- Parent-relative position restoration after world-space gizmo edits.
- Inspector and both viewports refreshed after gizmo transform edits.
- Editor smoke coverage for gizmo geometry, mode switching, Local/Global sync, and synthetic ray-driven Move/Rotate/Scale drags that restore the scene after verification.

## Files Involved

- `cmake/ProjectUnityOptions.cmake`
- `cmake/ProjectUnityDependencies.cmake`
- `editor/viewport/CMakeLists.txt`
- `editor/viewport/include/projectunity/editor/IEditorGizmoBackend.hpp`
- `editor/viewport/include/projectunity/editor/ViewportWidget.hpp`
- `editor/viewport/src/TinyGizmoBackend.cpp`
- `editor/viewport/src/ViewportWidget.cpp`
- `editor/app/src/MainWindow.cpp`
- `docs/licenses.md`
- `docs/architecture.md`
- `docs/project_rules.md`
- `docs/phase4.md`
- `README.md`
- `examples/basic_scene/README.md`

## Architecture

- Engine scene/math modules still do not depend on Qt or tinygizmo.
- Qt viewport code depends on `IEditorGizmoBackend`; tinygizmo-specific API stays in `TinyGizmoBackend.cpp`.
- Gizmo frame input uses the same viewport camera basis and ray path as drawing and picking.
- Transform edits go back through the engine scene API and editor callbacks instead of mutating Inspector widgets directly.

## Build And Test

```powershell
cmake --preset dev-core
cmake --build --preset dev-core
ctest --preset dev-core

cmake --preset dev-editor-local-qt
cmake --build --preset dev-editor-local-qt
ctest --preset dev-editor-local-qt
```

## Verification

- Clean removed `build/dev-core` and `build/dev-editor`.
- Clean configured `dev-core` with Visual Studio 18 2026.
- Clean configured `dev-editor-local-qt`; CMake fetched pinned tinygizmo and ADS.
- Clean built `dev-core`.
- Clean built `dev-editor-local-qt`.
- `ctest --preset dev-core`: 3/3 tests passed in 1.99 seconds.
- `ctest --preset dev-editor-local-qt`: 4/4 tests passed in 5.17 seconds.
- `projectunity_editor_smoke` exercised tinygizmo Move, Rotate, and Scale drags through camera rays.
- Launched the deployed editor executable normally and closed through the main window with exit code 0.
- Source text check for unfinished-work markers, disallowed immediate-mode UI dependencies, local Windows user paths, and temporary-workaround wording returned no matches in current source and touched technical docs.

## Bugs Fixed During Phase

- Upstream tinygizmo `/W4` warnings leaked into project builds at first. Its target and headers are now isolated as third-party warnings without lowering warnings for ProjectUnity sources.
- Local/Global switching originally relied only on a raw tinygizmo toggle pulse. `TinyGizmoBackend` now tracks desired tool/space state and flushes the repeated local-space pulse path safely before toggling again.
- A hidden-window startup verification could not close the Qt main window through `CloseMainWindow`; the normal window lifecycle check was rerun and passed.

## Known Bugs

No known Phase 4 bugs at completion.

## Later Phase Work

- General debug draw and Im3d belong to Phase 5.
- Mesh asset rendering belongs to Phase 6.
- Editor undo/redo command stack expansion remains tied to broader editor command work.

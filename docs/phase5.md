# Phase 5 Status

## Scope

Phase 5 covers:

- Engine-side debug draw API.
- Im3d dependency integration after license review.
- Scene View debug rendering without Dear ImGui or ImGuizmo.
- Rays, AABBs, frustums, grids, and line primitive expansion.
- Focused engine tests and editor smoke coverage for debug draw.

## Status

Status: COMPLETA

## Implemented

- `engine/debug` with `IDebugDraw`, `DebugDrawList`, `DebugLine`, `DebugGrid`, and frustum corner types.
- Input validation and warning logs for invalid line, ray, AABB, frustum, and grid requests.
- Pinned Im3d FetchContent integration at commit `3fd7afaf3192ed50bca29c191969aa41252b53d5`.
- `IEditorDebugDrawBackend` editor-neutral frame and line mesh structures.
- `Im3dDebugDrawBackend` that fills Im3d `AppData`, finalizes draw lists, and extracts world-space line segments for the Qt Scene View.
- Scene View grid emitted through the debug draw API instead of a viewport-only grid loop.
- Camera ray and camera frustum debug geometry in Scene View.
- Selected entity debug AABB in Scene View using the same scene world-position path as picking and gizmos.
- Focused `projectunity_debug_tests` coverage for primitive expansion and rejected invalid input.
- Editor smoke coverage that requires the Im3d backend to emit line geometry.

## Files Involved

- `CMakeLists.txt`
- `cmake/ProjectUnityOptions.cmake`
- `cmake/ProjectUnityDependencies.cmake`
- `engine/debug/CMakeLists.txt`
- `engine/debug/include/projectunity/debug/DebugDraw.hpp`
- `engine/debug/src/DebugDraw.cpp`
- `editor/viewport/CMakeLists.txt`
- `editor/viewport/include/projectunity/editor/IEditorDebugDrawBackend.hpp`
- `editor/viewport/include/projectunity/editor/ViewportWidget.hpp`
- `editor/viewport/src/Im3dDebugDrawBackend.cpp`
- `editor/viewport/src/ViewportWidget.cpp`
- `tests/CMakeLists.txt`
- `tests/debug_tests/CMakeLists.txt`
- `tests/debug_tests/debug_tests.cpp`
- `docs/licenses.md`
- `docs/architecture.md`
- `docs/project_rules.md`
- `docs/phase5.md`
- `README.md`

## Architecture

- Engine modules can submit debug primitives through `IDebugDraw` without Qt, Im3d, or viewport dependencies.
- Editor debug rendering depends on the engine debug API through `IEditorDebugDrawBackend`.
- Im3d API use is isolated in `Im3dDebugDrawBackend.cpp`.
- The viewport still projects world-space line geometry with the same editor camera used by labels, picking, and tinygizmo.

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

- Removed `build/dev-core` and `build/dev-editor` after checking both resolved inside the workspace.
- Clean configured `dev-core` with Visual Studio 18 2026.
- Clean configured `dev-editor-local-qt`; CMake fetched pinned Im3d, tinygizmo, ADS, and the scene JSON dependency path.
- Clean built `dev-core`.
- Clean built `dev-editor-local-qt`.
- `ctest --preset dev-core`: 4/4 tests passed in 2.37 seconds.
- `ctest --preset dev-editor-local-qt`: 5/5 tests passed in 5.87 seconds.
- `projectunity_editor_smoke` opened the editor shell, exercised Scene View self-tests, required Im3d line output, and closed the smoke window path.
- Source text check for unfinished-work markers, disallowed immediate-mode UI dependencies, and local Windows user paths returned no matches in current Phase 5 source paths.

## Known Bugs

No known Phase 5 bugs at completion.

## Later Phase Work

- Renderer-owned GPU debug passes and RenderDoc markers belong to renderer phases.
- Physics collider and navmesh debug producers belong to their owning phases.
- Asset mesh rendering belongs to Phase 6.

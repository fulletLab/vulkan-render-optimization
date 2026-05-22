# Phase 3 Status

## Scope

Phase 3 covers:

- Scene View render surface.
- Game View render surface.
- Editor camera projection.
- Grid and axis drawing.
- Entity drawing from the engine scene.
- Real 3D picking from camera rays.
- Unity-like viewport navigation basics.
- Resize-safe projection.

## Status

Status: COMPLETA

## Implemented

- `editor/viewport` static library.
- `ViewportWidget` for Scene View and Game View.
- Camera orbit, right-mouse look, WASD fly navigation while right mouse is held, pan, zoom, and focus selected.
- Q/W/E/R tool mode shortcuts in the Scene View.
- Grid, axes, parent-child links, entity bounds, labels, and selection outline.
- Picking by ray/sphere intersection using the same camera basis used for drawing.
- Scene View click selection connected to the editor selection.
- Hierarchy and Inspector selection updates reflected back into both viewports.
- Viewport self-test integrated into `projectunity_editor_smoke`.

## Files Involved

- `editor/viewport/*`
- `editor/app/*`
- `engine/math/include/projectunity/math/Vec3.hpp`
- `tests/math_tests/math_tests.cpp`
- `docs/project_rules.md`
- `docs/phase3.md`
- `docs/architecture.md`
- `docs/licenses.md`
- `README.md`
- `examples/basic_scene/README.md`

## Verification

- 2026-05-22: `cmake --build --preset dev-editor-local-qt` passed.
- 2026-05-22: `ctest --preset dev-editor-local-qt`: 8/8 tests passed in 5.14 seconds.
- 2026-05-22: `projectunity_editor.exe --smoke-test` passed with exit code 0.
- Clean removed `build/dev-core` and `build/dev-editor`.
- `cmake --preset dev-core`
- `cmake --build --preset dev-core`
- `ctest --preset dev-core`: 3/3 tests passed in 2.43 seconds.
- `cmake --preset dev-editor-local-qt`
- `cmake --build --preset dev-editor-local-qt`
- `ctest --preset dev-editor-local-qt`: 4/4 tests passed in 5.62 seconds.
- Launched deployed editor executable and closed through the main window with exit code 0.
- Source text check for unfinished-work markers, disallowed immediate-mode UI dependencies, local Windows user paths, and temporary-workaround wording returned no matches in source and current phase docs.

## Bug Fixed During Phase

- `distanceSquared` was initially marked `constexpr` while `Vec3::lengthSquared()` was not. MSVC rejected the build. `lengthSquared()` is now `constexpr`, and both core and editor builds pass.

## Known Bugs

No known Phase 3 bugs at completion.

## Later Phase Work

- Gizmo manipulation belongs to Phase 4.
- Debug draw module belongs to Phase 5.
- Asset mesh rendering belongs to Phase 6.
- The final runtime renderer and Vulkan/VMA work belong to later renderer phases.

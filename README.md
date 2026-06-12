# ProjectUnity

ProjectUnity is a modular C++20 engine/editor foundation. The implemented scope currently covers the base project, Scene/ECS basics, and a Qt Widgets editor shell with ADS docking, real Scene/Game viewport widgets, tinygizmo-backed transform gizmos, Im3d-backed debug draw in Scene View, and a first asset pipeline for PNG/JPG plus glTF/GLB model import.

## Build

From a shell that already exposes CMake, a C++20 compiler, and Qt:

```powershell
cmake --preset dev-editor
cmake --build --preset dev-editor
ctest --preset dev-editor
```

For machines without Qt installed:

```powershell
cmake --preset dev-core
cmake --build --preset dev-core
ctest --preset dev-core
```

For editor builds that need Qt discovery help, see `docs/qt_setup.md`.

## Current Phase

Current active phase: Phase 6 asset pipeline / Vulkan imported-asset viewport.
Status: PARCIAL / under review. Phase 6 must not be treated as complete yet.

The importer, material preservation, Vulkan viewport path, shadows, culling,
profiling counters, and large-scene optimization work are integrated, but the
phase still needs Release validation on representative external scenes before it
can close. Current review items:

- Close-camera visual parity: no occlusion popping, no unintended asset
  degradation, and selected/near objects remain full fidelity.
- Final visible-list control: culled or occluded objects must not reach resource
  preparation, shadow submission, descriptor/buffer binds, or `vkCmdDrawIndexed`.
- Large-scene stress cases: duplicated 400 MB terrain assets and the 37 MB
  rock-cluster asset, including separated instances and close camera movement.
- Editor interaction: selection/picking and transform movement must remain
  accurate and responsive on heavy imported assets.
- Profiler counters must match real Vulkan work and FPS in the same frame.

Per `docs/PROMPT_MAESTRO.md`, do not advance to Phase 7 as if Phase 6 is done
until the current Phase 6 blockers are resolved, rebuilt, tested, and documented.

See `docs/project_rules.md`, `docs/phase1.md`, `docs/phase2.md`, `docs/phase3.md`, `docs/phase4.md`, `docs/phase5.md`, and `docs/phase6.md` for implementation status. The editor target intentionally depends on Qt Widgets and Qt Advanced Docking System; runtime and server targets must not depend on Qt in later phases.

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

See `docs/project_rules.md`, `docs/phase1.md`, `docs/phase2.md`, `docs/phase3.md`, `docs/phase4.md`, `docs/phase5.md`, and `docs/phase6.md` for implementation status. The editor target intentionally depends on Qt Widgets and Qt Advanced Docking System; runtime and server targets must not depend on Qt in later phases.

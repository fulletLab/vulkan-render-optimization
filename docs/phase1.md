# Phase 1 Status

## Scope

Implemented scope:

- CMake project root and build presets.
- Modular folders for engine core, engine math, editor app, tests, docs, server, tools, examples, client, and third party notes.
- Thread-safe logging API with categories and basic sensitive-value redaction.
- Stable ID generator.
- Math primitives for `Vec3` and `Mat4`.
- Qt Widgets editor shell using Qt Advanced Docking System.
- Unity-like menu, toolbar, dock panels, log console, and layout save/restore through `QSettings`.
- Core and math test executables registered with CTest.

## Build Commands

Full editor build:

```powershell
cmake --preset dev-editor
cmake --build --preset dev-editor
ctest --preset dev-editor
```

Core-only build when Qt is unavailable:

```powershell
cmake --preset dev-core
cmake --build --preset dev-core
ctest --preset dev-core
```

## Known Local Verification State

Status: COMPLETA

The core-only build was verified by the user from Visual Studio 2026 Developer Command Prompt. The editor build is now verified locally with Qt 6.8.3 installed under `.deps/qt/6.8.3/msvc2022_64`.

Checks performed in this shell:

- `rg --files` confirms the created source tree is present.
- Source text checks for unfinished-work markers, temporary-workaround wording, local Windows user paths, and disallowed immediate-mode editor UI dependencies returned no matches.
- `cmake --preset dev-core` failed because `cmake` is not available in this shell.
- `cmake --preset dev-editor` failed because `cmake` is not available in this shell.

Checks performed by the user from Visual Studio 2026 Developer Command Prompt:

- `cmake --preset dev-core` configured successfully with MSVC 19.50 / Visual Studio 18 2026.
- `cmake --build --preset dev-core` built `projectunity_core.lib`, `projectunity_core_tests.exe`, and `projectunity_math_tests.exe`.
- `ctest --preset dev-core` passed 2/2 tests in 0.05 seconds.

Dependency setup performed locally:

- Installed `aqtinstall` into `.deps/aqt-py`.
- Queried Qt packages and selected Qt 6.8.3 `win64_msvc2022_64`.
- Downloaded the seven required Qt archives into `.deps/qtarchives`.
- Verified SHA256 checksums for all downloaded Qt archives.
- Extracted Qt with CMake's `cmake -E tar` to avoid an `aqtinstall` / `py7zr` extraction bug on this Python 3.13 environment.
- Added `dev-editor-local-qt` preset for the local `.deps` Qt installation.

Checks performed from generated `dev-core` binaries:

- `build/dev-core/tests/core_tests/Debug/projectunity_core_tests.exe` passed with exit code 0.
- `build/dev-core/tests/math_tests/Debug/projectunity_math_tests.exe` passed with exit code 0.

Fixes applied after verification:

- Removed `CMAKE_BUILD_TYPE` from presets because Visual Studio is a multi-config generator.
- Added `Debug` configuration to build and test presets.
- Replaced the generic Qt package failure with a project-specific editor dependency error.
- Added `QT_PREFIX` / `QT_ROOT` discovery support and a `dev-editor-qt-env` preset for Qt editor builds.
- Patched ADS 4.5.0 version resource generation during FetchContent population so non-Git workspaces produce valid Windows `.rc` version fields.
- Disabled ADS examples/demos in dependency builds.
- Removed a deprecated ADS constructor use in the editor.
- Added post-build deployment of Qt and ADS runtime DLLs next to `projectunity_editor.exe`.
- Added `projectunity_editor_smoke`, which validates editor dock creation, layout save/restore, and console log visibility.

Final Phase 1 verification:

- Removed `build/dev-editor`.
- Ran `cmake --preset dev-editor-qt-env`.
- Ran `cmake --build --preset dev-editor-qt-env`.
- Ran `ctest --preset dev-editor-qt-env`: 3/3 tests passed in 5.06 seconds.
- Launched the deployed `projectunity_editor.exe` directly from `build/dev-editor/editor/app/Debug`.
- Closed the editor through the main window; process exited with code 0.
- Ran `cmake --preset dev-editor-local-qt`.
- Ran `cmake --build --preset dev-editor-local-qt`.
- Ran `ctest --preset dev-editor-local-qt`: 3/3 tests passed in 3.14 seconds.

## Phase 1 Completion Criteria

- Editor opens: verified.
- ADS dock panels exist and layout save/restore works: verified by `projectunity_editor_smoke`.
- Layout is saved on close through `QSettings`: verified by normal launch/close path and destructor.
- Core/math tests pass: verified.
- Editor smoke test passes: verified.
- Clean rebuild from removed `build/dev-editor`: verified.
- No known crashes: verified for smoke test and direct launch/close.
- No known build errors: verified.

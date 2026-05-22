# Qt Setup For Editor Builds

The editor target requires Qt 6.5 or newer with the Widgets module. Core engine builds do not require Qt.

## Configure With An Environment Variable

Set `QT_PREFIX` to the Qt installation prefix, then configure the editor:

```powershell
$env:QT_PREFIX = "<qt-install-prefix>"
cmake --fresh --preset dev-editor-qt-env
cmake --build --preset dev-editor-qt-env
ctest --preset dev-editor-qt-env
```

The prefix is the directory that contains Qt's CMake package folders under `lib/cmake/Qt6`.

## Configure With Local `.deps` Qt

This workspace has Qt 6.8.3 installed under `.deps/qt/6.8.3/msvc2022_64`.

```powershell
cmake --preset dev-editor-local-qt
cmake --build --preset dev-editor-local-qt
ctest --preset dev-editor-local-qt
```

## Configure With A CMake Argument

```powershell
cmake --fresh --preset dev-editor -DCMAKE_PREFIX_PATH="<qt-install-prefix>"
cmake --build --preset dev-editor
ctest --preset dev-editor
```

## Current Blocker

If configure prints that Qt 6.5+ Widgets is required, CMake still cannot see Qt. Do not run the build or test command for `dev-editor` until configure succeeds, because no Visual Studio project or editor tests are generated.

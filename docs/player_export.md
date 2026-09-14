# Player Export

This is the first standalone export path for ProjectUnity.

## Verifiable Chain

```text
saved scene
-> projectunity_player executable
-> cooked asset cache
-> native script module
-> game.projectunity.json manifest
-> portable output folder
```

The player does not guess paths. The exported manifest points to:

- `Content/Scenes/main.scene.json`
- `Content/Assets`
- `Scripts/ProjectUnityGameScripts.dll` on Windows

## Export Example

```powershell
python tools/export_game.py `
  --scene examples/basic_scene/BasicScene.scene.json `
  --output Builds/Windows/BasicScene `
  --name BasicScene `
  --zip
```

The scene must contain a runtime camera. The export fails if the scene, asset cache,
player executable, or script module is missing.

If `cmake` is not on `PATH`, pass `--cmake "C:/path/to/cmake.exe"`.

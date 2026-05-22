# Phase 2 Status

## Scope

Phase 2 covers:

- Engine scene module.
- Entity IDs and names.
- Transform component.
- Parent/child hierarchy.
- Hierarchy panel connected to the scene.
- Inspector editing of name and transform.
- Scene save/load.
- Tests for hierarchy and serialization.

## Status

Status: COMPLETA

## Implemented

- `engine/scene` static library with `Scene`, `Entity`, and `TransformComponent`.
- Stable entity IDs using `core::StableId`.
- Parent/child hierarchy with cycle rejection.
- Entity create/delete/duplicate.
- Scene name and entity name editing.
- JSON scene serialization using nlohmann/json.
- File save/load with error messages and logs.
- Editor Hierarchy panel connected to real scene entities.
- Editor Inspector connected to selected entity transform.
- File menu scene New/Open/Save/Save As commands.
- GameObject menu Create Empty/Duplicate/Delete commands.
- Editor smoke test for create/select/edit/save/load.
- Basic scene example at `examples/basic_scene/BasicScene.scene.json`.

## Files Involved

- `engine/scene/*`
- `editor/app/*`
- `tests/scene_tests/*`
- `examples/basic_scene/*`
- `docs/project_rules.md`
- `docs/phase2.md`

## Verification

- Clean removed `build/dev-core` and `build/dev-editor`.
- `cmake --preset dev-core`
- `cmake --build --preset dev-core`
- `ctest --preset dev-core`: 3/3 tests passed in 1.89 seconds.
- `cmake --preset dev-editor-local-qt`
- `cmake --build --preset dev-editor-local-qt`
- `ctest --preset dev-editor-local-qt`: 4/4 tests passed in 5.78 seconds.
- Launched deployed editor executable and closed through the main window with exit code 0.

## Known Bugs

No known Phase 2 bugs at completion.

## Later Phase Work

- Viewport selection is Phase 3.
- Gizmo editing is Phase 4.
- Undo/redo command stack will be expanded when editor commands become more numerous.

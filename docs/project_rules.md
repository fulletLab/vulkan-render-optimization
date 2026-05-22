# Project Rules

This project is built phase by phase. A phase is only complete when it compiles, is integrated, has no known crashes or build errors, has useful logs and error handling, has focused tests or manual verification, is documented, and can be rebuilt from scratch.

## Current Phase Order

- Phase 1: project base and Qt/ADS editor shell. Status: complete.
- Phase 2: Scene / ECS basics. Status: complete.
- Phase 3: 3D viewport. Status: complete.
- Phase 4: tinygizmo transform gizmos. Status: complete.
- Phase 5: debug draw. Status: complete.
- Phase 6: asset pipeline. Status: partial while the imported-asset Vulkan path,
  representative visual verification, and remaining current-phase renderer
  blockers in `docs/phase6.md` are still open.
- Later phases must not be treated as complete until their own Definition of Done is verified.

## Non-Negotiables

- Do not use Dear ImGui or ImGuizmo.
- Do not make engine runtime modules depend on Qt.
- Do not make server modules depend on Qt, renderer, audio, or editor code.
- Do not trust client data in future networking phases.
- Do not print secrets, tokens, private keys, passwords, or complete auth headers in logs.
- Keep code files at 800 lines or fewer. Split ownership before files grow past that limit.
- Do not advance to the next phase as if the current phase is complete while current-phase build, tests, UI integration, or known bugs remain unresolved.

## Verification Discipline

Every completed phase must list:

- What changed.
- Files involved.
- How to build.
- How to test.
- Test results actually executed.
- Bugs known at completion.
- Remaining work that belongs to later phases.

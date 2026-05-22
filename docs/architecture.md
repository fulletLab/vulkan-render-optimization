# Architecture Baseline

## Ownership Rules

- `engine/*` owns runtime systems and must not depend on Qt or editor code.
- `editor/*` may depend on engine public APIs and Qt Widgets.
- `server/*` must remain headless and must not depend on Qt, renderer, audio, or editor code.
- `tools/*` will contain command line and offline processing tools.
- `third_party/*` is reserved for vendored or mirrored dependencies only after license review.

## Phase 1 Modules

- `engine/core`: logging, IDs, low level utility types.
- `engine/math`: C++20 math primitives shared by runtime, editor, and tests.
- `engine/scene`: entity hierarchy, transform component, and scene serialization. It depends on engine core/math and does not depend on Qt.
- `editor/app`: Qt Widgets editor shell with ADS dock layout and visible log console.
- `editor/viewport`: Qt Widgets editor viewport surface. It depends on engine core/math/scene and Qt Widgets, but remains outside runtime/server modules.
- `tests/*`: dependency-light test executables registered with CTest.

## Phase 3 Viewport

- Scene View and Game View are real editor viewport widgets, not static labels.
- The viewport uses one camera model for drawing, labels, screen rays, and picking.
- Picking is performed by building a ray from the camera through the mouse position and intersecting 3D entity bounds.
- The Phase 3 viewport is editor UI infrastructure. It is not the final runtime renderer and does not introduce renderer dependencies into engine runtime code.

## Phase 4 Gizmos

- `IEditorGizmoBackend` keeps editor viewport input, mesh drawing, and scene transform application independent from the selected gizmo library.
- `TinyGizmoBackend` is the concrete editor backend. It owns tinygizmo state, converts editor camera/ray/transform data, and returns editor-neutral gizmo triangles to the viewport.
- `ViewportWidget` renders that returned geometry through the current Qt viewport surface and writes transform edits back through the scene API so Hierarchy/Inspector state stays coherent.

## Phase 5 Debug Draw

- `engine/debug` owns `IDebugDraw` and `DebugDrawList`; engine-side debug producers expand lines, rays, AABBs, frustums, and grids without depending on Qt or Im3d.
- `IEditorDebugDrawBackend` keeps the Qt viewport separate from the selected debug-draw library.
- `Im3dDebugDrawBackend` feeds editor camera frame data and world-space debug lines into Im3d, then returns editor-neutral line segments for the current Scene View paint surface.
- `ViewportWidget` uses the debug path for the Scene View grid plus camera ray, camera frustum, and selected-entity AABB verification geometry.

## Phase 6 Asset Pipeline

- `engine/assets` owns asset import records, CPU-side texture/model data, cache metadata, `IAssetManager`, and the concrete `AssetManager`.
- Scene entities store a `MeshRendererComponent` with only a stable model asset ID; `engine/scene` does not import files or know TinyGLTF internals.
- TinyGLTF and its single compiled stb image implementation load glTF/GLB scenes, nodes, meshes, and images. glTF node transforms are applied before MikkTSpace generates imported tangent bases, and meshoptimizer reorders indices/vertices and emits simplification LOD data when a primitive has enough indices.
- The Qt editor imports assets asynchronously from Project Browser. Imported model records create mesh-renderer entities and the viewport resolves model/material data through `IAssetManager`.
- The first Phase 6 GPU path builds imported primitive draw items in the editor viewport, uploads vertex/index buffers and base-color textures through the renderer module, and renders textured mesh draws into a Vulkan viewport target. Selected tinygizmo geometry is also submitted through a renderer-owned Vulkan color mesh path when imported meshes are presented.
- Material expansion, mipmap generation, culling, grid/debug/label overlay passes, and full scene lighting remain open renderer work; Qt must not become the primary 3D renderer.

## Future Module Boundaries

The later rendering, assets, terrain, physics, navigation, runtime UI, networking, security, and server systems should expose interfaces before concrete implementations are introduced. The planned API names are `IRenderer`, `IRenderDevice`, `IAssetManager`, `IPhysicsWorld`, `INavigationWorld`, `INetworkTransport`, `ISecureChannel`, `IEditorGizmoBackend`, `IDebugDraw`, and `IGameUISystem`.

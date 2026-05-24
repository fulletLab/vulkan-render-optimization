# Phase 6 Status

## Scope

Phase 6 covers:

- PNG/JPG decode through stb image.
- glTF/GLB model import through TinyGLTF.
- glTF punctual light and camera preservation for renderer/editor use.
- glTF vertex color preservation for `COLOR_0`.
- Imported tangent generation through MikkTSpace.
- Imported mesh optimization and offline simplification LOD data through meshoptimizer.
- Asset cache metadata and Project Browser visibility.
- Imported textured mesh visibility in the editor viewport.

## Status

Status: COMPLETE

## Phase 6.0.01 Research Baseline

- Vulkan optimization notes are tracked in
  `docs/phase6_0_01_vulkan_optimization_notes.md`.
- This checkpoint is research and process guidance, not a completed renderer feature.
- Future Phase 6 performance work must measure before changing, preserve imported asset
  fidelity, keep Qt out of the primary 3D render path, and optimize renderer data flow
  rather than degrading authored content.

## Blocking Renderer Status

- The GLB/glTF importer keeps the full source mesh and material data. The default Scene View path must use the full primitive index buffers, not preview triangle budgets or automatic destructive LODs.
- The editor now initializes a Vulkan renderer module with device selection, VMA, Win32 viewport surface creation, swapchain creation, swapchain image views, command buffer recording, synchronization, depth-backed render pass, GPU timestamp queries, GPU mesh upload, color-space-aware GPU material texture upload with imported sampler state, generated RGBA8 mip chains, HDR texture import for environment sources, explicit KTX/KTX2 GPU mip upload for supported RGBA8/BC/ETC2/ASTC 2D textures, libktx-backed KTX2 Basis/UASTC/Zstd import with BC7 GPU payload plus RGBA8 fallback for Basis/UASTC when possible, conservative bounds culling, culling/timing renderer stats, glTF material factor and vertex-color preservation, textured imported mesh draws, Lighting panel driven RenderFrame environment settings, generated irradiance/prefiltered IBL cubemaps, renderer-owned cascaded directional shadows, spot 2D shadows, point-light cubemap shadows, shared opaque-caster descriptors, and Scene View color mesh passes for gizmo/grid/axes/hierarchy links.
- Imported meshoptimizer LOD index buffers are now selected at runtime by conservative screen-space primitive size. This keeps full-resolution indices for nearby and selected objects, uses lower LODs only when a primitive is small on screen, and leaves imported source geometry/materials/textures unchanged.
- The CPU/QPainter mesh bridge is a temporary fallback when Vulkan surface/frame rendering fails, including Qt offscreen tests. Empty-scene editor aids and transform-only entity markers now use the Vulkan color path when a real viewport surface is available.
- Phase 6 completion was verified with the normal core/editor suites, visible editor smoke, and a broad Phase 6 visual smoke over the local verification GLB pack, including cascaded directional shadows, `LightsPunctualLamp.glb` point-light cubemap shadows, and `NodePerformanceTest.glb` large-scene counters.

## Implemented

- `engine/assets` module with `IAssetManager`, `AssetManager`, model/texture/material CPU data, asset records, and cache metadata writes.
- Stable model and texture asset IDs derived from source/content bytes instead of persisted absolute local paths.
- Texture import for PNG/JPG/HDR through stb image. HDR sources preserve floating-point RGBA radiance data plus an RGBA8 preview/fallback.
- Texture import for KTX1/KTX2 2D GPU payloads. The internal parser preserves RGBA8, BC, ETC2 RGBA8, and ASTC explicit mip levels; when `PROJECTUNITY_ENABLE_LIBKTX` is enabled, libktx imports KTX2 supercompressed assets, transcodes Basis/UASTC to BC7 GPU mip payloads plus RGBA8 fallback data when possible, and preserves uploadable Zstd explicit mips for supported VkFormats.
- License check for this dependency: KTX-Software/libktx is fetched at `v4.4.2` and the upstream license is Apache-2.0, so it is acceptable for the current non-GPL dependency policy.
- Model import for `.gltf` and `.glb` through TinyGLTF with validation for accessors, index bounds, triangle primitive mode, images, missing files, and default scene node hierarchy traversal.
- glTF node matrix/TRS transforms are applied during import so multi-object exports keep their authored positions, rotations, and scale instead of collapsing raw mesh primitives.
- Mesh primitive bounds are computed at import time and refreshed after glTF node transforms for viewport culling.
- glTF material `baseColorFactor`, `metallicFactor`, `roughnessFactor`, and `emissiveFactor` are preserved in `MaterialAsset`.
- glTF mesh `COLOR_0` vertex colors are imported, kept through meshoptimizer vertex fetch reordering, uploaded in the Vulkan vertex buffer, and multiplied by material/texture color in the mesh shader.
- glTF normal textures, normal scale, metallic-roughness textures, occlusion textures, occlusion strength, and emissive textures are preserved in `MaterialAsset`; the Vulkan material descriptor now binds base-color, flat-normal-or-normal-map, white-or-metallic-roughness, white-or-occlusion, and white-or-emissive texture slots.
- glTF texture sampler magnification/minification/mipmap filter choice plus S/T wrap state are preserved per `TextureAsset` and mapped to Vulkan sampler/cache variants.
- glTF material alpha mode and cutoff are preserved. The Vulkan mesh shader forces `OPAQUE` output alpha, discards `MASK` fragments at the imported cutoff, and forwards `BLEND` alpha to the current blend pipeline.
- glTF `KHR_lights_punctual` directional, point, and spot light nodes are kept in `ModelAsset` with their node transform, color, intensity, range, and cone data; glTF camera nodes are kept with perspective or orthographic projection data and world-space node basis.
- Game View uses the first imported perspective camera found in instantiated model data while Scene View keeps its editor camera.
- The Vulkan mesh draw order keeps opaque/masked primitives ahead of transparent ones, sorts glTF `BLEND` primitives back-to-front by viewport camera depth, and uses a transparent mesh pipeline without depth writes for those blended primitive draws.
- MikkTSpace tangent generation for imported primitives with logged fallback tangent basis on unusable data.
- meshoptimizer vertex-cache, overdraw, vertex-fetch optimization, and simplification LOD generation when primitive index counts permit it.
- `MeshRendererComponent` scene serialization that stores only the model asset ID.
- Project Browser import table and asynchronous Qt import command.
- The Asset Import panel now shows deterministic 1-100 staged progress for async imports instead of an indeterminate bar.
- Imported model entities created in scene data after a successful editor import.
- Scene/Game viewport CPU fallback mesh drawing from `IAssetManager`, including base-color textured triangle mapping for the Phase 6 editor bridge.
- Initial `engine/renderer` Vulkan module with instance/device creation, validation-layer discovery, debug-utils availability check, VMA allocator creation, Win32 viewport surface/swapchain preparation, render pass/pipeline creation, staged vertex/index/texture upload caches with sRGB color, linear data-map, imported sampler variants, generated material texture mip chains, explicit KTX/KTX2 mip upload when the Vulkan device supports the stored format, RGBA fallback when a stored Basis/UASTC BC7 candidate is not sampleable, conservative viewport culling, frame-uniform camera/light/environment/shadow-view state, Cook-Torrance-style direct material shading, Lighting panel driven renderer-generated irradiance and prefiltered environment cubemaps, HDR source environment cubemap generation with float-preserving Vulkan upload when supported, a renderer-generated BRDF integration LUT bound as Vulkan material descriptors, renderer-owned 4096 2D shadow atlas for cascaded directional shadows, 2D spot shadow fallback, point-light depth cubemap shadows, `MASK` alpha cutoff sampling, Vulkan comparison-sampler PCF, and shared opaque-caster descriptor reuse, glTF alpha mode/cutoff handling, transparent primitive draw ordering, vertex-color-aware textured mesh draw path, controlled Scene View editor color mesh draw path, renderer stats, and renderer tests.
- The editor Profiler tab displays renderer GPU/API, frame, draw, light, and shadow counters from `RendererStats`.
- The Profiler separates CPU frame cost into resource preparation, Vulkan command
  recording, shadow pass recording, main mesh pass recording, and editor color-aid
  recording so large-scene stalls can be diagnosed by renderer stage instead of guessed.
- The Vulkan viewport records GPU timestamp queries around the full viewport frame,
  shadow pass, main mesh pass, and editor color-aid pass. The Profiler exposes support/
  validity plus the latest collected GPU times in microseconds.
- The Profiler also reports runtime LOD draw count and skipped triangles so large-scene distance behavior is measurable instead of guessed.
- The shadow pass uses each draw's world-space sphere bounds against the selected shadow
  view-projection clip planes and skips off-map opaque/masked batches conservatively.
  The Profiler reports shadow view count plus both drawn and culled shadow batches.
- Example textured asset at `examples/basic_assets/TexturedTriangle.gltf`.

## Files Involved

- `CMakeLists.txt`
- `cmake/ProjectUnityOptions.cmake`
- `cmake/ProjectUnityDependencies.cmake`
- `engine/assets/CMakeLists.txt`
- `engine/assets/include/projectunity/assets/AssetManager.hpp`
- `engine/assets/src/AssetCache.cpp`
- `engine/assets/src/AssetImport.cpp`
- `engine/assets/src/AssetManager.cpp`
- `engine/assets/src/GltfAttributeReader.cpp`
- `engine/assets/src/GltfAttributeReader.hpp`
- `engine/assets/src/GltfImageLoader.cpp`
- `engine/assets/src/GltfImageLoader.hpp`
- `engine/assets/src/GltfNodeTransforms.cpp`
- `engine/assets/src/GltfNodeTransforms.hpp`
- `engine/assets/src/GltfSceneObjects.cpp`
- `engine/assets/src/GltfSceneObjects.hpp`
- `engine/assets/src/GltfTextureImport.cpp`
- `engine/assets/src/GltfTextureImport.hpp`
- `engine/assets/src/KtxTextureImport.cpp`
- `engine/assets/src/KtxTextureImport.hpp`
- `engine/assets/src/KtxTextureImportLibktx.cpp`
- `engine/assets/src/KtxTextureImportLibktx.hpp`
- `engine/assets/src/MeshBounds.cpp`
- `engine/assets/src/MeshBounds.hpp`
- `engine/assets/src/StbTextureImport.cpp`
- `engine/assets/src/StbTextureImport.hpp`
- `engine/assets/src/TinyGltfImplementation.cpp`
- `engine/renderer/CMakeLists.txt`
- `engine/renderer/include/projectunity/renderer/IRenderer.hpp`
- `engine/renderer/include/projectunity/renderer/RenderBrdfLut.hpp`
- `engine/renderer/include/projectunity/renderer/RenderDrawOrdering.hpp`
- `engine/renderer/include/projectunity/renderer/RenderEnvironmentMap.hpp`
- `engine/renderer/include/projectunity/renderer/RenderShadowSetup.hpp`
- `engine/renderer/include/projectunity/renderer/RendererTypes.hpp`
- `engine/renderer/include/projectunity/renderer/ViewportRenderSurface.hpp`
- `engine/renderer/include/projectunity/renderer/VulkanRenderer.hpp`
- `engine/renderer/shaders/TexturedMesh.vert`
- `engine/renderer/shaders/TexturedMesh.frag`
- `engine/renderer/shaders/ShadowDepth.vert`
- `engine/renderer/shaders/ShadowDepth.frag`
- `engine/renderer/shaders/EditorColor.vert`
- `engine/renderer/shaders/EditorColor.frag`
- `engine/renderer/src/RenderBrdfLut.cpp`
- `engine/renderer/src/RenderEnvironmentMap.cpp`
- `engine/renderer/src/VulkanColorMesh.cpp`
- `engine/renderer/src/VulkanColorMesh.hpp`
- `engine/renderer/src/RenderDrawOrdering.cpp`
- `engine/renderer/src/RenderShadowSetup.cpp`
- `engine/renderer/src/VulkanColorPipeline.cpp`
- `engine/renderer/src/VulkanColorPipeline.hpp`
- `engine/renderer/src/VulkanDebugLabels.hpp`
- `engine/renderer/src/VulkanEnvironmentTextureCache.cpp`
- `engine/renderer/src/VulkanGpuBuffer.cpp`
- `engine/renderer/src/VulkanGpuBuffer.hpp`
- `engine/renderer/src/VulkanGpuFrameProfiler.cpp`
- `engine/renderer/src/VulkanGpuFrameProfiler.hpp`
- `engine/renderer/src/VulkanFrameData.cpp`
- `engine/renderer/src/VulkanFrameData.hpp`
- `engine/renderer/src/VulkanMaterialTextureSet.cpp`
- `engine/renderer/src/VulkanMaterialTextureSet.hpp`
- `engine/renderer/src/VulkanMeshCache.cpp`
- `engine/renderer/src/VulkanMeshCache.hpp`
- `engine/renderer/src/VulkanMeshPipeline.cpp`
- `engine/renderer/src/VulkanMeshPipeline.hpp`
- `engine/renderer/src/VulkanResourceContext.hpp`
- `engine/renderer/src/VulkanShadowPipeline.cpp`
- `engine/renderer/src/VulkanShadowPipeline.hpp`
- `engine/renderer/src/VulkanTextureCache.cpp`
- `engine/renderer/src/VulkanTextureCache.hpp`
- `engine/renderer/src/VulkanTextureFormat.cpp`
- `engine/renderer/src/VulkanTextureFormat.hpp`
- `engine/renderer/src/VulkanUploadContext.cpp`
- `engine/renderer/src/VulkanUploadContext.hpp`
- `engine/renderer/src/VulkanRenderer.cpp`
- `engine/renderer/src/VulkanSupport.cpp`
- `engine/renderer/src/VulkanSupport.hpp`
- `engine/renderer/src/VulkanViewportTarget.cpp`
- `engine/renderer/src/VulkanViewportTargetDescriptors.cpp`
- `engine/renderer/src/VulkanViewportTarget.hpp`
- `cmake/ProjectUnityEmbedSpirv.cmake`
- `engine/scene/include/projectunity/scene/Scene.hpp`
- `engine/scene/src/Scene.cpp`
- `engine/scene/src/SceneSerialization.cpp`
- `editor/app/CMakeLists.txt`
- `editor/app/include/projectunity/editor/MainWindow.hpp`
- `editor/app/src/EditorApp.cpp`
- `editor/app/src/MainWindow.cpp`
- `editor/app/src/MainWindowAssets.cpp`
- `editor/app/src/MainWindowPanels.cpp`
- `editor/app/src/MainWindowSmoke.cpp`
- `editor/viewport/CMakeLists.txt`
- `editor/viewport/include/projectunity/editor/ViewportWidget.hpp`
- `editor/viewport/src/ViewportWidgetCamera.cpp`
- `editor/viewport/src/ViewportWidget.cpp`
- `editor/viewport/src/ViewportWidgetDraw.cpp`
- `editor/viewport/src/ViewportWidgetSelfTest.cpp`
- `tests/CMakeLists.txt`
- `tests/asset_tests/CMakeLists.txt`
- `tests/asset_tests/asset_tests.cpp`
- `tests/renderer_tests/CMakeLists.txt`
- `tests/renderer_tests/renderer_tests.cpp`
- `tests/source_rule_tests/CMakeLists.txt`
- `tests/source_rule_tests/source_rule_tests.cpp`
- `tests/scene_tests/scene_tests.cpp`
- `examples/basic_assets/README.md`
- `examples/basic_assets/TexturedTriangle.gltf`
- `docs/licenses.md`
- `docs/architecture.md`
- `docs/project_rules.md`
- `docs/phase6.md`
- `README.md`

## Architecture

- File import, decoded image bytes, CPU mesh buffers, tangent generation, optimization, and cache writes stay in `engine/assets`.
- Scene serialization records stable mesh asset IDs and does not store decoded asset buffers or source absolute paths.
- The editor imports in a Qt background task and updates Project Browser/UI only on completion.
- Viewport fallback code resolves imported models through `IAssetManager`; it does not call TinyGLTF, stb, MikkTSpace, or meshoptimizer.
- Qt owns editor UI, docking, menus, panels, and input dispatch. The renderer module is independent of Qt and is injected into editor viewports through `IRenderer`.
- Coordinate convention: glTF/GLB source data is read in glTF's right-handed, Y-up convention with local `-Z` forward, then converted once at import into the engine/editor convention: Y-up with `+Z` forward. The conversion is a single Z reflection applied after glTF node `matrix`/TRS evaluation; UVs are not flipped. `node.matrix` is treated as column-major and TRS is built as column-vector `T * R * S`. When node transforms are baked into mesh vertices, positions and tangents use the converted linear transform, normals use the inverse-transpose linear transform, bounds are recomputed after baking, and negative-determinant converted transforms flip triangle winding plus tangent handedness so future culling remains consistent without mirroring the asset. Imported cameras/lights are converted through the same matrix, and Game View uses imported camera local `+X` as screen-right instead of reconstructing it from forward/up.
- The current Vulkan renderer owns core GPU initialization, viewport presentation resources, textured mesh draws, vertex-color multiplication, base-color/normal/metallic-roughness/occlusion/emissive texture slots with generated mip chains, separate sRGB color, linear data-map, imported sampler cache entries, conservative bounds culling, a per-frame camera/light/environment/shadow UBO/API, punctual imported-light submission, direct PBR material shading with sampled renderer-generated irradiance and prefiltered environment cubemaps plus a sampled split-sum BRDF integration LUT, renderer-owned selection of cascaded directional shadows, spot 2D shadows, and point-light cubemap shadows, RenderDoc-friendly command labels around the viewport frame and key passes, basic glTF alpha mode/cutoff handling, back-to-front transparent primitive ordering, Scene View grid/axes/hierarchy/gizmo/entity-label/empty-marker color mesh draws, Lighting panel selected RGBA8/HDR texture environment input, and the upload caches needed by imported primitives/textures. Remaining post-Phase-6 work is transparent shadow policy, higher quality shadow filtering controls, broader renderer-owned text overlays, and future render-graph/bindless cleanup.
- Runtime LOD policy is non-destructive: LOD index buffers are uploaded and cached separately by `(asset, primitive, lod)`, draw batching includes the active LOD, and the full mesh remains available for close-up or selected Scene View editing.
- Vertex buffer ownership is separate from runtime LOD index buffers: one GPU vertex buffer is cached per imported primitive, and each active LOD owns only its index buffer. This avoids multiplying vertex uploads when the viewport chooses a different LOD.
- Static renderer resources are now prepared per batch before command recording. Shadow and mesh passes reuse the resolved `VulkanMeshBuffers` pointer and material descriptor instead of repeating mesh upload checks, material texture lookups, and descriptor lookup/update work in each pass.
- Assimp stays unintegrated in this phase because GLB/glTF coverage is real through TinyGLTF and no FBX/OBJ DoD was claimed.

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

- `cmake --preset dev-core`: configured successfully with Visual Studio 18 2026 after adding renderer-owned environment cubemap upload, BRDF LUT upload, shadow setup, RenderFrame-keyed environment cubemap cache entries, and the Lighting panel environment bridge.
- `cmake --build --preset dev-core`: built successfully after adding `RenderEnvironmentMap`, `RenderBrdfLut`, `RenderShadowSetup`, the descriptor split, the private material texture-set helper, and `VulkanEnvironmentTextureCache`.
- `ctest --preset dev-core --output-on-failure`: 7/7 tests passed in 34.13 seconds after generated environment cubemap, RenderFrame environment settings, RGBA8 source texture environment sampling, BRDF LUT, and renderer-owned directional/spot/point shadow-selection coverage were added.
- `cmake --preset dev-editor-local-qt`: configured successfully with Visual Studio 18 2026, Qt, ADS, Vulkan, tinygizmo, Im3d, TinyGLTF, MikkTSpace, and meshoptimizer.
- `cmake --build --preset dev-editor-local-qt`: built successfully and deployed Qt/ADS runtime dependencies after integrating environment cube descriptors, the BRDF LUT descriptor, and moving shadow setup out of the Qt viewport bridge.
- `ctest --preset dev-editor-local-qt --output-on-failure`: 8/8 offscreen tests passed in 35.98 seconds after the renderer-owned environment cubemaps, BRDF LUT, RenderFrame-keyed environment cache, Lighting panel bridge, selectable RGBA8 environment texture, and shadow setup were integrated.
- Visible Windows `projectunity_editor --smoke-test`: passed with exit code 0 after the same renderer changes and requires imported textured mesh, Scene View color mesh draws, candidate mesh stats, render CPU timing, shadow frames, shadow caster draws, persisted Lighting panel environment controls, imported texture assignment from Project Browser, IBL refresh after environment edits, and cached static resources not reuploading on the following frame. The same Vulkan presentation assertions are skipped under Qt's offscreen platform because it presents no Win32 Vulkan frames.
- Latest Lighting panel stability check: `cmake --build --preset dev-core` passed, `ctest --preset dev-core --output-on-failure` passed 7/7 in 32.99 seconds, `cmake --build --preset dev-editor-local-qt` passed, `ctest --preset dev-editor-local-qt --output-on-failure` passed 8/8 in 39.42 seconds, visible Windows `projectunity_editor --smoke-test` passed with exit code 0, and `git diff --check` reported no whitespace errors beyond existing LF/CRLF warnings.
- Latest repeated-asset batching check: `cmake --build --preset dev-core` passed, `ctest --preset dev-core --output-on-failure` passed 7/7 in 29.63 seconds, `cmake --build --preset dev-editor-local-qt` passed, `ctest --preset dev-editor-local-qt --output-on-failure` passed 8/8 in 34.42 seconds, visible Windows `projectunity_editor --smoke-test` passed with exit code 0, and `git diff --check` reported no whitespace errors beyond existing LF/CRLF warnings.
- Latest shadow-pass CPU work reduction check: `cmake --build --preset dev-core` passed, `ctest --preset dev-core --output-on-failure` passed 7/7 in 38.58 seconds, `cmake --build --preset dev-editor-local-qt` passed, `ctest --preset dev-editor-local-qt --output-on-failure` passed 8/8 in 42.89 seconds, and visible Windows `projectunity_editor --smoke-test` passed with exit code 0.
- Latest KTX/KTX2/progress/shadow-sampler check: `cmake --build --preset dev-core` passed, `ctest --preset dev-core --output-on-failure` passed 7/7 in 29.69 seconds, `cmake --build --preset dev-editor-local-qt` passed, `ctest --preset dev-editor-local-qt --output-on-failure` passed 8/8 in 32.89 seconds after the final import-progress UI adjustment, visible Windows `projectunity_editor --smoke-test` passed with exit code 0, `git diff --check` reported no whitespace errors beyond LF/CRLF warnings, and source-rule coverage confirmed all source files stay at or below 800 lines.
- Latest libktx KTX2 Basis transcode check: `cmake --preset dev-core` passed, `cmake --build --preset dev-core` passed, `ctest --preset dev-core --output-on-failure` passed 7/7 in 32.45 seconds, `cmake --preset dev-editor-local-qt` passed, `cmake --build --preset dev-editor-local-qt` passed, `ctest --preset dev-editor-local-qt --output-on-failure` passed 8/8 in 37.11 seconds, visible Windows `projectunity_editor --smoke-test` passed with exit code 0, `git diff --check` reported no whitespace errors beyond LF/CRLF warnings, and source-rule coverage confirmed all source files stay at or below 800 lines.
- Latest runtime screen-space LOD check: `cmake --build --preset dev-core` passed, `ctest --preset dev-core --output-on-failure` passed 7/7 in 35.89 seconds, `cmake --build --preset dev-editor-local-qt` passed, `ctest --preset dev-editor-local-qt --output-on-failure` passed 8/8 in 41.15 seconds, visible Windows `projectunity_editor --smoke-test` passed with exit code 0, `git diff --check` reported no whitespace errors beyond LF/CRLF warnings, and source-rule coverage confirmed all source files stay at or below 800 lines.
- Latest runtime LOD cache correction check: `cmake --build --preset dev-core` passed, `ctest --preset dev-core --output-on-failure` passed 7/7 in 38.12 seconds, `cmake --build --preset dev-editor-local-qt` passed, `ctest --preset dev-editor-local-qt --output-on-failure` passed 8/8 in 39.04 seconds, and visible Windows `projectunity_editor --smoke-test` passed with exit code 0.
- Latest renderer batch resource preparation check: `cmake --build --preset dev-core` passed, `ctest --preset dev-core --output-on-failure` passed 7/7 in 31.04 seconds, `cmake --build --preset dev-editor-local-qt` passed, `ctest --preset dev-editor-local-qt --output-on-failure` passed 8/8 in 35.07 seconds, and visible Windows `projectunity_editor --smoke-test` passed with exit code 0.
- Latest per-pass profiling check: `cmake --build --preset dev-core` passed,
  `ctest --preset dev-core --output-on-failure` passed 7/7 in 37.64 seconds,
  `cmake --build --preset dev-editor-local-qt` passed, `ctest --preset
  dev-editor-local-qt --output-on-failure` passed 8/8 in 34.89 seconds, visible
  Windows `projectunity_editor --smoke-test` passed with exit code 0, and source
  files stayed under the 800-line rule.
- Latest shadow-frustum culling check: `cmake --build --preset dev-core` passed,
  `ctest --preset dev-core --output-on-failure` passed 7/7 in 37.47 seconds,
  `cmake --build --preset dev-editor-local-qt` passed, `ctest --preset
  dev-editor-local-qt --output-on-failure` passed 8/8 in 36.87 seconds, visible
  Windows `projectunity_editor --smoke-test` passed with exit code 0, and source
  files stayed under the 800-line rule.
- Latest GPU timestamp query check: `cmake --build --preset dev-core` passed,
  `ctest --preset dev-core --output-on-failure` passed 7/7 in 35.81 seconds,
  `cmake --build --preset dev-editor-local-qt` passed, `ctest --preset
  dev-editor-local-qt --output-on-failure` passed 8/8 in 36.29 seconds, and visible
  Windows `projectunity_editor --smoke-test` passed with exit code 0.
- Latest HDR/KTX2/point-shadow check: `cmake --build --preset dev-core` passed,
  `ctest --preset dev-core --output-on-failure` passed 7/7 in 34.13 seconds after
  point-light shadow selection, `cmake --build --preset dev-editor-local-qt` passed,
  `ctest --preset dev-editor-local-qt --output-on-failure` passed 8/8 in 36.57
  seconds, and visible Windows `projectunity_editor --smoke-test` passed with exit
  code 0.
- Latest Phase 6 completion check after cascaded directional shadows, point-light
  cubemap shadows, and broad visual smoke: `cmake --build --preset dev-core` passed,
  `ctest --preset dev-core --output-on-failure` passed 7/7, `cmake --build --preset
  dev-editor-local-qt` passed, `ctest --preset dev-editor-local-qt --output-on-failure`
  passed 8/8, visible Windows `projectunity_editor --smoke-test` passed, and visible
  Windows `projectunity_editor --phase6-visual-smoke` passed after importing the 13 GLB
  assets in `Project/Assets/VisualVerification`.
- `projectunity_asset_tests` generates a real temporary GLB with a node transform, vertex colors, PBR material factors/maps, sampler wrap/filter state, occlusion strength, emissive texture state, mask alpha state, a punctual spot light, and a perspective camera, imports it, verifies that the transform, transformed bounds, vertex colors, factors, material maps, sampler state, light state, camera state, occlusion/emissive state, alpha mode, and alpha cutoff are preserved, validates tangent data, imports PNG and HDR textures, imports a generated KTX1 ASTC texture while checking GPU mip preservation and 1-100 progress events, imports a small KTX2 Basis texture through libktx when enabled and verifies BC7 GPU mip data plus RGBA fallback output, verifies cache records, imports the textured glTF example, and rejects a missing asset.
- `projectunity_asset_tests` also generates an asymmetric spatial GLB with left, right, front, back, `node.matrix`, quaternion-rotated, and negative-scale mirrored nodes. It verifies that left/right stays stable, glTF `-Z` maps to engine `+Z`, matrix translation remains column-major, quaternion rotation affects converted bounds, UVs are not flipped, and negative determinant converted transforms preserve bounds, normals, winding, and tangent handedness.
- `projectunity_scene_tests` verifies `MeshRendererComponent` scene roundtrip.
- `projectunity_renderer_tests` verifies opaque/masked before back-to-front blended primitive ordering, renderer-owned directional/spot/point shadow-map selection policy, deterministic BRDF integration LUT generation, deterministic irradiance/prefiltered environment cubemap generation, RenderFrame environment settings affecting generated cubemap data, RGBA8 and HDR source texture data affecting generated cubemap data, creates the Vulkan renderer, verifies ready state, GPU name, VMA allocator creation, lighting/shadow stat initialization, surface descriptor validation, and invalid surface rejection.
- `projectunity_renderer_tests` also verifies that compatible opaque draws are grouped adjacent so repeated imported assets can reach the Vulkan instance-batching path instead of recording one draw per copied entity.
- `projectunity_source_rule_tests` verifies code files stay at or below the 800-line project rule.
- `projectunity_editor_smoke` imports the textured glTF example into Project Browser, creates a mesh-renderer entity, requires the editor Vulkan renderer to initialize, and exercises the imported mesh bridge. A visible Windows smoke run also requires presented Vulkan mesh, textured-mesh, color-mesh, shadow-frame, and shadow-caster counters; the offscreen CTest run has no Win32 Vulkan presentation surface and keeps fallback coverage.

## Performance Corrections After Failed Optimization

Date: 2026-05-22

- Removed fixed preview triangle budgets and automatic low-detail viewport selection that degraded imported models.
- Removed destructive texture sampling/fallback behavior from the default mesh display path.
- Imported mesh data keeps optional meshoptimizer LODs for future controlled use, but Scene View does not use them by default to destroy visual fidelity.
- CPU-side QPainter fallback still cannot be the final performance solution; optimization must move to Vulkan GPU buffers, cached textures/materials, culling, mipmaps, and renderer-owned draw submission.
- Empty Scene View frames now use renderer-owned Vulkan color geometry for grid, axes,
  hierarchy links, labels, and transform-only entity markers when a real viewport
  surface is available.
- Idle mouse movement over a selected mesh no longer forces viewport repaint work.
- GLB/glTF and standalone texture imports now decode from the already-read source memory instead of reading the same file twice.
- The viewport uses opaque painting/no system background and disables global antialiasing in the CPU preview path.
- Mesh preview triangles outside the expanded viewport rectangle are culled before painting.
- Viewport, editor window, and asset manager implementation files were split so code files stay under the 800-line project rule.
- `MainWindowSmoke.cpp` now owns editor smoke coverage so `MainWindow.cpp` stays under the 800-line rule.
- Added `projectunity_source_rule_tests` to fail the build if a C/C++ source file exceeds 800 lines.
- `ctest --preset dev-core`: 7/7 tests passed in 1.66 seconds.
- `ctest --preset dev-editor-local-qt`: 8/8 tests passed in 6.29 seconds.

## Bugs Fixed During Phase

- Disabling TinyGLTF image write support left its default write callback unresolved during link. The single TinyGLTF implementation unit now compiles stb image and stb image write implementation together, and TinyGLTF examples are no longer added to the build graph.
- Editor smoke initially chose an unstable cache root under its test working directory. Editor cache root creation is now validated first and falls back to a temp cache root if needed.
- The first accessor reader used typed pointer casts over byte buffers. Accessors now copy scalar values from byte buffers before use.
- Imported assets with large triangle counts could push too much per-frame QPainter work in the editor preview. A first optimization skipped triangles and damaged mesh continuity; this was corrected so the viewport now keeps continuous geometry and uses real imported LOD data instead of destructive triangle sampling.
- Raw glTF mesh import ignored scene nodes and node transforms. The importer now traverses the default scene and applies each node world matrix before optimization/cache writes.

## Known Bugs

- A CPU/QPainter Scene/Game fallback still exists when Vulkan cannot present a viewport frame, including Qt offscreen tests. That fallback is not the final renderer path and remains too slow for real imported scenes.
- Vulkan imported mesh draws, color-space-aware base-color/normal/metallic-roughness/occlusion/emissive texture uploads with imported sampler state and generated mip chains, conservative viewport culling, texture descriptor binding, frame-uniform punctual lights, imported Game View camera selection, generated Vulkan irradiance/prefiltered environment cubemaps with a sampled Vulkan BRDF LUT, renderer-owned cascaded directional shadows, spot 2D shadows, point-light cubemap shadows with weighted PCF and masked-caster alpha discard, glTF alpha mode/cutoff handling, primitive-level `BLEND` ordering, Scene View color mesh draws, entity labels, transform-only object markers, Lighting panel selected RGBA8/HDR environment textures, editor-visible renderer counters, GPU timestamp queries, BC7-preferred KTX2 Basis/UASTC import, and broad visual smoke coverage over representative imported GLB content exist.

## Renderer Refactor Notes

- Vulkan platform setup, support queries, renderer orchestration, and viewport target ownership were split across private renderer files to keep code files under the 800-line project rule.
- `ViewportWidgetRenderer.cpp` owns Qt viewport-to-renderer surface/frame bridging, keeping `ViewportWidget.cpp` below the file-size limit.
- `MainWindowSmoke.cpp` owns smoke-test orchestration, keeping `MainWindow.cpp` below the file-size limit.
- `MainWindowAssets.cpp` owns Project Browser import orchestration and the Asset
  Import status panel, keeping `MainWindow.cpp` below the file-size limit while
  avoiding a fake percentage for importer stages that are not instrumented yet.
- GLSL textured mesh shaders are compiled to SPIR-V at build time and embedded into the renderer binary through a CMake script, avoiding runtime absolute shader paths.

## Bugs Fixed During Renderer Integration

- A failed viewport swapchain creation could leak a `VkSurfaceKHR` because the target constructor threw after surface creation. Partial Vulkan resources are now destroyed before rethrowing.
- `MainWindow` disconnected viewports too late relative to member destruction. It now clears viewport renderer pointers and releases presentation resources before destroying the renderer.
- A clear-only Vulkan frame hid Scene View grid/overlay aids when no imported mesh
  draw existed. The viewport bridge now keeps that empty-scene case on the Qt
  fallback while the GPU path is used for imported mesh frames.
- Closed editor docks were hard to recover. The `Window` menu now exposes dock
  toggle actions and `Reset Layout` restores the default ADS layout.
- The selected gizmo disappeared over Vulkan-presented mesh frames because the Qt
  overlay was not reliably visible over the swapchain. A Vulkan editor color mesh
  pass now draws the selected tinygizmo geometry during imported mesh frames.
- Scene View grid/debug lines could flash for one Qt paint and then disappear after
  switching from Game View because Vulkan presented over the QPainter overlay. The
  viewport now submits controlled grid/axes/hierarchy lines as Vulkan color geometry and
  skips QPainter world-space overlay draws after a successful Vulkan mesh frame.
- The first GPU Scene View aid pass accidentally rendered camera frustum debug geometry
  as a large translucent shape. That path now submits only bounded grid, axes, hierarchy,
  and gizmo geometry until a proper debug-line render pass exists.
- The combined Scene View color draw initially appended tinygizmo vertices after grid
  vertices but did not rebase tinygizmo triangle indices. Rotate/move/scale handles could
  connect to grid vertices and form huge broken translucent triangles. The gizmo indices
  are now offset to their appended vertex range.
- Vulkan texture uploads now generate mip chains with `vkCmdBlitImage` when the
  chosen RGBA8 linear or sRGB material texture format supports linear blits, and
  use a base-mip fallback otherwise.
- Mesh primitive bounds are now cached during import and refreshed after glTF node
  transforms. The editor viewport uses those bounds for conservative camera culling
  before submitting Vulkan mesh draws.
- glTF material factors were previously ignored except base color. The importer now
  preserves metallic, roughness, and emissive factors; the Vulkan mesh push constants
  and shader use them for a first material preview approximation.
- glTF `COLOR_0` vertex colors were previously ignored. The importer now preserves
  them, the Vulkan mesh cache uploads them per vertex, and the mesh shader multiplies
  them with base-color texture/material output.
- glTF normal textures and metallic-roughness textures were imported only as decoded
  image bytes before this material pass. `MaterialAsset`, render draw submission,
  descriptor sets, and the Vulkan mesh shader now carry and sample those maps with
  a cached flat-normal fallback for materials that omit a normal map.
- glTF occlusion texture state was still decoded but not shaded. `MaterialAsset`,
  render draw submission, descriptor sets, and the Vulkan mesh shader now preserve
  the occlusion texture and strength and apply it to the current ambient term.
- glTF emissive texture state did not reach Vulkan. `MaterialAsset`, draw
  submission, descriptor sets, and the Vulkan mesh shader now carry the emissive
  texture and multiply it with the already imported emissive factor.
- All decoded RGBA material textures were uploaded through one linear Vulkan cache
  path. Base-color and emissive color inputs now request sRGB image uploads while
  normal, metallic-roughness, and occlusion data maps keep linear uploads, with
  distinct cache entries when one source image is reused across those roles.
- Imported glTF texture objects had their sampler state dropped and every Vulkan
  material texture used repeat/linear sampling. `TextureAsset` now preserves wrap
  and filter state, the cache differentiates sampler variants, and Vulkan uploads
  skip generated mips when the imported minification path does not use them.
- glTF alpha state was not preserved after import. `MaterialAsset` now keeps alpha
  mode/cutoff, and the Vulkan mesh shader treats opaque, masked, and blended
  material output distinctly.
- glTF `doubleSided` state is now preserved in `MaterialAsset`. Vulkan uses
  back-face culling for single-sided opaque/transparent materials and separate
  no-cull pipelines for imported double-sided materials, avoiding both unnecessary
  overdraw and incorrect removal of intentional two-sided surfaces.
- The first alpha pass forwarded glTF `BLEND` values but kept the draw list in
  raw submission order with depth writes enabled. Renderer draw ordering now
  keeps opaque/masked primitives first, sorts blended primitive draws
  back-to-front from viewport camera depth, and binds a transparent pipeline
  that still depth-tests without writing depth.
- The first directional shadow pass initially cast every masked primitive as solid
  geometry. `ShadowDepth.frag` now samples the base-color alpha and respects imported
  `MASK` cutoff values for opaque/masked shadow casters, while `BLEND` casters stay
  skipped until transparent shadow policy is designed.
- Renderer counters for last-frame light count, last-frame shadow casters, total shadow
  frames, and total shadow caster draws now flow through `RendererStats`, the visible
  editor smoke test, and the Profiler tab.
- Renderer counters for candidate mesh primitives, culled mesh primitives, last-frame
  render CPU time, and averaged render CPU time now flow through `RendererStats`, the
  visible editor smoke test, and the Profiler tab.
- Renderer counters now also track candidate/culled/visible triangle counts, resident
  mesh and texture cache sizes, mesh/texture upload counts, static upload bytes, and
  dynamic editor color overlay upload bytes. The editor Profiler tab and visible smoke
  test validate those counters so large imported scenes can be measured without guessing
  whether assets are being reuploaded every frame. The visible smoke path now renders a
  second imported-asset frame and fails if cached static meshes/textures upload again.
- Directional shadows now render four visible-bounds cascades into a 4096 2D atlas,
  snapping each light projection to atlas texels. The mesh shader chooses the first
  cascade covering the fragment and uses a Vulkan comparison sampler with weighted PCF
  filtering to reduce shimmer and harsh shadow edges without changing imported mesh data.
- Shadow-map selection now lives in `engine/renderer`, not the Qt viewport bridge. The
  renderer prefers cascaded directional shadows when a directional light exists and
  falls back to a perspective 2D spot-light shadow map when no directional light is
  present, then renders six point-light cubemap faces when only point lights are
  available.
- The mesh shader now samples renderer-generated irradiance and prefiltered
  environment cubemaps plus the generated BRDF LUT for ambient terms.
- The BRDF half of split-sum IBL now uses a generated RGBA8 integration LUT uploaded
  through `VulkanTextureCache` and bound to the material descriptor set. Diffuse and
  specular radiance now come from Vulkan cube maps generated by the renderer instead
  of shader-local procedural functions.
- Vulkan command buffers now emit debug labels for the viewport frame, shadow pass,
  mesh pass, and Scene View aid pass when `VK_EXT_debug_utils` entry points are available,
  making RenderDoc captures navigable without hard dependencies on the extension.
- Scene View entity labels no longer depend on a QPainter overlay above the Vulkan
  swapchain. The editor now emits a bounded GPU billboard text mesh into the Vulkan
  color pass, keeping labels visible with imported mesh frames while capping label
  generation to avoid editor overlay spikes.
- Empty `GameObject` entities now draw a Vulkan color marker box/pivot in Scene View,
  fixing transform-only objects that previously existed in the hierarchy but were only
  visible through the CPU/QPainter fallback path.
- The Lighting / Bake panel now edits sky RGB, ground RGB, and IBL intensity values,
  persists them through editor `QSettings`, sends them to Scene View and Game View as
  `RenderFrame::environment`, and visible smoke coverage verifies that changing them
  refreshes Vulkan IBL textures without causing another static resource upload on the
  following frame.
- The Lighting / Bake panel can now assign an imported Texture2D row from the Project
  Browser as an equirectangular RGBA8 or HDR environment source. The renderer samples
  that texture into generated irradiance and prefiltered cubemaps keyed by the texture
  asset ID, dimensions, byte count, and intensity, while preserving the original texture
  bytes unchanged.
- HDR texture import stores floating-point RGBA radiance and an RGBA8 preview/fallback.
  HDR environment cubemaps keep float radiance and upload as
  `VK_FORMAT_R16G16B16A16_SFLOAT` when the selected Vulkan device supports sampled
  linear filtering for that format.
- The Lighting / Bake panel now debounces sky/ground/intensity spinbox edits, auto-selects
  the most recently imported Project Browser row, labels procedural mode versus texture
  environment mode explicitly, and exposes `Reset Defaults`. `Clear Texture` only returns
  the environment to procedural mode; `Reset Defaults` also restores sky, ground, and
  intensity values.
- Vulkan material descriptor sets are now reset when the active generated irradiance or
  prefiltered environment cubemap key changes, preventing descriptor-pool growth and
  stale environment descriptors during repeated Lighting panel edits.
- Repeated imported models now scale through renderer batching more cleanly: opaque and
  masked mesh draws are sorted by compatible mesh/material/texture state before
  `VulkanViewportTarget::buildMeshBatches`, and the Profiler exposes `Mesh batches last`
  alongside raw submitted draw count. This preserves asset fidelity and reduces command
  recording for duplicated NodePerformance-style scenes instead of lowering quality.
- Reimporting the same source GLB/texture in one editor session now refreshes the
  active in-memory asset instance for that stable asset id instead of leaving the
  first imported copy ahead of newer importer output.
- A Phase 6 visual/profiling asset pack now lives under
  `Project/Assets/VisualVerification`, with source, license notes, import order, and
  expected checks documented in `docs/phase6_visual_verification_assets.md`.
- The local Vulkan Samples `vokselia` pack is a glTF-plus-external-KTX stress
  case, not proof that every hardware path is complete. Current Phase 6 can parse
  the `.gltf` structure/geometry path, import/upload external explicit KTX/KTX2
  payloads when the selected Vulkan device supports the stored GPU format, and
  transcode KTX2 Basis/UASTC through libktx to a BC7 GPU candidate plus RGBA8 fallback
  bytes for upload/environment use.
- Large glTF files with thousands of nodes/primitives now keep renderer-friendly
  representation without reducing asset fidelity: repeated mesh primitives are
  shared when possible, repeated textures/materials are deduplicated, large flat
  material scalar factors are baked into internal vertex attributes, and very large
  primitive sets are combined into static material batches. `NodePerformanceTest.glb`
  is covered by an optional asset test when the local verification pack exists.
- Vulkan mesh and shadow shaders now consume an instance matrix vertex stream, and
  the viewport renderer batches compatible mesh draws with
  `vkCmdDrawIndexed(..., instanceCount)` instead of forcing one GPU draw per scene
  instance when the mesh/material state is shareable.
- The Vulkan mesh cache now keys GPU index buffers by runtime LOD as well as asset
  primitive. The viewport chooses those LODs by projected screen radius, reducing
  submitted triangles when a huge scene is fully visible from far away while keeping
  full-resolution editing for close and selected objects.
- The initial runtime LOD implementation duplicated vertex buffers per LOD and could
  stall the first visible frame after importing a large scene. The mesh cache now shares
  vertex buffers per primitive and preuploads static mesh/material resources from
  renderer batches instead of raw draw submissions.
- Large-scene optimization is being treated as renderer architecture work, not a single
  asset-size fix. Current local code now follows the first practical Vulkan guidance
  step: reduce repeated CPU-side resource lookup/bind preparation around the current
  direct draw path before attempting GPU-driven indirect rendering.
- The shadow pass no longer prepares full material texture descriptors for every
  opaque caster. Opaque casters share a default descriptor for the frame, while
  `MASK` casters still bind base-color alpha so cutoff shadows stay correct.
- KTX-Software is added as an `EXCLUDE_FROM_ALL` FetchContent dependency and only
  `ktx_read` is linked. This avoids building KTX tool/version helper targets that
  can be blocked by Windows protected-folder policy while still enabling import-time
  KTX2 transcode through the asset module.
- Adding vertex color support pushed `AssetManager.cpp` over the 800-line project
  rule. Attribute/accessor decoding was split into `GltfAttributeReader` and
  `projectunity_source_rule_tests` passes again.

## Later Phase Work

- Complete renderer-owned material/shader coverage, lighting integration, and GPU editor
  overlay passes remain renderer and lighting work after this first imported mesh path.
- Thumbnails, asset database persistence, additional compressed GPU target choices for Basis/UASTC, and build-time asset packaging remain later asset pipeline work.
- Assimp import for FBX/OBJ remains optional later work after its own integration decision.

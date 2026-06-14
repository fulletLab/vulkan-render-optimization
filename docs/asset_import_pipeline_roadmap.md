# Asset Import Pipeline Roadmap

Status: PARCIAL / architecture and evaluation plan

## Goal

Build a robust offline-first import pipeline that accepts common DCC formats, preserves
per-primitive materials and source provenance, cooks them into `ModelAsset` / `.ffult`,
and leaves the Vulkan renderer consuming only the existing internal asset model.

This roadmap does not include finding large benchmark scenes. The local verification
pack and externally selected stress assets are the benchmark inputs.

## Non-Goals

- Do not build a Blender-style editor.
- Do not parse `.blend` files inside the engine.
- Do not route every format through Assimp.
- Do not expose TinyGLTF, fastgltf, cgltf, Assimp, Blender, or OpenImageIO types to the
  renderer, scene module, or editor viewport.
- Do not improve performance by dropping triangles, textures, materials, UVs, normals,
  tangents, vertex colors, alpha state, or node transforms.
- Do not replace a working dependency until parity and benchmark evidence exist.

## Current Baseline

ProjectUnity already has more of this pipeline than an initial roadmap may suggest:

- TinyGLTF imports GLB/glTF from memory in `engine/assets/src/AssetManager.cpp`.
- `ModelAsset` stores primitives, primitive instances, editor instances, materials,
  textures, lights, and cameras.
- Every `MeshPrimitive` already carries its own `materialIndex`.
- `MaterialAsset` already preserves base color, metallic/roughness, normal, occlusion,
  emissive, alpha mode/cutoff, double-sided state, and texture slots.
- MikkTSpace, meshoptimizer, spatial batching, primitive clusters, non-destructive LODs,
  KTX/KTX2, Basis/UASTC transcoding through libktx, and `.ffult` caching are integrated.
- Vulkan resolves material state from each primitive and includes material compatibility
  in draw ordering and batching.

The next work is therefore adapter separation, source provenance, importer parity, and
new source formats. It is not a renderer rewrite.

## Architecture Decision

Use this data flow:

```text
External source
  GLB/glTF | OBJ/MTL | FBX | DAE | BLEND | future USD
        |
        v
Format adapter
  fastgltf/cgltf/TinyGLTF | Assimp | Blender CLI | future OpenUSD
        |
        v
ImportedScene IR
  source nodes, meshes, primitives, material slots, textures, provenance
        |
        v
Shared validation and cooker
  coordinates, normals/tangents, texture policy, meshoptimizer, LOD, batching
        |
        v
ModelAsset + import diagnostics
        |
        v
FFULT cache/package
        |
        v
Scene/RenderWorld/Vulkan renderer
```

`ImportedScene` should be introduced when the second importer backend is added. It is a
small parser-neutral transfer model, not a second runtime asset system. Parser objects
must die at the adapter boundary. The existing `ModelAsset` remains the cooked/runtime
contract consumed by the renderer.

The shared cooker must own behavior currently mixed into the TinyGLTF path:

- coordinate conversion and node traversal;
- normal and MikkTSpace tangent generation;
- texture decode/import policy and color-space classification;
- primitive validation and bounds;
- meshoptimizer index/vertex optimization and optional LOD generation;
- deduplication, spatial batching, clusters, diagnostics, and FFULT writing.

## Material And Primitive Contract

These invariants apply to every importer backend:

1. Every output primitive has exactly one valid `materialIndex`.
2. A source mesh using multiple materials becomes multiple primitives.
3. OBJ `mtllib` is resolved relative to the OBJ, and every `usemtl` boundary is
   preserved as a material primitive boundary.
4. Textures resolve through the material, never through a model-global texture field.
5. Base-color/emissive color textures are tagged sRGB. Normal, metallic/roughness,
   occlusion, masks, and other data maps stay linear.
6. Alpha mode, alpha cutoff, double-sided state, sampler state, UV set, and texture
   transform are explicit import data. Unsupported semantics produce diagnostics rather
   than silently becoming a global default.
7. Batching may combine only compatible pipeline/material/alpha/cull state.
8. Batching must never remap a primitive to a different material merely to reduce draws.
9. Material edits invalidate and rebuild affected cooked batches. They must not patch a
   baked multi-source batch without knowing which source geometry contributed to it.

The current batcher preserves one `materialIndex` per cooked primitive, but a future
per-sub-mesh editor also needs provenance. Add stable source identifiers before the
material editor:

- `sourceMeshId` and `sourcePrimitiveId` for imported primitive identity;
- `sourceMaterialSlotId` for editable material-slot identity;
- cooked primitive contribution records mapping back to source primitives/instances;
- import diagnostics recording source and cooked primitive/material counts.

This provenance may live in editor/import metadata rather than hot renderer structs, but
it must survive FFULT if FFULT is expected to support editing without the original source
being re-parsed.

## Library Decisions

| Area | Decision | Reason |
| --- | --- | --- |
| GLB/glTF | Spike fastgltf first; keep TinyGLTF as the baseline; use cgltf as the minimal control implementation. | fastgltf targets modern C++ and parse speed. cgltf is compact and dependency-free. The existing TinyGLTF path is already featureful and tested. |
| OBJ/MTL | Assimp secondary adapter, first Assimp milestone. | Avoid a handwritten OBJ/MTL parser while explicitly testing `mtllib`, `usemtl`, texture paths, and alpha. |
| FBX/DAE | Assimp secondary/offline adapter after OBJ acceptance. | Broad format coverage is valuable, but material and transform fidelity must be verified per format. |
| BLEND | Blender command line/headless export to GLB, then the normal glTF adapter. | Assimp documents BLEND support as deprecated; Blender owns the format and can evaluate modifiers and bake procedural content. |
| Images | Keep stb and libktx paths. Add OpenImageIO only to an offline cooker/tool target when broader formats, bit depths, metadata, or color processing are required. | OIIO is a professional image pipeline but is a larger dependency than the current runtime needs. |
| GPU textures | Keep KTX-Software/libktx as the Vulkan-facing compressed texture path. | KTX2 and Basis Universal are already integrated and match the renderer's needs. |
| Geometry optimization | Keep meshoptimizer in the shared cooker. | It already provides cache/fetch/overdraw optimization and simplification without coupling to a parser. |
| Draco | Add an optional Draco decoder behind the glTF adapter, not as a renderer feature. | Parsers expose the extension differently; decoded geometry should enter the same `ImportedScene` primitive path. |
| Material graphs | Add a ProjectUnity-owned `MaterialGraphAsset`; evaluate MaterialX only as optional interchange/shader generation. | The engine needs a stable internal graph independent from UI and third-party object models. |
| Node UI | Spike QtNodes from `paceholder/nodeeditor`; keep native `QGraphicsScene` / `QGraphicsView` as fallback. | QtNodes is pure Qt/C++, BSD-3-Clause, supports Qt 6, and separates graph model from graphics view. Any ImGui-based option is rejected. |
| USD | Use an OpenUSD-based adapter in a future phase, not Assimp as the authoritative USD path. | USD composition, references, variants, time samples, and asset resolution need USD semantics before flattening to `ImportedScene`. |

The complete material graph, Qt UI, FFULT serialization, preview, and MaterialX boundary
is defined in `docs/material_graph_architecture.md`.

### Important Extension Note

Do not select a glTF parser from headline parse speed alone. The official fastgltf
extension list includes `EXT_meshopt_compression` and many material extensions but does
not currently list `KHR_draco_mesh_compression`. cgltf recognizes the Draco extension but
requires an external Draco library to decode it. TinyGLTF remains the current behavior
baseline. The spike must test the extensions ProjectUnity actually intends to support.

## Backend Boundary

The backend API should remain narrow. A function table or small interface is enough:

```cpp
struct ModelImportRequest {
    std::filesystem::path sourcePath;
    std::span<const std::uint8_t> sourceBytes;
};

struct ImportedSceneResult {
    ImportedScene scene;
    ImportDiagnostics diagnostics;
};

class IModelImporterBackend {
public:
    virtual ~IModelImporterBackend() = default;
    virtual bool supports(const std::filesystem::path& path) const = 0;
    virtual ImportedSceneResult import(const ModelImportRequest& request) = 0;
};
```

Do not add runtime plugin loading for this. Static CMake-selected backends are enough.
The abstraction becomes useful only when at least two real backends share the cooker.

## Benchmark And Diagnostics Contract

Measure source parsing separately from cooking and runtime rendering.

Import/cook metrics:

- source bytes and format;
- parser backend and pinned dependency revision;
- parse, texture decode, geometry cook, batching, FFULT write, and total time;
- peak CPU memory when practical;
- source and cooked node, mesh, primitive, instance, material, and texture counts;
- vertex/index counts before and after optimization, without counting LOD reduction as
  source fidelity loss;
- image decoded bytes, compressed GPU bytes, FFULT bytes, warnings, and unsupported
  extension/material counts.

Runtime metrics already available or to retain:

- candidate/submitted/culled draws and triangles;
- RenderWorld chunks, instances, batches, and visible counts;
- material/pipeline switches and descriptor pressure;
- upload bytes, upload waits, and first-visible-frame time;
- CPU record/prepare time and GPU pass timestamps;
- shadow candidates, culled casters, submitted shadow batches, and shadow CPU/GPU time;
- FPS/frame time only as the final outcome, not the only metric.

Use at least three stress classes:

1. Geometry-heavy: high vertex/index count with moderate material count.
2. Texture-heavy: large images, many mip levels, mixed color/data maps, and alpha.
3. Submission-heavy: thousands of small primitives/instances and many materials.

## Implementation Phases

### Phase 0: Lock The Contract

- Add generated multi-material fixtures independent of third-party benchmark assets.
- Cover GLB primitives with distinct materials and textures.
- Cover OBJ + MTL with multiple `usemtl` regions, relative texture paths, spaces in paths,
  missing textures, normal maps, opacity/cutout, and duplicate material names.
- Add import diagnostics and material/primitive invariant checks.
- Add source provenance fields or metadata before any editor material work.

Definition of Done:

- Existing TinyGLTF behavior and Vulkan visual smoke remain unchanged.
- Tests fail if a primitive loses or changes its material assignment through cooking,
  batching, FFULT write/read, RenderWorld compilation, or shadow submission.

### Phase 1: Extract The Shared Cooker

- Move TinyGLTF-specific parsing into a glTF adapter.
- Move parser-neutral post-processing out of `AssetManager.cpp`.
- Keep `AssetManager` responsible for dispatch, progress, cache lookup/write, and storage.
- Keep the renderer and scene APIs unchanged.

Definition of Done:

- TinyGLTF remains the only active backend, but all current asset, renderer, editor, and
  visual smoke tests pass through the new boundary.
- FFULT output is equivalent for the existing fixture corpus, except for an intentional
  documented version bump if provenance is serialized.

### Phase 2: Evaluate fastgltf And cgltf

- Add both as optional, pinned, off-by-default evaluation dependencies.
- Implement only the feature slice currently supported by ProjectUnity first.
- Run the same bytes and fixtures through TinyGLTF, fastgltf, and cgltf.
- Compare structural output before comparing speed.

Selection gates:

- exact primitive/material/texture slot parity;
- node transform, winding, tangent, camera, light, alpha, and sampler parity;
- external `.gltf` buffers/images and embedded GLB parity;
- malformed input diagnostics and no crashes;
- lower parse/cook wall time or memory on representative large assets;
- maintainable extension path for Basis/KTX, meshopt compression, and Draco.

Recommended decision: prefer fastgltf if it reaches parity cleanly and wins the measured
large-file path. Choose cgltf if its simpler data model produces a smaller, safer adapter
or if required extension handling is materially better. Do not replace TinyGLTF merely
because a microbenchmark parses JSON faster.

### Phase 3: Integrate The Selected glTF Backend

- Select the backend with a build option and keep TinyGLTF as a rollback/reference path
  for one stabilization period.
- Add A/B import reports for the benchmark corpus.
- Remove the old path only after parity tests and visible Vulkan smoke coverage pass.
- Add Draco as a separate optional milestone if the selected parser does not decode it.

Definition of Done:

- No renderer source change is required to switch parser backends.
- Existing GLB/glTF scenes produce equivalent ModelAsset/FFULT semantics.
- Import time and peak memory results are recorded for all three stress classes.

### Phase 4: Assimp Offline Formats

Implement in this order:

1. OBJ + MTL.
2. DAE.
3. FBX.

Use conservative Assimp post-process flags. Triangulation and validation are useful, but
do not enable material merging, graph flattening, UV flipping, coordinate conversion, or
mesh joining without a ProjectUnity-owned reason and parity test. Normalize everything
through the shared cooker.

Definition of Done per format:

- materials and texture semantics survive into ModelAsset and FFULT;
- transforms and units are documented and tested;
- `usemtl` or source material partitions remain distinct primitives;
- alpha and normal mapping reach Vulkan correctly;
- unsupported source features produce actionable diagnostics.

### Phase 5: Blender Conversion Tool

- Add a headless tool under `tools/`, not an engine dependency.
- Invoke a pinned/minimum supported Blender version with a repository-owned Python export
  script and explicit GLB export options.
- Capture Blender version, command, exit code, stdout/stderr, source timestamp/hash, and
  generated GLB hash in import metadata.
- Export to a deterministic cache path, then feed the generated GLB through the selected
  glTF adapter.
- Treat procedural-node baking as an explicit preprocessing option with clear errors when
  an asset cannot be represented faithfully in glTF.

Definition of Done:

- `.blend` import never links Blender libraries into ProjectUnity.
- Missing Blender, timeout, export failure, and unsupported material cases are visible to
  the editor and do not leave a valid-looking stale cache entry.

### Phase 6: Professional Texture Cooking

- Keep current stb/libktx behavior as the compatibility path.
- Add OpenImageIO to an optional offline tool when formats or processing justify it.
- Define source color space, alpha association, channel packing, normal-map policy,
  mip generation, and KTX2/Basis target settings in cooker options.
- Preserve an RGBA fallback only where the current Vulkan capability path requires it.

Definition of Done:

- texture processing is reproducible and included in the cache key;
- color textures and data maps cannot be silently interchanged;
- compressed GPU payload choice is visible in diagnostics and Profiler upload metrics.

### Phase 7: Simple Material Editor

- Show source material slots and source primitives, not only cooked batch primitives.
- Allow base color, roughness, metallic, normal, texture, alpha, and shadow policy edits.
- Separate asset-default edits from scene-instance overrides.
- Use explicit precedence for shadow policy: instance override, source primitive override,
  material default, then renderer default.
- Re-cook affected batches when an edit changes batch compatibility.

Definition of Done:

- editing one material slot does not alter unrelated primitives that happened to be
  combined into a cooked batch;
- material changes survive scene save/load and asset reimport policy;
- draw, descriptor, and shadow metrics expose the cost of the edit.

### Future: MaterialX, Node UI, And USD

- Add the engine-owned `MaterialGraphAsset` before integrating a node UI.
- Use QtNodes only as an editor adapter over that graph, with native Qt Graphics View as
  the fallback. ImGui-based node libraries remain rejected.
- Add MaterialX only after the fixed PBR material editor and shader permutation model are
  stable, and keep it optional as interchange/shader generation rather than UI.
- Evaluate OpenUSD as its own scene adapter. Decide whether to preserve live composition
  or flatten a selected stage/variant into ImportedScene before implementation.

## Immediate Execution Order

1. Add provenance and invariant tests around the current TinyGLTF -> ModelAsset -> FFULT
   -> batching -> Vulkan path.
2. Extract a parser-neutral cooker while preserving current output.
3. Implement the fastgltf spike and cgltf control behind build options.
4. Select the glTF backend from parity plus measured results.
5. Add Assimp OBJ/MTL as the first new external format.

This order protects the current Vulkan renderer, makes library evaluation meaningful,
and prevents the future material editor from being blocked by baked batches that no
longer know which source sub-mesh they came from.

## Primary References

- fastgltf: https://github.com/spnda/fastgltf
- cgltf: https://github.com/jkuhlmann/cgltf
- TinyGLTF: https://github.com/syoyo/tinygltf
- Assimp: https://github.com/assimp/assimp
- Blender command line and glTF export: https://docs.blender.org/manual/en/latest/advanced/command_line/index.html
- OpenImageIO: https://openimageio.readthedocs.io/en/latest/
- KTX-Software: https://github.com/KhronosGroup/KTX-Software
- meshoptimizer: https://github.com/zeux/meshoptimizer
- Draco: https://github.com/google/draco
- MaterialX: https://github.com/AcademySoftwareFoundation/MaterialX
- OpenUSD: https://openusd.org/release/intro.html

# Material Graph Architecture

Status: PARCIAL / design approved for a future implementation phase

## Decision Summary

- ProjectUnity owns a renderer-independent `MaterialGraphAsset` data model.
- The graph is serializable as an internal asset and does not contain Qt, QtNodes,
  MaterialX, Vulkan, or parser-library types.
- The first UI spike uses QtNodes from `paceholder/nodeeditor`, a Qt Widgets/C++ library
  built on `QGraphicsScene` and `QGraphicsView`.
- A native `QGraphicsScene` / `QGraphicsView` implementation remains the fallback because
  it can consume the same `MaterialGraphAsset` model.
- Dear ImGui, imgui-node-editor, ImNodeFlow, imnodes, and any other ImGui dependency are
  rejected for this project.
- MaterialX is an optional interchange and shader-generation backend. It is not the UI
  and is not the authoritative ProjectUnity graph representation.
- Imported mesh primitives keep using `materialIndex`; a graph is assigned to an
  individual material slot, never globally to the complete model.

## Qt UI Evaluation

### Recommended Spike: QtNodes

Repository: https://github.com/paceholder/nodeeditor

The library name is QtNodes even though the repository is named `nodeeditor`.

Why it fits:

- BSD-3-Clause license.
- Qt Widgets/C++ implementation with no Dear ImGui dependency.
- Qt 6 support and a CMake target.
- Model/view architecture through `AbstractGraphModel`.
- `BasicGraphicsScene` and `GraphicsView` are based on `QGraphicsScene` and
  `QGraphicsView`.
- Dynamic typed ports, custom painting, undo/redo, copy/paste, and custom node geometry.
- The graph model can operate without a visible scene.

Evaluate the stable `3.0.16` tag, commit
`7c6341a66a8e46b8988140b9e60d892b6a3560b3`, dated February 10, 2026. Pin an exact
commit if the spike is accepted; do not track the development branch.

Integration cautions:

- Derive a ProjectUnity adapter from `AbstractGraphModel`; do not use QtNodes'
  `DataFlowGraphModel` as the engine material model.
- Do not use QtNodes JSON as the asset format. All save/load operations go through
  ProjectUnity serialization.
- Keep QtNodes in an editor-only module. `engine/assets`, renderer runtime, and server
  targets must not link QtNodes or Qt.
- QtNodes 3.0.16 links Qt Core, Gui, Widgets, and OpenGL, although its `GraphicsView.cpp`
  only includes the QtOpenGL umbrella in the reviewed source. The spike must determine
  whether that unused dependency can be removed or isolated. It must not create an
  OpenGL material preview path; ProjectUnity's preview remains Vulkan.
- Stress-test selection, movement, zoom, connection editing, undo, and repaint cost with
  at least 1,000 nodes and 2,000 links before committing to the dependency.

### Native Qt Fallback

A custom implementation using `QGraphicsScene`, `QGraphicsView`, `QGraphicsObject`, and
`QPainterPath` is acceptable if QtNodes blocks styling, performance, or ownership rules.
Qt documents `QGraphicsScene` specifically for managing large numbers of 2D items and
provides both BSP and no-index modes. A dynamic node editor should begin with `NoIndex`,
an explicit scene rectangle, bounded repaint regions, and measured zoomed-out behavior.

The fallback costs more editor code because ProjectUnity would own connection gestures,
selection, copy/paste, undo commands, layout, pin hit-testing, styling, and accessibility.
It does not change the internal graph or serialization design.

### Rejected Or Deferred Options

| Option | Decision | Reason |
| --- | --- | --- |
| imgui-node-editor | Reject | Dear ImGui based. |
| ImNodeFlow | Reject | Dear ImGui based. |
| imnodes | Reject | Dear ImGui based. |
| QuickQanava | Defer | Pure Qt/C++, but its presentation layer is QML/Qt Quick. ProjectUnity currently uses Qt Widgets and would gain another UI/deployment stack. |
| NodeLink | Defer | Qt/QML-oriented and does not fit the current Qt Widgets editor as directly as QtNodes. |
| facontidavide/QtNodeEditor | Reject for new integration | Historical QtNodes line centered on the older API; use the maintained paceholder v3 model instead. |
| MaterialX Graph Editor | Reject as ProjectUnity UI | It is an example application and does not replace the engine-owned graph/UI boundary. |

## Ownership Boundaries

```text
engine/assets
  MaterialGraphAsset
  validation
  deterministic serialization
  semantic hashing
        |
        +-----------------------+
        |                       |
        v                       v
tools/material_compiler   editor/material_graph
  graph lowering            QtNodes adapter
  optional MaterialX        Qt Widgets panels
  shader/cache output       undo/redo commands
        |                       |
        v                       v
compiled material cache    preview requests
        |                       |
        +-----------+-----------+
                    v
             Vulkan renderer
       compiled shaders and bindings only
```

Rules:

- `engine/assets` contains no Qt or MaterialX includes.
- `editor/material_graph` contains no Vulkan compilation logic.
- MaterialX conversion lives in an optional tool/compiler target.
- The renderer never traverses an editable graph while recording a frame.
- Shader generation and pipeline creation never run synchronously on the render thread.

## Internal Asset Model

The exact C++ placement can be introduced later as
`engine/assets/include/projectunity/assets/MaterialGraphAsset.hpp`. The intended shape is:

```cpp
using MaterialGraphNodeId = core::StableId;
using MaterialGraphPinId = core::StableId;
using MaterialGraphConnectionId = core::StableId;
using MaterialGraphParameterId = core::StableId;

enum class MaterialGraphValueType : std::uint8_t {
    Bool,
    Int,
    Float,
    Float2,
    Float3,
    Float4,
    Color3,
    Color4,
    Texture2D,
};

enum class MaterialGraphPinDirection : std::uint8_t {
    Input,
    Output,
};

struct MaterialGraphPin {
    MaterialGraphPinId id;
    std::string key;
    std::string displayName;
    MaterialGraphPinDirection direction;
    MaterialGraphValueType valueType;
    MaterialGraphValue defaultValue;
    bool allowsMultipleConnections {false};
};

struct MaterialGraphNode {
    MaterialGraphNodeId id;
    std::string typeId;
    std::uint32_t typeVersion {1};
    std::string displayName;
    std::vector<MaterialGraphPin> pins;
    std::vector<MaterialGraphProperty> properties;
};

struct MaterialGraphConnection {
    MaterialGraphConnectionId id;
    MaterialGraphPinId outputPinId;
    MaterialGraphPinId inputPinId;
};

struct MaterialGraphParameter {
    MaterialGraphParameterId id;
    std::string name;
    MaterialGraphValueType valueType;
    MaterialGraphValue defaultValue;
    MaterialGraphPinId targetPinId;
    MaterialGraphParameterUiMetadata ui;
};

struct MaterialGraphAsset {
    AssetId id;
    std::string name;
    std::uint32_t schemaVersion {1};
    MaterialDomain domain {MaterialDomain::Surface};
    MaterialGraphNodeId outputNodeId;
    std::vector<MaterialGraphNode> nodes;
    std::vector<MaterialGraphConnection> connections;
    std::vector<MaterialGraphParameter> parameters;
    MaterialGraphEditorState editorState;
    MaterialGraphPreviewSettings preview;
};
```

`MaterialGraphValue` should be a tagged variant of fixed, serializable engine types. A
texture value stores a `TextureAsset` ID and optional sampler/UV metadata; it never stores
a Qt image, Vulkan descriptor, filesystem pointer, or importer-library object.

### Stable Node Types

Node types use versioned string identifiers instead of a closed enum, for example:

- `projectunity.output.pbr_surface`
- `projectunity.input.float`
- `projectunity.input.color4`
- `projectunity.texture.sample2d`
- `projectunity.math.multiply`
- `projectunity.math.lerp`
- `materialx.standard_surface`

A registry describes display metadata, expected pins, compiler lowering, and migration.
Unknown node types remain loadable and round-trippable but make compilation fail with a
clear diagnostic. This prevents a missing optional backend from corrupting the asset.

### Editor And Preview State

Editor layout is serialized but excluded from the semantic shader hash:

- node position and collapsed state;
- comments/groups;
- canvas pan and zoom;
- selected preview mesh: sphere, cube, plane, or a `ModelAsset` reference;
- preview environment `TextureAsset` ID;
- preview camera orbit, exposure, and light preset.

The preview panel requests an asynchronous compilation and renders through a small Vulkan
target using the existing renderer infrastructure. Qt displays the resulting editor
surface; Qt/OpenGL must not become a second material renderer.

## Material Assignment

`MeshPrimitive::materialIndex` remains the only primitive-to-material relationship.

An individual `MaterialAsset` can later reference:

- an optional `MaterialGraphAsset` ID;
- a parameter override block;
- fixed fallback PBR values and textures for unsupported or not-yet-compiled graphs.

This preserves imported assets:

- GLB/glTF primitives retain their original material index.
- OBJ `mtllib` and `usemtl` partitions become distinct primitive/material assignments.
- Relative MTL texture paths resolve during import into material texture slots.
- FBX/DAE materials imported through Assimp enter the same material table.
- A user may convert or replace one material slot with a graph without changing unrelated
  primitives or applying one texture to the whole model.

Batching keys must include the compiled material identity and render state. If a graph
edit changes alpha, culling, shader features, texture bindings, or pipeline compatibility,
the affected cooked/render batches are invalidated and rebuilt while source primitive
provenance remains intact.

## Validation Rules

Validation runs after load and before compile:

1. Asset, node, pin, connection, and parameter IDs are unique and non-zero.
2. Exactly one valid output node exists for the graph domain.
3. Every connection goes from an output pin to an input pin.
4. Pin types match or use an explicitly registered conversion.
5. Non-array input pins have at most one incoming connection.
6. The v1 graph is acyclic; cycle diagnostics include the involved node IDs.
7. Texture references point to Texture2D assets.
8. Numeric values are finite and respect serialized limits.
9. Required node types and versions have registered implementations.
10. Resource and complexity limits reject malicious or accidental oversized graphs.

Suggested initial limits are 16,384 nodes, 65,536 pins, 65,536 connections, and bounded
string/property payloads. Limits belong in the reader as well as the validator.

## Serialization In FFULT

Add `AssetType::MaterialGraph` only when reader, writer, manager storage, and focused tests
land together. Do not partially advertise the asset type.

The first implementation can follow the existing FFULT binary reader/writer style:

1. FFULT header and asset ID.
2. Material graph schema version and domain.
3. Name and output node ID.
4. Nodes, pins, typed default values, and properties.
5. Connections.
6. Exposed parameters and UI metadata.
7. Editor layout and preview settings.

Serialization requirements:

- deterministic ordering by stable ID before writing;
- explicit enum widths and tagged value payloads;
- checked counts, string sizes, and integer conversions;
- graph-specific schema migrations independent from the global FFULT version;
- unknown optional properties can be preserved or skipped by length;
- no generated SPIR-V, MaterialX DOM, Qt JSON, or GPU handles in the authoritative graph.

Maintain two hashes:

- document hash: includes graph, editor layout, and preview state;
- semantic hash: includes only compilation-relevant graph and parameter defaults.

The compiled cache key includes the semantic hash, ProjectUnity material compiler
version, target backend, shader template version, enabled Vulkan features, and optional
MaterialX version.

## Compilation Strategy

### Tier 1: Existing PBR Compatibility

The first executable graph subset lowers to the current `MaterialAsset` PBR inputs:

- base color;
- metallic;
- roughness;
- normal;
- occlusion;
- emissive;
- alpha and cutoff;
- double-sided/cull state.

Constants, parameters, texture samples, UV transforms, channel extraction, multiply, add,
and lerp are enough for the first milestone. This tier must preserve the current Vulkan
material path and can reject graphs requiring generated shader code.

### Tier 2: Native Graph Compiler

Lower the editable graph to a renderer-neutral, topologically ordered IR. Apply constant
folding, dead-node elimination, type conversion, resource collection, and feature-key
generation. Produce cached shader source/SPIR-V, reflected bindings, parameter layout,
texture binding table, alpha/cull state, and diagnostics.

### Tier 3: Optional MaterialX Backend

MaterialX can be used for:

- import/export of supported graph subsets;
- standard node definitions and interoperability;
- offline texture baking experiments;
- optional GLSL/Vulkan GLSL shader generation.

ProjectUnity remains authoritative. The bridge maps `MaterialGraphAsset` to a MaterialX
document and maps supported MaterialX nodes back into ProjectUnity nodes. Unsupported
nodes are reported and preserved as opaque interchange metadata when practical.

MaterialX provides `MaterialXGenShader`, `MaterialXGenGlsl`, and a Vulkan GLSL
`VkShaderGenerator`, but generated resource bindings, lighting, shadows, alpha policy,
vertex inputs, and descriptor-set conventions must be adapted to ProjectUnity. Generated
code must not be injected directly into the current Vulkan renderer without this contract
and shader/pipeline cache coverage.

## Editor Adapter

The QtNodes adapter should derive from `QtNodes::AbstractGraphModel` and translate:

- ProjectUnity node IDs to QtNodes node IDs;
- ProjectUnity pin IDs to QtNodes port indices;
- ProjectUnity connection IDs to QtNodes connection records;
- graph validation results to node/pin error presentation.

All editing commands mutate `MaterialGraphAsset` through a document/controller API.
QtNodes signals update the view; QtNodes is not allowed to maintain a second authoritative
graph. ProjectUnity undo commands store stable IDs and before/after values.

Prefer a side Inspector for complex parameter widgets. Embedding many QWidget controls
inside thousands of graphics items can become expensive and must be benchmarked before it
becomes the default node presentation.

## Implementation Phases

### MG0: Data Contract

- Add the graph structs, value variant, validator, semantic hash, and unit tests.
- No UI, MaterialX, shader generation, or renderer changes.

### MG1: Internal Serialization

- Add `AssetType::MaterialGraph` and FFULT round-trip support.
- Test malformed counts, unknown node types, deterministic output, and migrations.

### MG2: QtNodes Spike

- Add an editor-only optional dependency pinned to the reviewed commit.
- Display and edit a `MaterialGraphAsset` through `AbstractGraphModel`.
- Verify undo/redo and stress behavior without using QtNodes serialization.
- Compare against a small native QGraphics proof if QtNodes ownership or performance is
  problematic.

### MG3: PBR Graph Preview

- Lower the supported graph subset to current PBR material inputs.
- Compile asynchronously and render a Vulkan preview sphere.
- Keep the last valid compiled preview when the edited graph has errors.

### MG4: Material Assignment

- Assign a graph per `MaterialAsset` slot with instance parameter overrides.
- Preserve `primitive.materialIndex` through import, FFULT, batching, and RenderWorld.
- Rebuild only affected material/pipeline batches.

### MG5: MaterialX Evaluation

- Build an optional offline bridge and compare graph round-trip fidelity.
- Evaluate `VkShaderGenerator` output against ProjectUnity descriptor and lighting rules.
- Keep the native compiler path and current PBR fallback available.

## Acceptance Criteria

- No ImGui source or transitive dependency exists in the node editor path.
- Core graph tests build in the non-editor `dev-core` preset without Qt.
- Saving and loading FFULT preserves nodes, pins, links, parameters, layout, and preview.
- Moving a node changes the document hash but not the semantic shader hash.
- Editing one imported material graph affects only primitives using that material index.
- Missing optional QtNodes or MaterialX support does not prevent loading graph assets.
- Vulkan receives only compiled material state and never depends on QtNodes or MaterialX
  object lifetimes.

## Primary References

- QtNodes: https://github.com/paceholder/nodeeditor
- QtNodes documentation: https://qtnodes.readthedocs.io/
- QGraphicsScene: https://doc.qt.io/qt-6/qgraphicsscene.html
- QGraphicsView: https://doc.qt.io/qt-6/qgraphicsview.html
- MaterialX: https://github.com/AcademySoftwareFoundation/MaterialX
- MaterialX shader generation: https://github.com/AcademySoftwareFoundation/MaterialX/blob/main/documents/DeveloperGuide/ShaderGeneration.md
- MaterialX Vulkan GLSL generator: https://materialx.org/docs/api/_vk_shader_generator_8h.html

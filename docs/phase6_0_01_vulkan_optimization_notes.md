# Phase 6.0.01 - Vulkan Optimization Research Notes

Status: PARCIAL / RESEARCH CHECKPOINT

This checkpoint records Vulkan renderer optimization research before continuing Phase 6.
It is not a completed renderer feature. It is a working notebook for ProjectUnity so
future renderer changes are tied to measured Vulkan problems instead of guesses.

Important: the "literal anchors" below preserve the exact names, API identifiers, limits,
and short labels from the sources. The longer guidance is intentionally paraphrased and
translated into ProjectUnity actions. Do not treat the paraphrase as a replacement for
the original articles.

## Sources Read

- Vulkan Learn: https://vulkan.org/learn
- Zeux / Arseny Kapoulkine, Writing an Efficient Vulkan Renderer:
  https://zeux.io/2020/02/27/writing-an-efficient-vulkan-renderer/
- AMD GPUOpen / Quantic Dream, Porting Detroit Become Human from PlayStation 4 to PC -
  Part 1: https://gpuopen.com/learn/porting-detroit-1/
- Khronos/NVIDIA, Vulkan Ray Tracing Best Practices for Hybrid Rendering:
  https://www.khronos.org/blog/vulkan-ray-tracing-best-practices-for-hybrid-rendering
- Vulkan Guide ray tracing overview:
  https://docs.vulkan.org/guide/latest/extensions/ray_tracing.html

## How To Use These Notes

- Keep the original source language close: preserve exact section names, Vulkan symbols,
  extension names, numeric targets, and warning labels when discussing a technique.
- Keep the ProjectUnity decision separate: only implement a technique after checking the
  current code path, the profiler counters, and the hardware assumptions.
- Treat 2020 case studies with care. The principles still matter, but driver behavior,
  Vulkan versions, and GPU architecture support can change.
- Do not claim Phase 6 is complete from research notes. These notes only prepare the next
  code changes.

## Non-Negotiable ProjectUnity Rules

- Optimize renderer data flow, not imported content.
- Do not remove triangles, faces, UVs, normals, tangents, textures, materials, alpha
  state, colors, node transforms, or animation data as an optimization shortcut.
- Runtime LOD must be reversible and screen-space driven; close/selected Scene View
  objects stay full fidelity.
- Qt remains editor UI. Vulkan remains the real 3D viewport path when available.
- Every optimization needs a metric, profiler row, test, smoke test, GPU timestamp, or
  explicit validation path.
- Mark status as PARCIAL until the implementation is compiled, integrated, tested, and
  has no known blocker.

## Zeux Article Reading Matrix

This section follows the article structure so the words used in future discussions match
the source instead of drifting into vague "best practice" language.

### Abstract

Literal anchors:

- `memory allocation`
- `descriptor set management`
- `command buffer recording`
- `pipeline barriers`
- `render passes`
- `CPU and GPU performance`
- `profile the target application`

Caution:

- The article gives general guidance across desktop and mobile. It repeatedly points back
  to profiling the real application on the real target platform.

ProjectUnity action:

- Do not introduce heavy Phase 6 renderer changes from theory alone. Add counters first:
  upload bytes/time, descriptor updates, command record CPU, pass GPU timestamps, pipeline
  cache events, culled batches, and draw/instance counts.

### Memory management

Literal anchors:

- `VulkanMemoryAllocator`
- `VMA`
- `Memory heap selection`
- `VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT`
- `VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT`
- `VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT`
- `VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT`
- `Memory suballocation`
- `bufferImageGranularity`
- `Dedicated allocations`
- `VkMemoryDedicatedRequirementsKHR`
- `requiresDedicatedAllocation`
- `prefersDedicatedAllocation`
- `Mapping memory`
- `persistent mapping`

Caution:

- Memory type behavior is not uniform across vendors. Integrated GPUs, discrete GPUs,
  tiled GPUs, AMD host-visible device-local memory, and PCIe reads all change the best
  choice.
- Host-visible coherent memory is convenient, but random shader access from host memory
  can cost badly on discrete GPUs.
- Per-resource `vkAllocateMemory` style allocation does not scale; suballocation and
  correct alignment are part of correctness, not just optimization.

ProjectUnity action:

- Keep VMA as the default allocator layer.
- Put static meshes, static textures, GPU-only buffers, render targets, shadow maps, and
  compressed texture payloads in device-local memory when possible.
- Use host-visible memory for staging and per-frame ring/uniform data, not as a permanent
  home for large randomly accessed scene resources.
- Prefer persistent mapping for upload/ring buffers after confirming memory type behavior.
- Add upload budgeting and telemetry before more Phase 6 asset import work: bytes queued,
  bytes submitted, bytes completed, fence wait time, and first-visible-frame upload time.
- Consider dedicated allocation only for large bandwidth-heavy render targets after a
  profiler shows benefit.

### Descriptor sets

Literal anchors:

- `Descriptor sets`
- `Mental model`
- `Dynamic descriptor set management`
- `vkResetDescriptorPool`
- `VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT`
- `vkAllocateDescriptorSets`
- `Choosing appropriate descriptor types`
- `uniform buffers`
- `storage buffers`
- `dynamic offsets`
- `combined image/sampler descriptor`
- `immutable sampler descriptor`
- `Slot-based binding`
- `vkUpdateDescriptorSets`
- `vkUpdateDescriptorSetWithTemplate`
- `dynamic uniform buffers`
- `Frequency-based descriptor sets`
- `set=0`
- `set=1`
- `set=2`
- `Bindless descriptor designs`
- `VK_EXT_descriptor_indexing`
- `descriptor array`
- `maxPerStageDescriptorSampledImages`

Caution:

- Descriptor sets should be treated like GPU-visible memory with frame-safe lifetimes.
- Freeing individual descriptor sets in hot paths can make driver-side management worse.
- Worst-case pool sizing wastes memory when shader descriptor counts differ by pass.
- Bindless/descriptor indexing is not universally available at useful limits; it needs a
  fallback path.

ProjectUnity action:

- Preserve the current descriptor cache, but add metrics: allocations/frame, updates/frame,
  descriptor write count, cache hits/misses, pool resets, and per-pass descriptor pressure.
- Move toward frequency-based grouping:
  - set 0: frame/view/global data, environment, shadow maps.
  - set 1: material or material index state.
  - set 2: per-draw/per-instance transform and draw constants.
- For shadow/depth passes, avoid full PBR material descriptors for opaque casters. Masked
  casters still need alpha cutoff data.
- Plan a feature-gated descriptor indexing path where visible textures/material buffers are
  arrays and shaders use indices.
- Do not rebuild a massive descriptor array every frame. Track visible resource deltas and
  keep updated descriptor sets immutable for enough frames to avoid GPU read hazards.

### Command buffer recording and submission

Literal anchors:

- `Command buffer recording and submission`
- `Command pool`
- `Multi-threaded command recording`
- `F*T pools`
- `vkResetCommandPool`
- `Command buffer submission`
- `<10 submits per frame`
- `<100 command buffers per frame`
- `VkSubmitInfo`
- `Secondary command buffers`
- `VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT`
- `VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS`
- `Command buffer reuse`
- `VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT`
- `VK_KHR_multiview`

Caution:

- More threads are not automatically faster. Small passes can lose performance if split
  into tiny command buffers.
- The unit that matters for scheduling is `VkSubmitInfo`, not just the raw count of
  `vkQueueSubmit` calls.
- Tiled GPUs care strongly about secondary command buffers inside a render pass because
  splitting primary render passes can force tile flush/load behavior.

ProjectUnity action:

- Keep current direct recording correct, but keep per-pass CPU timings visible.
- Avoid over-parallelizing small editor/shadow passes until metrics prove command recording
  is a bottleneck.
- When multithreaded recording is introduced, use command pools per frame and per recording
  thread, then consider pass/size classes for memory stability.
- Target coarse command buffers and few submits before pursuing many tiny jobs.
- Use one-time submit behavior for dynamic frame command buffers unless a rare reuse case is
  deliberately designed.

### Pipeline barriers

Literal anchors:

- `Pipeline barriers`
- `execution dependencies`
- `memory visibility`
- `layout changes`
- `Stalling execution`
- `Flushing or invalidating`
- `decompress`
- `vkCmdPipelineBarrier`
- `VK_PIPELINE_STAGE_ALL_COMMANDS_BIT`
- `VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT`
- `split barrier`
- `vkCmdSetEvent`
- `vkCmdWaitEvents`
- `simple_vulkan_synchronization`
- `render graphs`

Caution:

- A missing barrier can be a correctness bug that only appears on untested hardware.
- An unnecessary barrier can kill overlap, force idle gaps, or trigger expensive layout/
  compression work.
- Validation tools are not enough; missing barriers are not always caught.

ProjectUnity action:

- Add clear pass/resource state ownership before advanced postprocess, shadows, IBL, or ray
  tracing makes dependencies harder.
- Batch barriers where possible and keep stage masks specific. Avoid defaulting to broad
  all-stage barriers.
- Do not transition depth/render targets to read layouts unless a later pass actually reads
  them.
- Treat a render graph as a Phase 6+ architecture direction: declare pass reads/writes
  before recording so barriers, transient resources, and layout transitions are generated
  from known dependencies.

### Render passes

Literal anchors:

- `Render passes`
- `Load & store operations`
- `VK_ATTACHMENT_LOAD_OP_CLEAR`
- `VK_ATTACHMENT_LOAD_OP_LOAD`
- `VK_ATTACHMENT_LOAD_OP_DONT_CARE`
- `VK_ATTACHMENT_STORE_OP_DONT_CARE`
- `Fast MSAA resolve`
- `vkCmdResolveImage`
- `pResolveAttachments`
- `pInputAttachments`
- `VK_DEPENDENCY_BY_REGION_BIT`

Caution:

- Load/store flags are not decoration. They affect bandwidth, tiler behavior, compression
  metadata, and render target lifetime.
- Fixed-function MSAA resolve through render pass resolve attachments can be much better
  than storing MSAA then resolving afterward.

ProjectUnity action:

- Audit current attachment load/store choices when adding more viewport passes.
- Use DONT_CARE when the previous or final attachment contents are not needed.
- Track which depth/MSAA/color targets are actually consumed by later passes before adding
  stores or shader-readable transitions.

### Pipeline objects

Literal anchors:

- `Pipeline objects`
- `Just-In-Time compilation`
- `two-level cache`
- `Pipeline cache and cache pre-warming`
- `VkPipelineCache`
- `vkCreateGraphicsPipelines`
- `vkGetPipelineCacheData`
- `VkPipelineCacheCreateInfo`
- `Ahead of time compilation`
- `technique`
- `effects`
- `Fossilize`

Caution:

- First-use pipeline creation causes interactive frame stutter.
- JIT caches can still have lock contention and compilation spikes.
- AOT/technique systems reduce stutter but require renderer/material design discipline.
- Pipeline permutations can explode from shaders, render target formats, depth/stencil,
  culling, blending, vertex layout, alpha, skinning, terrain, and quality settings.

ProjectUnity action:

- Add persistent pipeline cache load/save before PBR, terrain, shadows, postprocess, and
  alpha variants multiply.
- Add metrics for pipeline cache hits/misses, first-use creation, creation time, and warmup
  time.
- Avoid letting every imported material create unique pipeline state by accident.
- Move long term toward material/effect/technique descriptions that fully specify graphics
  state and are known at load time.

### Conclusion and tools list

Literal anchors:

- `VulkanMemoryAllocator`
- `volk`
- `simple_vulkan_synchronization`
- `Fossilize`
- `perfdoc`
- `niagara`
- `Vulkan-Samples`
- `Radeon Graphics Profiler`
- `NVidia Nsight Graphics`

Caution:

- The article explicitly warns that the same feature can be a fast path on one vendor and
  a slow path on another.

ProjectUnity action:

- Use RenderDoc/Nsight/RGP style tooling when GPU behavior matters; keep in-engine profiler
  rows for quick editor iteration.
- Treat libraries as candidate tools, not automatic dependencies. Prefer the smallest
  integration that solves a measured problem.

## Detroit Become Human Notes

Literal anchors:

- `VkPipeline`
- `VkPipelineCache`
- `SPIR-V`
- `SPIR-V optimizer`
- `99,500 VkPipelines`
- `VK_EXT_descriptor_indexing`
- `more than 4,000 textures in a frame`
- `triple buffering`
- `vkCmdDrawIndexedIndirect`
- `gl_InstanceID`
- `60%`

Caution:

- Detroit had a production renderer with extreme material/shader variety. Do not copy the
  exact scale of the solution before ProjectUnity has comparable metrics.

ProjectUnity action:

- Avoid first-use pipeline stutter by building pipeline cache persistence early.
- Sort similar pipeline creation jobs if/when parallel warmup exists.
- Use material instancing and permutation control before material variety becomes huge.
- For large scenes, descriptor indexing plus visible-resource delta updates is the direction
  to investigate, with fallback for unsupported devices.
- Indirect primitive batching is attractive for depth/shadow passes after draw data and
  descriptor layout support it.

## Vulkan Ray Tracing / Hybrid Rendering Notes

This section is from "Vulkan Ray Tracing Best Practices for Hybrid Rendering" and the
attached diagrams. Do not collapse it into generic "future ray tracing" notes; the diagrams
show the data flow that the renderer architecture must eventually support.

### Overview anchors

Literal anchors:

- `Vulkan Ray Tracing extensions`
- `hybrid rendering`
- `rasterization and ray tracing`
- `Wolfenstein: Youngblood`
- `ray traced reflections`
- `VK_KHR_acceleration_structure`
- `VK_KHR_ray_tracing_pipeline`
- `VK_KHR_ray_query`
- `VK_KHR_pipeline_library`
- `VK_KHR_deferred_host_operations`

Meaning for ProjectUnity:

- Hybrid rendering is not "replace rasterization with ray tracing." Raster passes still
  produce depth, normals, roughness, motion, lighting, and environment data that ray tracing
  consumes.
- The final 5 extension names must stay explicit in future feature checks.

### Diagram literal labels

Visible text from the acceleration structure / SBT diagrams:

- `Shader Binding Table`
- `Shader info`
- `Ray tracing shaders`
- `Acceleration Structure Traversal`
- `Top Level Acceleration Structure`
- `Instance []`
- `BLAS`
- `Vertex Buffer`
- `Index Buffer`
- `AABB`
- `Instance Buffer`

Visible text from the implicit shader execution mechanism diagrams:

- `Implicit Shader Execution Mechanism`
- `Ray Generation Shader`
- `Acceleration Structure Traversal`
- `Any Hit Shader`
- `Intersection Shader`
- `Hit?`
- `Closest Hit Shader`
- `Miss Shader`
- `TLAS`
- `BLAS`
- `Shader Binding Table`

What the images show:

- `Shader Binding Table` feeds `Ray tracing shaders`.
- `Ray tracing shaders` start `Acceleration Structure Traversal`.
- Traversal reaches `Top Level Acceleration Structure`; TLAS contains `Instance []` entries.
- TLAS instances point to `BLAS`; BLAS points at `Vertex Buffer`, `Index Buffer`, and `AABB`.
- The `Instance Buffer` feeds instance data back into TLAS.
- The shader execution diagram is not a simple draw-call path: `Ray Generation Shader`
  invokes traversal, traversal can run `Any Hit Shader` / `Intersection Shader`, then the
  `Hit?` branch resolves to `Closest Hit Shader` or `Miss Shader`.

ProjectUnity action:

- Preserve stable renderer-side IDs for geometry, material, texture, and instance data now.
  The diagrams assume those relationships are explicit and indexable.
- Keep a future path where geometry buffers, material buffers, texture arrays, and instance
  transforms can be globally referenced instead of rebound per draw.

### Acceleration structures

Literal anchors:

- `Acceleration structures`
- `AS`
- `Bounding Volume Hierarchy`
- `BVH`
- `top level`
- `bottom level`
- `TLAS`
- `BLAS`
- `AABB`
- `transformation matrix`
- `unique index per instance`
- `vkCmdBuildAccelerationStructuresKHR()`
- `VkAccelerationStructureBuildGeometryInfoKHR`
- `VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR`
- `srcAccelerationStructure`
- `dstAccelerationStructure`
- `full acceleration structure rebuild`

Source meaning:

- TLAS stores instances. BLAS stores the actual geometry references: vertex data, index data,
  and bounds.
- A TLAS instance can carry the mapping needed to connect a hit to material/shading data.
- AS update is constrained. The article calls out that changing active primitives to
  inactive, or inactive to active, is not an update path; that requires rebuild.

ProjectUnity action:

- Do not design future scene visibility as "put the entire scene into TLAS." Use culling
  and importance rules before TLAS population.
- If/when acceleration structures arrive, track whether a mesh needs update or rebuild.
  Dynamic/skinned data cannot be treated like static geometry.
- Keep original asset buffers immutable where possible; use derived GPU buffers for dynamic
  transformed positions if a future AS update path needs them.

### Ray tracing descriptor set and pipeline

Literal anchors:

- `Ray tracing descriptor set and the ray tracing pipeline`
- `bundle all the required resources up-front`
- `single set of descriptors`
- `two descriptor sets`
- `scene information`
- `acceleration structures and indices for the SBT`
- `camera transform`
- `materials`
- `texture data`
- `vertex/index`
- `payload`
- `rayPayloadEXT`
- `traceRayEXT()`
- `callable shaders`
- `SPIR-V`
- `push constants`
- `VkPhysicalDeviceLimits.maxPushConstantSize`
- `VkRayTracingShaderGroupCreateInfoKHR`
- `VkRayTracingPipelineCreateInfoKHR`
- `VK_SHADER_UNUSED_KHR`

Source meaning:

- Ray tracing reverses the normal material binding mindset: a ray can hit any material, so
  resources need to be available up front.
- Hybrid applications commonly carry one descriptor set for scene/raster information and
  another for AS/SBT-related indices.
- Payload is the persistent state carried through ray traversal.

ProjectUnity action:

- Descriptor indexing/bindless is not only a draw-call optimization; it is also a ray
  tracing architecture prerequisite.
- Material and texture data should move toward indexed GPU buffers/arrays before ray
  tracing exists.
- Keep push constants small and intentional; do not assume transforms always belong there.

### Five shader stages

Literal anchors:

- `Ray generation shader`
- `Closest hit shader`
- `Miss shader`
- `Intersection shader`
- `Any-hit shader`

Source meaning:

- `Ray generation shader`: starts ray tracing through `traceRayEXT()`.
- `Closest hit shader`: shades the closest accepted intersection.
- `Miss shader`: handles no-hit cases, commonly environment sampling.
- `Intersection shader`: custom intersection logic; built-in intersection is ray-triangle.
- `Any-hit shader`: filters intersections and is often used for alpha-testing.

ProjectUnity action:

- Alpha-tested materials need explicit policy before ray tracing. `Any-hit shader` is the
  natural fit but can be expensive.
- Environment/IBL data should be accessible to `Miss shader` style fallbacks in a future
  hybrid renderer.

### Shader Binding Table

Literal anchors:

- `The Shader Binding Table (SBT)`
- `array of unique handles`
- `ray generation shader`
- `miss shader`
- `hit group`
- `hitGroupID`
- `address, stride, and size`
- `vkCmdTraceRaysKHR()`
- `ray generation shader is always at index 0`

Source meaning:

- SBT maps ray traversal results to shader groups. It is the bridge between hit geometry
  and the shader/material logic.
- The article describes associating instances and shader groups through a `hitGroupID`.

ProjectUnity action:

- Store material/surface IDs with renderer instances now, not only in editor-side objects.
- Keep batch/instance data explicit enough that a future SBT can map a hit to surface data
  without reverse-engineering draw state.

### Ray Lifetime - The Bigger Picture

Literal anchors:

- `Ray Lifetime - The Bigger Picture`
- `BVH instances`
- `Traversal flags`
- `culling operations`
- `transparency`
- `watertight`
- `rayPayloadEXT`

Source meaning:

- Ray traversal can cull/filter candidates before final hit confirmation.
- The hit result returns through the payload to the ray generation shader.

ProjectUnity action:

- Keep alpha/transparent material metadata precise. Ray tracing visibility is not the same
  as raster visibility.
- Avoid losing material flags during import/runtime conversion because future traversal
  flags and any-hit behavior depend on them.

### Building the Acceleration Structure

Literal anchors:

- `VK_FORMAT_R16G16B16_SNORM`
- `VK_FORMAT_R32G32B32_SFLOAT`
- `VK_INDEX_TYPE_UINT16`
- `update`
- `full rebuild`
- `skinned/animated objects`
- `particles`
- `group multiple geometries per BLAS`
- `gl_GeometryIndexEXT`
- `compute pipeline`
- `world-space position vector`
- `extended camera frustum test`
- `instance size`
- `angular size`
- `AABB (in camera space)`

Source meaning:

- Static geometry, hair/skin, indices, particles, and skinned geometry can use different
  representations depending on precision and update needs.
- Grouping multiple geometries per BLAS can reduce BLAS count.
- Wolfenstein used TLAS instance culling based on camera-space AABB angular size.

ProjectUnity action:

- Do not throw all primitives into independent future BLAS objects by default. Grouping can
  matter, but must preserve material and culling needs.
- Keep per-primitive and per-instance bounds accurate now; future TLAS culling depends on
  them.
- Runtime/skinned derived buffers should be designed as generated GPU data, not destructive
  edits to imported buffers.

### Ray Traced Reflections

Literal anchors:

- `Ray Traced Reflections`
- `screen space reflections`
- `off-screen pixels`
- `particle systems lacking in depth data`
- `opaque reflections`
- `depth data`
- `normal maps`
- `surface roughness map`
- `max ray bounds Tmax`
- `denoising`
- `reflected radiance`
- `hit distance`
- `deferred composite lighting pass`
- `transparent surfaces`
- `camera-aligned quads`
- `alpha-masked`
- `opaque and transparent`
- `static`
- `glass`
- `particles`
- `skin`
- `hair`
- `engine initialization stage`
- `VK_KHR_buffer_device_address`
- `VB/IB reference storage`
- `PhysicalStorageBuffer`
- `bindless texture support`
- `VK_EXT_descriptor_indexing`
- `Dynamic (non-uniform) indexing`
- `off-screen lighting`
- `spherical harmonics lighting`
- `view-dependent lighting artifacts`

Source meaning:

- The ray-traced reflection path uses raster depth/normal/roughness as input.
- Rough surfaces can use reduced `Tmax` to improve traversal performance because the result
  is later sampled/denoised.
- Transparent reflections need a different raster/ray/raster composition strategy.
- Wolfenstein created the ray tracing pipeline at initialization to avoid shader stutter.
- Buffer device address and descriptor indexing support the hit shaders reading VB/IB and
  texture data without frequent binding changes.

ProjectUnity action:

- Future reflections should be designed as hybrid passes: raster G-buffer-ish data first,
  ray pass second, composite pass after.
- Descriptor indexing and buffer device address should be evaluated together for future RT
  and bindless material access.
- Off-screen lighting needs fallback data; do not design clustered/lightmap systems that
  only work for currently visible pixels if ray effects are planned.

### Concluding remarks

Literal anchors:

- `optimal acceleration structure management`
- `60% BLAS count saving`
- `resource management`
- `processing budget allocation`

Source meaning:

- The article highlights acceleration structure management as a major performance lever.
- Grouping multiple geometries in one BLAS gave Wolfenstein a reported `60% BLAS count
  saving`.

ProjectUnity action:

- Treat AS/TLAS/BLAS design as a resource-budget problem, not only a feature checklist.
- The immediate Phase 6 benefit is architectural: stable IDs, bounds, descriptors,
  material indexing, GPU timestamps, and pass metrics.

## ProjectUnity Immediate Follow-Up

Done before heavier Phase 6 visual work:

- Add GPU timestamp queries for the viewport frame, shadow pass, mesh pass, and editor
  color-aid pass.
- Preserve HDR environment radiance through import, cubemap generation, and Vulkan float
  upload when the selected device supports the chosen sampled format.
- Prefer a compressed GPU payload for KTX2 Basis/UASTC where available, while keeping a
  fallback payload for devices that cannot sample the compressed candidate.
- Add point-light visible-bounds 2D shadow-map fallback in the current single-map path.

Still do before heavier Phase 6 visual work:

- Add upload queue/resource streaming metrics.
- Add descriptor cache/pool metrics.
- Add pipeline cache persistence and warmup metrics.
- Keep per-pass culling metrics, including shadows.
- Add cascaded directional shadows and full point-light cubemap policy before claiming
  shadow coverage complete.

Do before descriptor indexing:

- Define stable GPU material, draw, transform, and texture index data.
- Decide feature detection and fallback behavior.
- Measure descriptor update cost in current scenes.
- Preserve transparent sorting and masked alpha behavior.

Do before multi-draw indirect:

- Keep direct draw fallback correct.
- Ensure batch keys include pipeline, index buffer, material/texture state, alpha mode,
  LOD, cull mode, and shadow/depth policy.
- Add draw-data buffers and instance/material indexing.

Do before ray tracing:

- Add spatial partitioning for large scenes.
- Add explicit resource lifetime tracking for GPU-visible buffers/images.
- Add stable geometry/material/instance IDs.
- Add GPU timestamp infrastructure and debug labels per high-level pass.

## Current Phase 6.0.01 Status

- What works: research is recorded with source-aligned anchors; the notes are linked from
  Phase 6 and renderer status docs; GPU timestamps are integrated; HDR environment
  radiance and KTX2 Basis/UASTC compressed-candidate fallback paths are integrated.
- What does not work yet: these notes do not implement upload budgeting, descriptor
  indexing, indirect drawing, pipeline cache persistence, render graph, cascaded shadows,
  full point-light cubemap shadows, or ray tracing.
- Known implementation status: PARCIAL.
- Next code step: add upload/resource queue metrics, descriptor metrics, and cascaded/
  cubemap shadow architecture before claiming Phase 6 complete.

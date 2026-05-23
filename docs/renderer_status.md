# Renderer Status

Date: 2026-05-22

## Current State

Status: PARCIAL

The editor has a real renderer module boundary and initializes Vulkan through `IRenderer`.
The Vulkan path currently creates the instance, selects a GPU, creates a logical device,
checks validation/debug marker support, and creates a VMA allocator.
It also prepares Win32 viewport presentation resources by creating a `VkSurfaceKHR`,
validating present support, creating a swapchain, and creating swapchain image views.
The viewport target now owns command pool/buffer, acquire/present semaphores, an in-flight
fence, depth-backed render-pass resources, and a first presented mesh frame path.
The imported mesh path now builds Vulkan draw items from `Scene` and `ModelAsset` data,
uploads vertex/index buffers through VMA staging buffers, caches them by model primitive,
uploads material RGBA textures once, generates GPU mip chains when the format supports
linear blits, preserves glTF base-color/metallic/roughness/emissive factors, binds
base-color/normal/metallic-roughness/occlusion/emissive texture descriptors, preserves glTF
occlusion strength, `COLOR_0` vertex colors, and alpha mode/cutoff state, orders
`BLEND` primitive draws back-to-front after opaque/masked draws, and draws through
a textured mesh pipeline with opaque and transparent depth-write behavior.
Base-color and emissive texture uploads use sRGB cache entries while normal,
metallic-roughness, and occlusion uploads keep linear cache entries.
The Vulkan texture cache also keeps imported glTF wrap/filter sampler state in the
cache key and maps it onto the `VkSampler` used by each descriptor.
The textured mesh shader now reads a frame uniform buffer with view projection,
camera position, imported punctual lights, ambient environment terms, and the first
directional shadow transform instead of using a fixed shader-local light direction.
Direct lighting uses a Cook-Torrance-style metallic/roughness path and the viewport
records a VMA-backed 2048-square directional shadow map before the main pass.
Imported glTF light nodes are instantiated from model data, and Game View can render
through the first imported perspective camera while Scene View stays on the editor camera.
Imported primitives now store cached bounds, and the editor viewport performs camera
sphere culling before submitting Vulkan mesh draws so offscreen primitives do not enter
the draw list.
Selected Scene View gizmos, a controlled grid, axes, and hierarchy links now have a
renderer-owned Vulkan color mesh path for imported mesh frames, so those editor aids are
not hidden by the swapchain when the GPU path presents.

Scene View uses the CPU/QPainter mesh fallback only when Vulkan surface/frame rendering
does not succeed. Qt still draws empty-scene fallback content and text labels; it is not
the final 3D renderer. Until renderer-owned label/text overlay passes exist, an empty
Scene View does not present a Vulkan clear-only frame over the Qt Scene View aids.

## Degradation Reverted

- Removed fixed default preview triangle budgets.
- Removed automatic low-LOD Scene View selection.
- Removed texture sampling shortcuts that replaced material texture display by default.
- The default fallback path now draws the imported primitive index data rather than
  destructive preview indices.
- glTF scene/node TRS and matrix transforms are applied during import so multi-object
  exports keep their authored layout instead of collapsing raw meshes at the origin.

## Vulkan Work Remaining

- Replace the procedural ambient environment term with prefiltered IBL resources.
- Add editor/scene-owned lights and camera components beyond imported glTF model data.
- Expand shadows beyond the first directional map with masked-alpha caster handling,
  point/spot shadows, filtering controls, and stable shadow-frustum policy.
- Add anisotropic filtering and KTX2/Basis-ready compressed texture upload paths.
- Move labels/text overlays off QPainter and into renderer-owned passes.
- Add broader resource lifetime/cache policy around descriptors, materials, and
  renderer-owned passes.
- Add RenderDoc debug markers around frame, pass, and draw scopes.

## Bugs Fixed

- A failed viewport swapchain creation could leak a `VkSurfaceKHR` because the
  constructor threw after creating the raw surface. The constructor now destroys
  partially-created Vulkan resources before rethrowing.
- `MainWindow` now disconnects viewports from the renderer before destroying the
  renderer, so Qt child widget destruction cannot call back through a dangling
  renderer pointer.
- The first command-buffer path avoids resetting the in-flight fence before image
  acquisition succeeds, preventing an acquire failure from leaving the next frame
  blocked on an unsignaled fence.
- The viewport bridge no longer presents an empty Vulkan clear over Scene View when
  there are no imported mesh draws; that had hidden the Qt grid/overlay fallback.
- Imported glTF nodes are now traversed from the default scene and their world transforms
  are baked into imported primitive vertices, preventing visible layout loss on GLB/glTF
  files exported as several transformed mesh nodes.
- `MainWindow` smoke coverage was split into `MainWindowSmoke.cpp`, keeping the editor app
  source under the 800-line project rule without dropping tests.
- Scene View no longer asks QPainter to redraw world-space editor aids after a Vulkan
  mesh frame is presented. Those aids are now part of the Vulkan color mesh submission,
  which avoids the visible one-frame flash when switching back to Scene View.
- The first Vulkan color guide pass incorrectly submitted the whole debug frame,
  including camera frustum geometry, which produced a huge translucent Scene View shape.
  The GPU Scene View aid pass now submits a bounded grid, axes, hierarchy links, and
  gizmo geometry only.
- The first combined Scene View color buffer reused tinygizmo indices without adding the
  existing grid vertex offset. Rotate/move/scale triangles could point into grid vertices,
  creating huge malformed translucent handles. Gizmo indices are now rebased before upload.
- Material textures now generate mip levels during Vulkan upload when linear blitting
  is supported by their chosen RGBA8 linear or sRGB format; unsupported devices fall
  back to base mip upload without corrupting the texture.
- Mesh primitive bounds are computed at import time, updated after glTF node transforms,
  and used by Scene/Game viewport submission for conservative camera culling.
- glTF material `metallicFactor`, `roughnessFactor`, and `emissiveFactor` are now
  imported, tested, pushed to Vulkan, and used by the preview shader. This was the
  first factor pass and is now extended by normal and metallic-roughness texture
  sampling, but it is not the final full glTF material model.
- glTF `COLOR_0` vertex colors are now imported, tested, kept through meshoptimizer
  vertex reordering, uploaded as part of the Vulkan vertex buffer, and multiplied
  with base-color texture/material output in the textured mesh shader.
- glTF normal maps and metallic-roughness textures are now imported into
  `MaterialAsset`, submitted through renderer draw items, cached/uploaded as GPU
  texture descriptors, and sampled by the Vulkan mesh shader. Materials without a
  normal map use a cached flat-normal texture, so the fallback does not flatten or
  replace their base-color content.
- glTF occlusion textures and strength are now preserved in `MaterialAsset`, carried
  into the Vulkan material descriptor set, and used to attenuate the current ambient
  material term without dropping the imported base-color or metallic-roughness maps.
- glTF emissive textures are now preserved in `MaterialAsset`, passed through the
  Vulkan material descriptor set with a white fallback, and multiplied by the
  imported emissive factor in the current material shader.
- Vulkan material texture caching now keeps sRGB uploads for glTF base-color and
  emissive color inputs separate from linear normal, metallic-roughness, and
  occlusion uploads, even if a source glTF reuses the same image in both roles.
- glTF texture sampler wrap and filter state now survives model import. Texture
  uploads with no mipmap minification request skip extra mip generation, and Vulkan
  cache entries keep sampler variants separate when a source image is reused.
- glTF alpha mode and cutoff now survive import. The Vulkan mesh shader writes
  opaque alpha for `OPAQUE`, discards `MASK` fragments at the imported cutoff, and
  forwards `BLEND` alpha to the transparent mesh path.
- Vulkan draw ordering now keeps opaque and masked primitive draws in submission
  order, defers glTF `BLEND` primitives, sorts them back-to-front from camera depth,
  and uses a transparent mesh pipeline that keeps depth tests while disabling depth
  writes for those blended draws.
- glTF light and camera nodes were previously discarded after TinyGLTF parsed them.
  `ModelAsset` now preserves `KHR_lights_punctual` nodes and camera node transforms,
  the asset fixture tests those records, imported punctual lights enter the frame
  light list, and Game View can select an imported perspective camera.
- Mesh shading previously used a shader-local light vector and draw-level MVP only.
  Render frames now carry camera, light, environment, model, and shadow state to
  Vulkan through a frame UBO and model push constant, enabling world-space PBR
  direct lighting plus the first directional shadow pass.
- The vertex-color work pushed `AssetManager.cpp` over the 800-line code rule during
  development. Attribute/accessor decoding now lives in `GltfAttributeReader`, and
  the source-rule test passes again.

## Verification Notes

- The normal CTest editor smoke uses `QT_QPA_PLATFORM=offscreen`; that path validates
  editor/import/fallback behavior but cannot present a Win32 Vulkan surface.
- The Windows visible `--smoke-test` requires at least one imported textured mesh draw
  and one selected editor color mesh draw to reach the Vulkan viewport path and exits
  with an error if either is missing.

## Rules

- Do not modify imported source geometry destructively for viewport performance.
- Do not remove textures or replace materials as a performance shortcut.
- Optional LOD data may be generated at import time, but it must be selected deliberately
  and never become the default Scene View fidelity loss.
- Qt is editor UI only. It must not remain the primary 3D renderer.

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
uploads base-color RGBA textures once, generates GPU mip chains when the format supports
linear blits, preserves glTF base-color/metallic/roughness/emissive factors, binds
texture descriptors, and draws through a textured mesh pipeline with a depth target.
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

- Expand materials beyond current factor-only PBR approximation into normal maps,
  metallic-roughness textures, occlusion, alpha modes, IBL, and real scene lights.
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
- Base-color textures now generate mip levels during Vulkan upload when linear blitting
  is supported by `VK_FORMAT_R8G8B8A8_UNORM`; unsupported devices fall back to base mip
  upload without corrupting the texture.
- Mesh primitive bounds are computed at import time, updated after glTF node transforms,
  and used by Scene/Game viewport submission for conservative camera culling.
- glTF material `metallicFactor`, `roughnessFactor`, and `emissiveFactor` are now
  imported, tested, pushed to Vulkan, and used by the preview shader. This is a
  factor-only PBR approximation, not the final full glTF material model.

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

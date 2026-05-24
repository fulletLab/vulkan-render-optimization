# Phase 6 Visual Verification Assets

Status: ACTIVE. These assets are test inputs for Phase 6 renderer fidelity and
profiling. The visible `projectunity_editor --phase6-visual-smoke` command imports
the full list without editing or simplifying the sources.

## Local Pack

Downloaded assets live in:

`Project/Assets/VisualVerification`

They are intentionally not edited or simplified. Use the editor import flow on
these source files so renderer behavior is measured against unmodified GLB data.

## Sources

Primary source:

- Khronos glTF Sample Assets: https://github.com/KhronosGroup/glTF-Sample-Assets
- Browser/reference viewer: https://github.khronos.org/glTF-Assets/
- Complete model list and license summaries:
  https://github.com/KhronosGroup/glTF-Sample-Assets/blob/main/Models/Models.md

Khronos notes that model pages include license summaries and that each model
directory README should be checked for detailed license information.

## Selected Assets

| Asset | Purpose | License note |
| --- | --- | --- |
| `Avocado.glb` | Baseline textured PBR object, normal/material texture sanity. | CC0, Microsoft/Public summary. |
| `BoomBox.glb` | Multi-texture PBR object with emissive material coverage. | CC0, Microsoft/Public summary. |
| `Lantern.glb` | Wooden/metal material mix for PBR and texture fidelity. | CC0 summary with Microsoft/sbtron/Frank Galligan credits. |
| `MetalRoughSpheres.glb` | Grid for metallic/roughness material response. | CC BY 4.0 summary from Analytical Graphics. |
| `AlphaBlendModeTest.glb` | OPAQUE/MASK/BLEND alpha behavior. | Check per-model README before redistribution. |
| `TextureCoordinateTest.glb` | UV orientation and coordinate debugging. | CC0 summary from Analytical Graphics. |
| `BoxTexturedNonPowerOfTwo.glb` | Non-power-of-two texture upload/sampler path. | Check per-model README before redistribution. |
| `NormalTangentTest.glb` | Tangent and normal-map correctness. | Check per-model README before redistribution. |
| `NormalTangentMirrorTest.glb` | Mirrored tangent-space and handedness correctness. | Check per-model README before redistribution. |
| `CompareNormal.glb` | Normal-map method comparison. | CC0 content summary with Khronos mark note. |
| `DirectionalLight.glb` | Imported directional light path. | Check per-model README before redistribution. |
| `LightsPunctualLamp.glb` | Imported point/spot/punctual light path. | CC BY 4.0 summary from DGG. |
| `OrientationTest.glb` | Coordinate system and axis orientation regression tests. | Check per-model README before redistribution. |
| `NegativeScaleTest.glb` | Negative determinant, winding, normals, and culling regression tests. | Check per-model README before redistribution. |
| `NodePerformanceTest.glb` | Large-node scene for import/render profiling and cache counters. | Check per-model README before redistribution. |

## Import Order

1. `OrientationTest.glb`
2. `NegativeScaleTest.glb`
3. `TextureCoordinateTest.glb`
4. `NormalTangentTest.glb`
5. `NormalTangentMirrorTest.glb`
6. `Avocado.glb`
7. `BoomBox.glb`
8. `Lantern.glb`
9. `MetalRoughSpheres.glb`
10. `AlphaBlendModeTest.glb`
11. `DirectionalLight.glb`
12. `LightsPunctualLamp.glb`
13. `NodePerformanceTest.glb`

## What To Compare

- Scene orientation must match the Khronos reference viewer. No mirrored X/Z.
- UV labels and texture directions must match the reference viewer.
- Normal-map highlights must not invert on mirrored tangent tests.
- Single-sided geometry must not disappear unless its imported winding/cull state
  says so.
- BLEND and MASK materials must retain visibility and not force depth artifacts.
- Imported light test scenes should render with visible light contributions once
  the corresponding renderer feature is in place.
- `NodePerformanceTest.glb` should move renderer counters without reuploading
  static meshes/textures every frame.

## Not Included

- `Sponza` is not included because Khronos lists it under a Cryengine limited
  license. It may be useful for lighting later, but it needs explicit license
  approval before adding it to this project.

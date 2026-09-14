# Vulkan Render Optimization

A modular C++20 engine and graphics experiment focused on high-performance Vulkan rendering, scene culling, and graphics optimization techniques.

---

## Project Status and Disclaimer

This project is an ongoing experimental development foundation. It is incomplete, active work-in-progress, and contains known bugs across several subsystems.

The primary motivation for publishing this repository is to share the Vulkan rendering and optimization code. If you are developing your own engine, custom viewport, or graphics pipeline, you are welcome to inspect, copy, borrow, or adapt any of the Vulkan rendering, culling, or optimization routines implemented here for your own projects.

---

## Development Methodology

This project represents a synthesis of custom applied mathematics and AI-assisted software engineering:

- **Applied Mathematics:** The underlying spatial partitioning logic, projection transforms, screen-space error metrics for LOD transitions, bounding volume evaluations, and culling heuristics were formulated using domain-specific applied mathematical models designed for high-throughput graphics workloads.
- **AI-Assisted Architecture:** The engine scaffolding, modular C++20 boilerplate, and rapid systems integration were implemented with extensive assistance from modern AI systems.

This hybrid methodology enabled rapid prototyping, architectural iteration, and continuous stress-testing of complex Vulkan rendering and scene-management pipelines.

---

## Free and Open Usage

All code in this repository is provided openly for reference, learning, and integration:

- You may freely copy and integrate the Vulkan rendering optimization algorithms, shaders, culling logic, and pipeline setup into your own tools, engines, or games.
- You can modify, adapt, or refactor any module without restriction.
- No warranties or royalties; feel free to take whatever is useful and adapt it to your requirements.

---

## Implemented Systems and Areas of Focus

- **Vulkan Viewport and Rendering Backend:** Native Vulkan pipeline handling swapchain management, mesh and shadow passes, descriptor updates, and live GPU/CPU profiling metrics.
- **Scene Culling and Optimization:**
  - Frustum culling and occlusion culling routines.
  - Hierarchical Level of Detail (LOD and HLOD) management with screen-space error estimation and hysteresis.
  - Cascaded shadow maps (CSM) with shadow caster culling and draw budget management.
- **Asset Processing:** Fast glTF/GLB parser and texture loading paths with binary asset caching.
- **Editor Shell:** Qt Widgets editor framework featuring Advanced Docking System (ADS) docking panels and transform gizmos (intended for development and testing).
- **Core Architecture:** Decoupled C++20 modules spanning core utilities, math, scene management, and scripting foundations.

---

## Building the Project

### With Editor Support (Requires Qt 6 and a C++20 Compiler):

```powershell
cmake --preset dev-editor
cmake --build --preset dev-editor
ctest --preset dev-editor
```

### Core Engine and Renderer Only (No Qt Dependency):

```powershell
cmake --preset dev-core
cmake --build --preset dev-core
ctest --preset dev-core
```

For environment setup and Qt configuration details, refer to `docs/qt_setup.md`.

---

## Documentation

Additional technical notes, design documents, and module specifications can be found under the `docs/` directory.

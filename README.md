# Vulkan Render Optimization

A modular C++20 engine and graphics experiment focused on high-performance Vulkan rendering, scene culling, and graphics optimization techniques.

---

## Project Status and Disclaimer

This project is an ongoing experimental development foundation. It is incomplete, active work-in-progress, and contains known bugs across several subsystems.

The primary motivation for publishing this repository is to share the Vulkan rendering and optimization code. If you are developing your own engine, custom viewport, or graphics pipeline, you are welcome to inspect, copy, borrow, or adapt any of the Vulkan rendering, culling, or optimization routines implemented here for your own projects.

---

## Development Methodology

This project was designed and formulated around custom mathematical models and algorithmic logic, integrated and verified through agentic AI workflows:

- **Author's Logic & Applied Mathematics:** The core mathematical formulations—including spatial partitioning, projection transforms, screen-space LOD metrics, and culling logic—were conceived and designed by the author for high-performance graphics workloads.
- **Agent-Driven Integration & Code Scanning:** Autonomous AI agents were leveraged to perform deep repository scanning, static auditing, module scaffolding, and continuous integration across the C++20 engine subsystems.

This methodology enabled custom mathematical designs to be rapidly implemented into a functional Vulkan rendering pipeline and validated through agent-assisted engineering.

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

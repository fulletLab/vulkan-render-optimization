# Dependency License Notes

This file records license checks before a dependency is integrated.

## Integrated In Phase 1

- Qt Widgets: Qt offers commercial licensing and open-source LGPL/GPL options. For proprietary distribution under LGPL, dynamically link LGPL modules and avoid GPL-only Qt modules unless a commercial license is used.
- Qt Advanced Docking System: upstream repository states LGPL v2.1. This is acceptable for an editor dependency when dynamically linked and license obligations are respected.

## Integrated In Phase 2

- nlohmann/json: MIT License. Used as a header-only JSON serializer for engine scene files.

## Integrated In Phase 3

- No new external dependency was integrated. The editor viewport uses Qt Widgets/QPainter from the already reviewed Qt editor dependency.

## Integrated In Phase 4

- tinygizmo: upstream `README` and `LICENSE` declare Unlicense/public-domain terms. The Phase 4 editor integration pins commit `99c1c418d169774b0b8052b57fd1680b4c4de444`.

## Integrated In Phase 5

- Im3d: upstream repository declares MIT licensing and describes API-agnostic output of world-space primitive vertex buffers. The Phase 5 editor integration pins commit `3fd7afaf3192ed50bca29c191969aa41252b53d5`.

## Integrated In Phase 6

- TinyGLTF: upstream repository declares MIT licensing. Its release branch bundles stb image headers used by the loader; the Phase 6 import path pins commit `d31c16e333a6c8d593cad43f325f4e1825dd4776`.
- stb_image: upstream stb repository and `stb_image.h` identify the image loader as public-domain/MIT style dual licensing. Phase 6 uses the stb image loader bundled by the pinned TinyGLTF source tree so the implementation is compiled once.
- MikkTSpace: upstream source header grants commercial use, modification, and redistribution under zlib-style restrictions. The Phase 6 import path pins commit `3e895b49d05ea07e4c2133156cfa94369e19e409`.
- meshoptimizer: upstream repository declares MIT licensing and requests product attribution. The Phase 6 import path pins commit `c619e7b941646e72ad1da67c058811207bcbcf88`.
- Assimp: upstream repository states its license is based on modified 3-clause BSD. It remains optional and is not integrated in Phase 6 because GLB/glTF is handled by TinyGLTF.

References checked:

- https://doc.qt.io/QT-6/licensing.html
- https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System
- https://githubuser0xffff.github.io/Qt-Advanced-Docking-System/doc/user-guide.html
- https://github.com/nlohmann/json
- https://github.com/ddiakopoulos/tinygizmo
- https://github.com/john-chapman/im3d
- https://github.com/syoyo/tinygltf
- https://github.com/nothings/stb
- https://github.com/mmikk/MikkTSpace
- https://github.com/zeux/meshoptimizer
- https://github.com/assimp/assimp

## Pending Review Before Future Integration

- HighMap: must be reviewed carefully before use because GPL-style licensing would be incompatible with a closed commercial engine distribution unless isolated or replaced.
- KDDockWidgets: not selected for Phase 1 due GPL/commercial licensing risk compared with ADS.
- All rendering, physics, navigation, asset, UI runtime, security, and baking libraries must be checked before being vendored or fetched.

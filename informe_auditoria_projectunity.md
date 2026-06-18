# AUDITORIA PROJECTUNITY - ESTADO REAL

Fecha de auditoria: 2026-06-16  
Alcance: auditoria tecnica estatica del workspace `C:\Users\fullet\Documents\projectunity`.  
Importante: no ejecute build, `ctest`, smoke visible ni RenderDoc en esta pasada. Por eso ningun modulo queda marcado como "cerrado al 100%" aunque haya evidencia fuerte en codigo y tests.

## 1. Resumen ejecutivo brutal

- ProjectUnity no es humo: hay una arquitectura C++20/CMake real con modulos separados de core, math, terrain, scene, physics, scripting, assets, renderer y editor Qt.
- El renderer Vulkan existe de verdad y no es solo un wrapper: crea superficies, swapchains, command buffers, pipelines de mesh/color/shadow, sube buffers/texturas y expone contadores detallados.
- El viewport Qt/QPainter sigue existiendo como fallback y para caminos auxiliares. El codigo intenta impedir que meshes importados caigan al fallback CPU cuando Vulkan ya esta listo, pero el fallback no desaparecio.
- El importador GLB/glTF es de los modulos mas solidos: materiales PBR, texturas, samplers, luces, camaras, tangentes, LODs, batches, clusters, FFULT y tests.
- Play Mode existe, pero es un runtime snapshot dentro del editor con ventana independiente, no un ejecutable runtime separado tipo player build.
- El runtime cooking existe como transformacion de escena y politica de static/dynamic masking, pero no hay un asset/runtime cooked world independiente con formato propio.
- El scripting C++ por DLL esta bastante bien encaminado: target `ProjectUnityGameScripts`, export `registerProjectScripts`, copia por generacion para evitar locks y metadata de fields.
- El player/camera/input ya no parece hardcodeado en viewport: el movimiento esta en `FlyPlayerController.cpp`, con input pasado por `ScriptRuntime` y captura de mouse desde el viewport Game.
- Project Browser y Hierarchy manejan sub-assets grandes con paginas lazy. Hay tests de 600 y 10.000 nodos. Esto es real.
- La Hierarchy no es todavia una jerarquia Unity completa: acepta drops de assets, materializa proxies editables, pero no encontre drag interno para reparent/reorder de GameObjects.
- Material/textura override por instancia o por sub-nodo no existe como sistema persistente. Hay materiales importados, no slots editables estilo Unity.
- Material node editor no existe como editor. Existe un `MaterialGraphAsset` de datos y validacion, mas documentacion de arquitectura futura.
- Terrain es real para generar, chunkear, LODs, normales/tangentes, sculpt brush y guardar `.terrain.json`, pero texture painting esta desactivado y el collider runtime se declara parcial.
- Physics es basico: raycast/contacto vertical contra terrain, gravedad simplificada y componentes Rigidbody/Collider. No hay Jolt/Bullet/PhysX, triggers reales, layers, CharacterController ni colisiones cuerpo-cuerpo.
- HLOD/LOD/chunks son reales en RenderWorld: screen error, histéresis, budgets, overview draws, chunk collapse y counters. Aun asi es politica de viewport/renderer, no sistema authoring completo.
- Sombras Vulkan son reales: cascaded directional, spot/point, alpha mask, culling, budgets, proxy HLOD para shadow casters y contadores CPU/GPU. Falta shadow policy editable por usuario/instancia.
- Profiler existe con una tabla amplia y status bar: FPS, CPU/GPU, command/resource/shadow timings, draw calls, LOD/HLOD, occlusion, uploads, hitches y counters Vulkan.
- Save/load cubre escena, jerarquia, transforms, mesh renderer, runtimeCook, luces, camaras, scripts, terrain, rigidbody/collider. No cubre material overrides porque esos overrides no existen.
- Los tests son abundantes, pero no fueron ejecutados en esta auditoria. La evidencia de tests es "hay cobertura escrita", no "verde hoy".
- Veredicto corto: el motor tiene renderer usable, pero editor/runtime incompletos.

## 2. Tabla global de modulos

| Modulo | Estado | Evidencia principal | Riesgo | Siguiente accion |
|---|---|---|---|---|
| Arquitectura general | PARCIAL | `CMakeLists.txt`, `cmake/ProjectUnityOptions.cmake`, modulos `engine/*`, `editor/*` | Base sana, pero docs mezclan roadmap e implementacion | Definir matriz de estado por modulo y hacer build limpio |
| Vulkan viewport | PARCIAL AVANZADO | `engine/renderer/src/VulkanRenderer.cpp`, `VulkanViewportTarget.cpp`, `ViewportWidget.cpp` | Fallback QPainter sigue vivo; superficie Vulkan no implementada en todas las plataformas | Ejecutar smoke visible y documentar fallback real |
| Play Mode / Runtime | PARCIAL | `editor/app/src/MainWindowPlay.cpp`, `MainWindowSmoke.cpp` | Runtime snapshot en editor, no player/runtime separado | Decidir si el objetivo es ventana runtime o ejecutable runtime |
| Runtime cooking | PARCIAL | `cookPlayRuntimeScene`, `RuntimeAssetInstance`, `RuntimeBatchMask`, stats de chunks/drawPackets | "Cooked" es escena transformada, no asset cocinado independiente | Crear `CookedScene/RuntimeWorld` explicito o renombrar expectativas |
| Scripting C++ DLL | PARCIAL AVANZADO | `Project/CMakeLists.txt`, `ProjectUnityGameScripts.cpp`, `ScriptModuleLoader` | Reload detiene Play; depende de build target y pruebas actuales | Probar rebuild/reload y mejorar flujo live |
| Player/camera/input | PARCIAL AVANZADO | `FlyPlayerController.cpp`, `ViewportWidgetGameScripts.cpp`, `ScriptRuntime.cpp` | Tick fijo de 16 ms, `fixedUpdate` no integrado al viewport Game | Integrar fixed/update loop y pruebas manuales WASD/mouse |
| Project Browser | PARCIAL AVANZADO | `ProjectBrowserWidget.cpp`, `ProjectAssetTreeWidget.cpp`, `project_browser_tests.cpp` | QTreeWidget lazy, no modelo virtual completo | Probar 10k+ assets y memoria/UI |
| Hierarchy Unity-like | PARCIAL | `SceneHierarchyWidget.cpp`, `scene_hierarchy_tests.cpp` | No encontre drag interno reparent/reorder | Implementar reparent/reorder y persistencia visual |
| Parent-child GameObjects | PARCIAL | `Scene::setParent`, serialization de `parent`, `children` | Backend existe, UI incompleta | Exponer operaciones seguras en Hierarchy |
| Asset import GLB/glTF | PARCIAL AVANZADO | `AssetManager.cpp`, `FfultAssetFormat.cpp`, `asset_tests.cpp` | Solo triangulos; import pesado debe validarse con escenas reales | Ejecutar pack visual y perf tests |
| Materials/textures overrides | NO EXISTE / FALSO POSITIVO | Ausencia de `MaterialOverride/TextureOverride`; `MeshRendererComponent` solo referencia model/primitive | No se puede editar material por instancia/nodo | Diseñar `MaterialSlotOverrideComponent` persistente |
| Material node editor | PARCIAL DE DATOS | `MaterialGraphAsset.hpp/.cpp`, `material_graph_tests.cpp`, docs | No hay UI, asset type, storage ni compilacion renderer | Convertir data model en asset real y editor Qt |
| Terrain editor | PARCIAL | `MainWindowTerrain.cpp`, `TerrainGenerator.cpp`, tests terrain | Texture painting desactivado, collider runtime parcial | Splatmaps, materiales reales y validacion perf |
| Terrain brush | PARCIAL | `handleTerrainBrushEvent`, `applyBrush`, viewport brush callbacks | Rebuild/registra mesh por dab; requiere transform simple | Optimizar dirty regions y brush stroke batching |
| Physics/Rigidbody/Colliders | PARCIAL BAJO | `PhysicsWorld.cpp`, `MainWindowGameObjects.cpp`, `physics_tests.cpp` | No hay motor fisico completo | Integrar Jolt o renombrar claramente a BasicPhysics |
| Terrain collider | PARCIAL | `sampleTerrain`, `raycast`, `stepBasic`, `colliderDirty` | Solo heightfield simple, transform limitado | Heightfield collider real con backend fisico |
| HLOD/LOD/chunks | PARCIAL AVANZADO | `ViewportMeshLod.cpp`, `ViewportRenderWorldOverview.cpp`, `ViewportRenderWorldSettings.hpp` | Tuning por env vars; renderer-only | Mover settings a proyecto/UI y validar con mapas externos |
| Shadow system | PARCIAL AVANZADO | `VulkanViewportTargetShadows.cpp`, `RenderShadowSetup`, shadow tests | Falta authoring/override por instancia y QA visual | Probar RenderDoc + escenas con alpha/doubleSided/offscreen |
| Shadow proxy | PARCIAL RENDERER-ONLY | `collectShadowCasters` emite `shadowHlodProxyDrawCount` | No es proxy editable ni asset persistente | Definir politica authoring para casters/proxies |
| Profiler/counters | PARCIAL AVANZADO | `RendererStats`, `MainWindowProfiler.cpp`, `VulkanGpuFrameProfiler` | Algunos counters dependen de frame presentado; no validado hoy | Guardar snapshots y comparar en culling profiles |
| Save/load scene | PARCIAL | `SceneSerialization.cpp`, hierarchy tests | Falta material overrides, shadow policy, missing asset recovery fuerte | Expandir schema con overrides y migraciones |
| Inspector/Add Component | PARCIAL | `MainWindow.cpp`, `MainWindowPanels.cpp`, `MainWindowGameObjects.cpp` | Scripts/physics/terrain si; material slots no | Inspector de materials, render flags y overrides |
| Tests/docs | PARCIAL | `tests/*`, `docs/*` | Docs historicos/roadmap pueden sobreprometer | Ejecutar presets y separar "estado actual" de "roadmap" |

## 3. Detalle por modulo

### Modulo: Arquitectura general

Estado: PARCIAL.

Evidencia:
- Proyecto CMake raiz con C++20 y subdirectorios `engine/core`, `math`, `debug`, `terrain`, `scene`, `physics`, `scripting`, `assets`, `renderer`, `Project`, `editor/viewport`, `editor/app`, `tests`.
- Qt/ADS en editor, Vulkan en renderer, TinyGLTF/MikkTSpace/meshoptimizer/libktx en assets, TinyGizmo/Im3d en viewport.

Archivos revisados:
- `CMakeLists.txt`
- `cmake/ProjectUnityOptions.cmake`
- `cmake/ProjectUnityDependencies.cmake`
- `editor/app/CMakeLists.txt`
- `editor/viewport/CMakeLists.txt`
- `engine/renderer/CMakeLists.txt`
- `engine/scripting/CMakeLists.txt`

Que si existe:
- Modularizacion razonable y targets separados.
- Build flags C++20 consistentes.
- Tests organizados por modulo.

Que falta:
- Separacion clara entre editor runtime, player runtime y engine runtime.
- Estado unico de verdad: varios docs son roadmap/historico.

Que parece fake/parcial:
- Algunas capacidades aparecen como "fase" o "roadmap" en docs antes de estar cerradas.
- No hay directorio/runtime executable dedicado; Play Mode vive en editor.

Riesgos:
- Crecer features encima de nombres ambiguos como "runtime cooking" sin un contrato de runtime real.
- Dependencias externas de viewport como TinyGizmo/Im3d pueden chocar con el objetivo si se queria cero libs externas de gizmo/debug.

Como probarlo manualmente:
- `cmake --build --preset dev-core`
- `ctest --preset dev-core --output-on-failure`
- `cmake --build --preset dev-editor-local-qt`
- Abrir editor y verificar Scene/Game/Profiler/Project Browser.

Siguiente fix recomendado:
- Crear un `docs/status_actual.md` generado/curado despues de cada build verde, separando implementado, parcial y roadmap.

### Modulo: Vulkan viewport

Estado: PARCIAL AVANZADO.

Evidencia:
- `VulkanRenderer::renderSurfaceFrame` prepara surface/swapchain, llama `VulkanViewportTarget::renderFrame`, acumula stats reales y tracks uploads.
- `VulkanViewportTarget::recordFrameCommand` valida draws importados, construye batches, prepara resources, graba shadow pass, mesh pass, wire pass y color scene-aid pass.
- `ViewportWidget::paintEvent` intenta renderizar frame Vulkan; si Vulkan esta listo pero falla, evita dibujar meshes importados por CPU fallback.

Archivos revisados:
- `engine/renderer/src/VulkanRenderer.cpp`
- `engine/renderer/src/VulkanViewportTarget.cpp`
- `engine/renderer/src/VulkanMeshPipeline.cpp`
- `engine/renderer/src/VulkanColorPipeline.cpp`
- `editor/viewport/src/ViewportWidget.cpp`
- `editor/viewport/src/ViewportWidgetRenderer.cpp`
- `editor/viewport/src/ViewportWidgetDraw.cpp`

Que si existe:
- Backend Vulkan real.
- Mesh pass con pipelines normal, flipped winding, double-sided y transparent.
- Color mesh path renderer-owned para gizmo/grid/axes/hierarchy aids.
- Upload caches de mesh/texture y stats.
- GPU timestamp profiler.

Que falta:
- Prueba visible actual en esta auditoria.
- Implementacion cross-platform completa: hay ruta que lanza "Vulkan viewport surface creation is not implemented for this platform".
- Retirar o encerrar mejor el fallback CPU para no confundir estado.

Que parece fake/parcial:
- QPainter no es el renderer principal, pero existe y puede cubrir offscreen/fallback.
- Docs de fase dicen que smoke paso en algun momento; eso no prueba el estado actual.

Riesgos:
- Bugs en fallback pueden ocultar fallos de Vulkan en offscreen.
- Windows visible puede estar bien y otra plataforma rota.

Como probarlo manualmente:
- Abrir editor visible en Windows, importar `examples/basic_assets/TexturedTriangle.gltf`, confirmar `viewportFramesPresented > 0`, `vkDrawIndexed > 0`, `texturedMeshDrawsPresented > 0`.
- Ejecutar smoke visible y comparar Profiler con frame importado vs viewport vacio.

Siguiente fix recomendado:
- Agregar un smoke obligatorio que falle si Scene View importado se renderiza por QPainter cuando Vulkan esta listo.

### Modulo: Play Mode / Runtime

Estado: PARCIAL.

Evidencia:
- `MainWindow::startPlayMode` busca una Camera, cocina `playRuntimeScene_`, inicia `ScriptRuntime`, abre ventana independiente `ViewportWidget(ViewportMode::Game)`.
- `MainWindowSmoke.cpp` verifica que Play abre ventana independiente, no reutiliza `gameViewport_`, mutar runtime no cambia editor scene y stop destruye snapshot.

Archivos revisados:
- `editor/app/src/MainWindowPlay.cpp`
- `editor/app/src/MainWindowSmoke.cpp`
- `editor/app/include/projectunity/editor/MainWindow.hpp`

Que si existe:
- `playRuntimeScene_` separada de `scene_`.
- `playRuntimeViewport_` como ventana Game independiente.
- Input y scripts conectados a snapshot runtime.
- Stop limpia runtime scene, camera id y viewport.

Que falta:
- Runtime executable independiente.
- Estado no editable desde UI runtime mas alla de que la hierarchy principal apunta a editor scene.
- Flujo robusto para errores de cooking/scripts.

Que parece fake/parcial:
- "Runtime" significa snapshot dentro del editor, no build/player.
- La ventana es Qt editor-side.

Riesgos:
- Si se espera runtime real, la arquitectura actual no basta.
- Sistemas de editor pueden seguir compartiendo servicios globales con Play.

Como probarlo manualmente:
- Crear Player, pulsar Play, verificar ventana `ProjectUnity Runtime`.
- Mover Player en Play y confirmar que al salir no cambia la escena editor.
- Cerrar ventana runtime y verificar que Play se detiene.

Siguiente fix recomendado:
- Definir contrato: `EditorPlayRuntime` vs `StandaloneRuntime`. Si se quiere standalone, crear target separado.

### Modulo: Runtime cooking

Estado: PARCIAL.

Evidencia:
- `cookPlayRuntimeScene` crea nombres `RuntimeAssetInstance`, `RuntimeBatchMask`, `RuntimeEntity`, `RuntimeOverride`.
- Cuenta chunks, draw packets, merged/instanced parts, editable proxies y dynamic masks.
- Copia components de transform, mesh, light, camera, terrain, rigidbody, collider y scripts.

Archivos revisados:
- `editor/app/src/MainWindowPlay.cpp`
- `tests/editor_viewport_tests/runtime_cook_tests.cpp`
- `editor/viewport/src/ViewportRenderWorld.cpp`

Que si existe:
- Snapshot runtime con static/dynamic split.
- Mascara de batch para proxies dinamicos.
- RenderWorld puede usar snapshot para evitar duplicar objetos static/dynamic.

Que falta:
- Tipo explicito `CookedScene`, `RuntimeWorld`, `CookedMeshChunk` o formato persistente.
- Validacion de que los counters correspondan a recursos GPU cocinados reales y no solo a conteos de modelo.

Que parece fake/parcial:
- `RuntimeAssetInstance` es nombre de entidad, no clase/asset runtime.
- "drawsAfterCompile" sale de `runtimeDrawPackets`, no de una compilacion de renderer independiente.

Riesgos:
- El equipo puede creer que hay un pipeline runtime cook completo cuando hoy es principalmente una transformacion de escena para Play.

Como probarlo manualmente:
- Importar GLB grande, materializar proxy editable, mover un nodo, iniciar Play y revisar logs `CookRuntimeScene`.
- Confirmar que un proxy dinamico no duplica el batch static.

Siguiente fix recomendado:
- Introducir un objeto de resultado de cooking con schema, invariantes y tests que prueben recursos/cambios, no solo nombres.

### Modulo: Scripting C++ DLL

Estado: PARCIAL AVANZADO.

Evidencia:
- `Project/CMakeLists.txt` crea `ProjectUnityGameScripts` como shared library desde `Project/Assets/Scripts/*.cpp`.
- `ProjectUnityGameScripts.cpp` exporta `registerProjectScripts` y registra `FlyPlayerController` y `Health`.
- `ScriptRuntime.hpp` define alias `namespace ProjectUnity { namespace Scripting = ::projectunity::scripting; }`, por lo que el export con `ProjectUnity::Scripting` es valido.
- `ScriptModuleLoader` copia la DLL a `ProjectScripts_runtime_<pid>_<gen>` antes de cargarla.

Archivos revisados:
- `Project/CMakeLists.txt`
- `Project/Assets/Scripts/ProjectUnityGameScripts.cpp`
- `engine/scripting/include/projectunity/scripting/ScriptRuntime.hpp`
- `engine/scripting/src/ScriptRuntime.cpp`
- `engine/scripting/src/BuiltInScripts.cpp`
- `tests/scripting_tests/scripting_tests.cpp`

Que si existe:
- Registro por asset path.
- Fields por script.
- Reemplazo de script preservando campos coincidentes.
- Mensaje claro para script no cargado.
- Built-in gameplay desactivado, no fallback falso.

Que falta:
- Hot reload live real durante Play: `reloadProjectScriptsModule` llama `stopPlayMode`.
- Consola/diagnostico mas completo para errores de build/reload.
- Verificacion de ABI/versioning de scripts.

Que parece fake/parcial:
- "Reload scripts" es seguro pero no live hot-reload completo.

Riesgos:
- DLL ABI puede romperse silenciosamente si cambian estructuras del engine.
- `file(GLOB *.cpp)` mete todo cpp del folder; conviene controlar archivos o convencion.

Como probarlo manualmente:
- Modificar campo default de `Health.cpp`, Build Scripts Module, Reload Scripts, Add Component y confirmar campos.
- Intentar agregar un `.cpp` no registrado y verificar error esperado.

Siguiente fix recomendado:
- Crear versionado de `registerProjectScripts` y reporte de module generation en UI.

### Modulo: Player/camera/input

Estado: PARCIAL AVANZADO.

Evidencia:
- `FlyPlayerController.cpp` implementa WASD, Shift sprint, Space jump, Tab capture mouse, Escape release, yaw/pitch y actualizacion de CameraComponent.
- `ViewportWidgetGameScripts.cpp` mapea teclas Qt a `InputState`, maneja mouse capture y llama `ScriptRuntime::update` cada 16 ms.
- `ScriptRuntime::update` ejecuta scripts y luego `physics::stepBasic`.

Archivos revisados:
- `Project/Assets/Scripts/FlyPlayerController.cpp`
- `editor/viewport/src/ViewportWidgetGameScripts.cpp`
- `editor/viewport/src/ViewportWidget.cpp`
- `editor/viewport/src/ViewportWidgetRenderer.cpp`
- `engine/scripting/src/ScriptRuntime.cpp`
- `tests/scripting_tests/scripting_tests.cpp`

Que si existe:
- Movimiento fuera del viewport, en script nativo.
- Game mode separa input de Scene camera.
- Captura de mouse implementada como servicio de input.
- Tests de mouse capture, disabled script, no camera, snapshot isolation.

Que falta:
- Loop runtime con delta real y fixed timestep integrado en viewport.
- Game View fallback camera sigue existiendo si no hay CameraComponent.
- Mas input: mouse buttons, controller, rebinding, axes.

Que parece fake/parcial:
- `fixedUpdate` existe en `ScriptRuntime`, pero el viewport Game solo llama `update` en timer.

Riesgos:
- Movimiento dependiente de timer fijo si hay stutter.
- Input limitado para gameplay real.

Como probarlo manualmente:
- Play, Tab para capturar mouse, WASD/Shift/Space, Escape.
- Verificar que en Scene View WASD mueve editor camera, pero en Game solo input runtime.

Siguiente fix recomendado:
- Crear `RuntimeLoop` con accumulated fixed step, delta real y tests de update/fixedUpdate desde Game viewport.

### Modulo: Project Browser

Estado: PARCIAL AVANZADO.

Evidencia:
- `ProjectBrowserWidget` muestra filesystem `Assets`, `Imported Assets`, grupos Meshes/Materials/Textures/Nodes y paginas lazy.
- `ProjectAssetTreeWidget` genera MIME drag con asset id/path.
- `project_browser_tests.cpp` valida modelo de 600 nodos, paginas de 256 y stable sub-asset id across rebuild.

Archivos revisados:
- `editor/app/src/ProjectBrowserWidget.cpp`
- `editor/app/src/ProjectAssetTreeWidget.cpp`
- `tests/editor_viewport_tests/project_browser_tests.cpp`

Que si existe:
- Busqueda, grupos, iconos, thumbnails basicos, lazy paging.
- Drag payload para asset id/path.
- Separacion de assets fisicos e importados cacheados.

Que falta:
- Modelo virtual completo para bibliotecas enormes.
- Operaciones de asset editing/renaming/import settings robustas.
- Material graph/material asset real en browser.

Que parece fake/parcial:
- "Sub-assets" son vistas de datos importados; no necesariamente assets editables/persistentes por separado.

Riesgos:
- QTreeWidget puede degradarse con proyectos reales grandes.
- Los IDs de sub-assets necesitan contrato fuerte si van a usarse para overrides.

Como probarlo manualmente:
- Importar GLB con miles de nodos y expandir `Nodes / Primitives`; medir memoria y tiempo.
- Arrastrar GLB a Hierarchy y confirmar creacion de entidad.

Siguiente fix recomendado:
- Migrar a modelo Qt virtual si el proyecto apunta a 10k-100k assets reales.

### Modulo: Hierarchy Unity-like

Estado: PARCIAL.

Evidencia:
- `SceneHierarchyWidget` crea grupos Meshes/Materials/Textures/Nodes para modelos.
- Usa paginas de 256 y materializa un proxy editable solo al seleccionar un nodo.
- `scene_hierarchy_tests.cpp` valida 10.000 nodos, 40 paginas, proxy ligero, transform override serializado y source asset intacto.

Archivos revisados:
- `editor/app/src/SceneHierarchyWidget.cpp`
- `tests/editor_viewport_tests/scene_hierarchy_tests.cpp`
- `engine/scene/src/Scene.cpp`
- `engine/scene/src/SceneSerialization.cpp`

Que si existe:
- Visualizacion escalable de nodos importados.
- Proxies no renderables para overrides.
- Persistencia de transform override de proxies.
- Drop de assets desde Project Browser.

Que falta:
- Drag interno de GameObjects para reparent/reorder: `setDragDropMode(QAbstractItemView::DropOnly)` solo acepta drops.
- UI clara para crear hijos bajo seleccion y gestionar orden.
- Overrides de material/texture por nodo.

Que parece fake/parcial:
- La jerarquia "tipo Unity" aplica a visualizacion/proxies, no a todo el UX de Unity Hierarchy.

Riesgos:
- Usuarios pueden esperar poder reparentar por drag y no podran.
- Proxies materializados pueden confundirse con nodos fuente.

Como probarlo manualmente:
- Importar `NodePerformanceTest.glb`, expandir Nodes, seleccionar un nodo, moverlo y guardar/cargar escena.
- Intentar arrastrar un GameObject dentro de otro: debe documentarse si no funciona.

Siguiente fix recomendado:
- Implementar internal drag/drop con validacion de ciclos usando `Scene::setParent`.

### Modulo: Parent-child GameObjects

Estado: PARCIAL.

Evidencia:
- `Scene` tiene `parent`, `children`, `rootEntities`, `createEntity(parent)`, `setParent`, `duplicateEntityRecursive`.
- Serialization guarda `parent` y reconstruye children validando referencias.

Archivos revisados:
- `engine/scene/include/projectunity/scene/Scene.hpp`
- `engine/scene/src/Scene.cpp`
- `engine/scene/src/SceneSerialization.cpp`

Que si existe:
- Backend de jerarquia.
- Prevencion de parent invalido/ciclos.
- Duplicacion recursiva.
- Save/load de parent-child.

Que falta:
- UX completa en Hierarchy.
- Undo/redo para reparent/duplicate/delete.

Que parece fake/parcial:
- Backend completo no implica editor completo.

Riesgos:
- Operaciones programaticas y UI pueden divergir.

Como probarlo manualmente:
- Crear entidades parent/child, guardar/cargar y duplicar parent.
- Probar reparent programatico con ciclo y verificar rechazo.

Siguiente fix recomendado:
- Conectar backend a UI con comandos undoables.

### Modulo: Asset import GLB/glTF

Estado: PARCIAL AVANZADO.

Evidencia:
- `AssetManager.cpp` carga glTF/GLB, texturas, samplers, materiales, nodos, luces, camaras, tangentes, batches y clusters.
- `AssetManager.hpp` define `MaterialAsset`, `TextureAsset`, `ModelAsset`, `MeshPrimitive`, `MeshLod`, `MeshPrimitiveInstance`, `MeshEditorInstance`.
- Tests verifican materiales PBR, alpha, doubleSided, samplers, transforms, negative determinant, KTX, FFULT y spatial batching.

Archivos revisados:
- `engine/assets/include/projectunity/assets/AssetManager.hpp`
- `engine/assets/src/AssetManager.cpp`
- `engine/assets/src/FfultAssetFormat.cpp`
- `engine/assets/src/MeshPrimitiveBatcher.cpp`
- `engine/assets/src/GltfNodeTransforms.cpp`
- `tests/asset_tests/asset_tests.cpp`
- `tests/asset_tests/spatial_batch_tests.cpp`

Que si existe:
- Preservacion de source geometry/materials/textures en gran parte.
- Runtime LOD no destructivo basado en index buffers.
- `editorInstances` para mantener nodos editables.
- Color-space paths en renderer: base/emissive sRGB, normal/MR/occlusion linear.

Que falta:
- Validacion manual con escenas externas representativas hoy.
- Soporte completo de formatos no glTF mencionado en roadmap.
- Material overrides por instancia.

Que parece fake/parcial:
- Batching/import optimization no debe confundirse con perdida/destruccion de asset fuente; el codigo conserva editor/source paths, pero hay que medir visualmente.

Riesgos:
- Modelos raros con skins/animations/morphs/material extensions pueden no estar cubiertos.
- Caches FFULT viejos pueden quedar incompatibles si cambia schema.

Como probarlo manualmente:
- Importar pack con node.matrix, negative scale, vertex colors, alpha mask/blend, normal/MR/occlusion/emissive.
- Comparar contra viewer de referencia.

Siguiente fix recomendado:
- Crear asset validation report por import con warnings y features unsupported.

### Modulo: Materials/textures overrides

Estado: NO EXISTE / FALSO POSITIVO.

Evidencia:
- `MeshRendererComponent` referencia `modelAssetId`, indices de primitive/editor y `runtimeCook`; no contiene slots ni overrides.
- `AssetType` solo tiene `Texture2D` y `Model`.
- Busqueda no encontro `MaterialOverride`, `TextureOverride`, `MaterialSlotOverride`, reset override ni serializacion de override material.
- Inspector muestra transform/scripts/terrain/physics, no slots de material importado editables.

Archivos revisados:
- `engine/scene/include/projectunity/scene/Scene.hpp`
- `engine/scene/src/SceneSerialization.cpp`
- `engine/assets/include/projectunity/assets/AssetManager.hpp`
- `editor/app/src/MainWindow.cpp`
- `editor/app/src/MainWindowPanels.cpp`

Que si existe:
- Materiales importados con factores PBR y texturas.
- Renderer usa textures/materials del `ModelAsset`.
- Project Browser/Hierarchy muestran grupos Materials/Textures.

Que falta:
- Overrides por entidad/sub-nodo.
- Drag/drop de texturas/materiales a slots.
- Persistencia de override.
- Reset/revert override.

Que parece fake/parcial:
- Ver "Materials" en Project Browser/Hierarchy no significa edicion de slots.

Riesgos:
- Cualquier edicion global del material importado afectaria muchas primitivas si no hay override de instancia.

Como probarlo manualmente:
- Seleccionar nodo importado y buscar slots de material editables en Inspector. No deberian existir.
- Intentar arrastrar una textura a un sub-nodo y guardar/cargar; no hay flujo.

Siguiente fix recomendado:
- Implementar `MaterialOverrideComponent` con clave `(modelAssetId, materialIndex/editorInstanceIndex)` y serializacion.

### Modulo: Material node editor

Estado: PARCIAL DE DATOS.

Evidencia:
- `MaterialGraphAsset.hpp/.cpp` define data model renderer-independent, nodos, pins, conexiones, parametros, preview/editor state, validacion y hashes.
- `material_graph_tests.cpp` valida grafo, ciclos, type mismatch, duplicate pins.
- `docs/material_graph_architecture.md` dice explicitamente `Status: PARCIAL / design approved for a future implementation phase`.

Archivos revisados:
- `engine/assets/include/projectunity/assets/MaterialGraphAsset.hpp`
- `engine/assets/src/MaterialGraphAsset.cpp`
- `tests/asset_tests/material_graph_tests.cpp`
- `docs/material_graph_architecture.md`

Que si existe:
- Data model y validacion.
- Intencion arquitectonica sin Qt/ImGui en runtime.

Que falta:
- `AssetType::MaterialGraph`.
- Save/load FFULT o asset manager storage.
- UI Qt node editor.
- Compilacion a material/shader renderer.
- Preview real.

Que parece fake/parcial:
- "MaterialGraphAsset" no equivale a "material node editor".

Riesgos:
- El equipo puede construir UI encima sin resolver storage/render backend.

Como probarlo manualmente:
- Buscar una opcion para crear/abrir material graph en editor. No existe como flujo funcional.

Siguiente fix recomendado:
- Fase 1: asset type + serialization + Project Browser create/open. Fase 2: UI. Fase 3: renderer backend.

### Modulo: Terrain editor

Estado: PARCIAL.

Evidencia:
- `TerrainGenerator` genera heightmap determinista, chunks, normals/tangents, LOD indices, bounds.
- `MainWindowTerrain` expone settings, material layers, generate/regenerate, clear/flat y save terrain asset.
- Tests verifican determinismo, chunks, LOD data, winding +Y, brush y raycast.

Archivos revisados:
- `engine/terrain/include/projectunity/terrain/TerrainTypes.hpp`
- `engine/terrain/src/TerrainGenerator.cpp`
- `editor/app/src/MainWindowTerrain.cpp`
- `editor/app/src/MainWindowGameObjects.cpp`
- `tests/terrain_tests/terrain_tests.cpp`

Que si existe:
- Generacion y rebuild.
- Chunking y LOD indices.
- Normales/tangentes.
- Capas de material como datos.
- Export `.terrain.json`.

Que falta:
- Texture/splat painting.
- Material layer rendering completo como terrain shader dedicado.
- Streaming/dirty region update.
- Navmesh/Recast si eso estaba en objetivo.

Que parece fake/parcial:
- UI dice "Paint Texture (coming next)" y tooltip "not implemented in this phase".

Riesgos:
- Rebuild completo por edicion puede ser caro en terrenos grandes.

Como probarlo manualmente:
- Crear Terrain, regenerar, sculpt, guardar `.terrain.json`, cerrar/cargar escena.
- Confirmar que texture painting esta deshabilitado.

Siguiente fix recomendado:
- Implementar splatmap asset y actualizar solo regiones afectadas por brush.

### Modulo: Terrain brush

Estado: PARCIAL.

Evidencia:
- Viewport envia `ViewportTerrainBrushEvent`.
- `handleTerrainBrushEvent` raycastea heightmap local, aplica Raise/Lower/Smooth/Flatten y reconstruye mesh.
- `TerrainGenerator::applyBrush` modifica heightmap con falloff.

Archivos revisados:
- `editor/viewport/include/projectunity/editor/ViewportWidget.hpp`
- `editor/viewport/src/ViewportWidget.cpp`
- `editor/app/src/MainWindowTerrain.cpp`
- `engine/terrain/src/TerrainGenerator.cpp`

Que si existe:
- Hover/begin/drag/end.
- Shift lower.
- Brush radius/strength/falloff/target height.
- Ring de brush en viewport.

Que falta:
- Stroke batching.
- Undo/redo.
- Rotation/negative scale support.
- Edicion de texturas.

Que parece fake/parcial:
- Terrain "Edit Mode" no implica terrain tool completo.

Riesgos:
- Rebuild per dab puede stutter.

Como probarlo manualmente:
- Activar Edit Terrain in Scene View, usar Raise/Lower/Smooth/Flatten y medir frame hitches en Profiler.

Siguiente fix recomendado:
- Crear comando undoable por stroke y actualizar chunks afectados.

### Modulo: Physics/Rigidbody/Colliders

Estado: PARCIAL BAJO.

Evidencia:
- `Scene` tiene `RigidbodyComponent` y `ColliderComponent`.
- UI permite agregar Rigidbody y Box/Sphere/Capsule/Mesh/Terrain Collider, pero labels/logs dicen basic/PARCIAL.
- `PhysicsWorld::stepBasic` aplica gravedad simple y resuelve penetracion vertical contra terrain.

Archivos revisados:
- `engine/physics/include/projectunity/physics/PhysicsWorld.hpp`
- `engine/physics/src/PhysicsWorld.cpp`
- `engine/scene/include/projectunity/scene/Scene.hpp`
- `editor/app/src/MainWindowGameObjects.cpp`
- `editor/app/src/MainWindow.cpp`
- `tests/physics_tests/physics_tests.cpp`

Que si existe:
- Componentes y serializacion.
- Raycast terrain.
- Contacto vertical terrain para body con collider.
- Dirty flag de terrain collider.

Que falta:
- Jolt/Bullet/PhysX.
- Body-body collision.
- Shapes reales para box/sphere/capsule/mesh salvo offset bottom.
- Triggers/events/layers.
- CharacterController.
- Velocidad/fuerzas/integracion fisica real.

Que parece fake/parcial:
- Menu "Rigidbody" no significa fisica completa.
- `trigger` se serializa, pero `stepBasic` lo salta; no hay evento trigger.

Riesgos:
- Gameplay sobre esto sera inestable y no determinista.

Como probarlo manualmente:
- Crear Terrain con collider y sphere rigidbody encima; Play y verificar caida/resolucion vertical.
- Crear dos bodies sin terrain y confirmar que no colisionan.

Siguiente fix recomendado:
- Integrar Jolt o aislar `BasicPhysics` con nombres honestos hasta que exista backend real.

### Modulo: Terrain collider

Estado: PARCIAL.

Evidencia:
- `hasTerrainCollider` usa `terrain.settings.generateCollider` o Collider shape Terrain.
- `supportedTerrainTransform` exige rotacion cero y escala positiva.
- `raycast` y `sampleTerrain` usan `TerrainGenerator::raycast/sampleHeight`.
- `stepBasic` limpia `colliderDirty`.

Archivos revisados:
- `engine/physics/src/PhysicsWorld.cpp`
- `engine/terrain/src/TerrainGenerator.cpp`
- `editor/app/src/MainWindowTerrain.cpp`
- `tests/physics_tests/physics_tests.cpp`

Que si existe:
- Raycast heightfield.
- Sampling de altura.
- Resolucion vertical de contacto.

Que falta:
- Collider heightfield en motor fisico.
- Rotacion/escala general.
- Mesh collider/terrain collider broadphase.

Que parece fake/parcial:
- `ColliderShape::Terrain` existe, pero no es un backend fisico robusto.

Riesgos:
- Escenas con terrain rotado/escalado raro no funcionan.

Como probarlo manualmente:
- Rotar terrain y probar raycast/contacto; debe fallar/ignorarse segun codigo.

Siguiente fix recomendado:
- Construir heightfield collider real y rechazar en UI transform no soportado con aviso visible.

### Modulo: HLOD/LOD/chunks

Estado: PARCIAL AVANZADO.

Evidencia:
- `ViewportMeshLod.cpp` selecciona LOD por error proyectado en pixeles e histéresis.
- `ViewportRenderWorldSettings.hpp` define thresholds/budgets por env vars `PROJECTUNITY_HLOD_*`.
- `ViewportRenderWorldOverview.cpp` calcula chunks visibles, ratios, budgets, coverage, root/chunk HLOD history y emite overview draws.
- `RendererStats` y `RenderFrame` tienen counters LOD/HLOD/occlusion/spatial cells.
- Tests `viewport_render_world_hlod_tests.cpp` validan pitch stability, HLOD collapse, duplicated root identity y shadow HLOD proxy.

Archivos revisados:
- `editor/viewport/src/ViewportMeshLod.cpp`
- `editor/viewport/src/ViewportRenderWorldSettings.hpp`
- `editor/viewport/src/ViewportRenderWorldOverview.cpp`
- `editor/viewport/src/ViewportRenderWorldRecord.hpp`
- `engine/renderer/include/projectunity/renderer/RendererTypes.hpp`
- `tests/editor_viewport_tests/viewport_render_world_hlod_tests.cpp`

Que si existe:
- LOD runtime no destructivo.
- Histéresis LOD/HLOD.
- Root and chunk overview draws.
- Draw packet/chunk budgets.
- Counters profundos.

Que falta:
- Authoring UI/proyecto para thresholds.
- Validacion visual/manual actual.
- Garantia de calidad para assets externos grandes.

Que parece fake/parcial:
- HLOD es renderer/viewport overview, no asset HLOD authorable/persistente.

Riesgos:
- Env vars hacen tuning invisible para usuarios.
- Overviews pueden bajar fidelidad si thresholds no estan calibrados.

Como probarlo manualmente:
- Cargar asset >8M triangles o fixture HLOD, mover camara cerca/lejos y mirar counters `HLOD`, `LOD`, chunks visibles.
- Probar `PROJECTUNITY_HLOD_DEBUG_OVERRIDE=detailed/hlod`.

Siguiente fix recomendado:
- Mover HLOD settings a panel/debug project settings y guardar snapshots profiler por asset.

### Modulo: Shadow system

Estado: PARCIAL AVANZADO.

Evidencia:
- `RenderShadowMode` soporta `DirectionalCascades`, `Spot2D`, `PointCubemap`.
- `collectShadowCasters` incluye offscreen directional casters si su sombra puede alcanzar camara, descarta blend, aplica LOD y HLOD proxy.
- `applyViewportShadowPolicy` presupone max shadow casters y prioriza seleccionados/proyectados.
- `VulkanViewportTargetShadows.cpp` graba shadow pass, reuse frozen/live unchanged map, alpha mask descriptor, doubleSided/flipped winding pipelines y counters.

Archivos revisados:
- `engine/renderer/include/projectunity/renderer/RendererTypes.hpp`
- `engine/renderer/src/VulkanViewportTargetShadows.cpp`
- `engine/renderer/src/VulkanViewportTargetShadowResources.cpp`
- `engine/renderer/src/RenderShadowSetup.cpp`
- `editor/viewport/src/ViewportRenderWorldShadowCasters.cpp`
- `editor/viewport/src/ViewportRenderWorldShadowPolicy.cpp`
- `tests/renderer_tests/renderer_tests.cpp`
- `tests/editor_viewport_tests/editor_viewport_tests.cpp`

Que si existe:
- Cascaded directional shadows.
- Spot 2D fallback and point cubemap.
- Alpha MASK shadow sampling.
- Transparent BLEND excluded.
- Caster culling and policy rejection counters.
- GPU/CPU shadow times.

Que falta:
- UI de shadow policy por light/material/instance.
- Calidad configurable de filtering/bias/cascades.
- QA visual actual en escenas complejas.

Que parece fake/parcial:
- Shadow proxy existe como overview draw renderer-only, no como objeto editable.

Riesgos:
- Sombras offscreen y alpha/doubleSided son propensas a regresiones visuales.

Como probarlo manualmente:
- Escena con Directional + offscreen caster + alpha mask + doubleSided + point light.
- Ver Profiler: `shadowCandidateInstances`, `shadowPolicyRejectedInstances`, `shadowBatchesSubmitted`, GPU shadow time.
- Capturar RenderDoc del shadow pass.

Siguiente fix recomendado:
- Crear suite visual automatizada para shadow cases y exponer policy en Lighting/Inspector.

### Modulo: Shadow proxy

Estado: PARCIAL RENDERER-ONLY.

Evidencia:
- `collectShadowCasters` usa `useShadowOverview` y emite draws de `record->overviewDraws`, incrementando `shadowHlodProxyDrawCount`.
- Test HLOD shadow comprueba que casters lejanos usan proxy draws.

Archivos revisados:
- `editor/viewport/src/ViewportRenderWorldShadowCasters.cpp`
- `tests/editor_viewport_tests/viewport_render_world_hlod_tests.cpp`
- `engine/renderer/include/projectunity/renderer/RendererTypes.hpp`

Que si existe:
- Proxy HLOD para shadow caster selection en renderer.

Que falta:
- Shadow proxy asset.
- Control por instancia/material.
- Persistencia o debug UI especializada.

Que parece fake/parcial:
- El nombre "proxy" puede sonar a sistema authorable, pero es una optimizacion interna.

Riesgos:
- Dificil explicar al usuario por que una sombra usa geometria simplificada.

Como probarlo manualmente:
- Activar debug overlay de shadow/HLOD, cargar asset grande lejano y comparar `shadowHlodProxyDrawCount`.

Siguiente fix recomendado:
- Mostrar en Profiler/overlay cuando una sombra usa HLOD proxy y permitir override por seleccion.

### Modulo: Profiler/counters

Estado: PARCIAL AVANZADO.

Evidencia:
- `RendererStats` contiene GPU/API/VMA, draw calls, triangles, LOD/HLOD, occlusion, chunks, shadows, Vulkan binds/draws, timings, uploads y hitches.
- `VulkanRenderer.cpp` actualiza stats desde `RenderFrame` y `VulkanViewportFrameProfile`.
- `MainWindowProfiler.cpp` muestra 120 filas y status bar con FPS, CPU/GPU, WORLD/CMD/RES, draw/batch/VKD/bind/triangles/LOD/HLOD/OCC/SH.

Archivos revisados:
- `engine/renderer/include/projectunity/renderer/RendererTypes.hpp`
- `engine/renderer/src/VulkanRenderer.cpp`
- `engine/renderer/src/VulkanViewportTarget.cpp`
- `engine/renderer/src/VulkanGpuFrameProfiler.cpp`
- `editor/app/src/MainWindowProfiler.cpp`
- `editor/app/src/MainWindowCullingProfile.cpp`

Que si existe:
- Contadores reales alimentados por frame presentado.
- GPU timestamps cuando soportados.
- Hitch tracking.
- Breakdown LOD/material.

Que falta:
- Persistir/exportar snapshots.
- Verificacion de counters contra RenderDoc/perf captures hoy.
- UI de tendencias/graficas.

Que parece fake/parcial:
- Algunos contadores dependen de que Vulkan presente; si no hay frame, pueden mostrar fallback/renderer global.

Riesgos:
- Un contador incorrecto puede guiar optimizaciones malas.

Como probarlo manualmente:
- Abrir Profiler, mirar `Scene View` como fuente, mover camara a asset y a vacio, comparar draws/triangles/FPS.
- Ejecutar culling profile si esta integrado.

Siguiente fix recomendado:
- Boton "Export profiler snapshot" con JSON y fixture de expected ranges.

### Modulo: Save/load scene

Estado: PARCIAL.

Evidencia:
- Serialization guarda transform, parent, meshRenderer, runtimeCook, light, camera, scripts, terrain settings/layers/heightmap, rigidbody/collider y componentOrder.
- Deserialize valida ids, parents, camera clipping, terrain heightmap size, collider values, script data.

Archivos revisados:
- `engine/scene/src/SceneSerialization.cpp`
- `engine/scene/src/SceneScriptSerialization.cpp`
- `tests/scene_tests`
- `tests/editor_viewport_tests/scene_hierarchy_tests.cpp`

Que si existe:
- Persistencia central de escena.
- Jerarquia y proxies de node override.
- Terrain y physics basic.
- Script fields.

Que falta:
- Material/texture overrides.
- Shadow policy/render flags por instancia.
- Migration/versioning mas alla de version strict.
- Missing asset recovery UX robusta.

Que parece fake/parcial:
- Se serializa `partial: true` en rigidbody/collider, honesto pero confirma parcialidad.

Riesgos:
- Al agregar overrides, schema debe evitar romper escenas existentes.

Como probarlo manualmente:
- Crear escena con imported model, proxy editado, player script, terrain, collider, guardar/cargar y comparar.

Siguiente fix recomendado:
- Agregar tests golden JSON con migraciones y missing asset/script warnings.

### Modulo: Inspector/Add Component

Estado: PARCIAL.

Evidencia:
- Inspector maneja transform, scripts con fields, rigidbody/collider, terrain panel.
- `MainWindowGameObjects.cpp` crea menus para Camera, Lights, Terrain, Rigidbody/Collider basic, Scripts.
- Script Add Component lista scripts registrados y muestra scripts no cargados con error esperado.

Archivos revisados:
- `editor/app/src/MainWindow.cpp`
- `editor/app/src/MainWindowPanels.cpp`
- `editor/app/src/MainWindowGameObjects.cpp`
- `editor/app/src/MainWindowPlay.cpp`

Que si existe:
- Add/remove de scripts, physics basic y terrain.
- Fields editables por script.
- UI de renderer debug toggles.

Que falta:
- Material slots.
- Renderer flags/shadow flags por entity/material.
- Prefab controls.
- Build Settings: log dice "Build Settings is not implemented yet".

Que parece fake/parcial:
- Menus con "(basic/PARCIAL)" son honestos, pero usuarios aun pueden asumir completitud.

Riesgos:
- Inspector puede quedar como acumulador de controles sin modelo de undo/validation.

Como probarlo manualmente:
- Seleccionar entity, agregar FlyPlayer/Health, editar fields, guardar/cargar.
- Intentar editar material imported: no hay flow.

Siguiente fix recomendado:
- Introducir component editor registry y undoable property commands.

### Modulo: Tests/docs

Estado: PARCIAL.

Evidencia:
- `tests/CMakeLists.txt` incluye core, math, debug, terrain, scene, physics, scripting, asset, renderer, editor_viewport y source rules.
- Hay tests especificos de Project Browser, Hierarchy, HLOD, runtime cook, scripting, terrain, physics y renderer stats/shadows.
- Docs `phase6.md`, `renderer_status.md`, `material_graph_architecture.md`, `asset_import_pipeline_roadmap.md` mezclan estado, historial y planes.

Archivos revisados:
- `tests/CMakeLists.txt`
- `tests/*`
- `docs/phase6.md`
- `docs/renderer_status.md`
- `docs/material_graph_architecture.md`
- `docs/asset_import_pipeline_roadmap.md`
- `README.md`

Que si existe:
- Cobertura escrita amplia.
- Tests offscreen Qt para widgets clave.
- Renderer tests para shadow selection/stats.

Que falta:
- Ejecucion actual en esta auditoria.
- Separar documentos historicos de estado real vigente.
- Artefactos de build/smoke adjuntos por commit.

Que parece fake/parcial:
- Frases tipo "Latest check passed" en docs son historicas; no deben usarse como prueba actual.

Riesgos:
- Documentacion optimista puede ocultar regresiones.

Como probarlo manualmente:
- Ejecutar presets core/editor y smoke visible.
- Guardar logs con fecha y commit/worktree status.

Siguiente fix recomendado:
- CI local script `tools/verify_current_state.ps1` que produzca un reporte markdown de checks reales.

## 4. Lista de bloqueos criticos

### P0

1. No hay build/test ejecutado en esta auditoria. Antes de declarar cualquier modulo como completo, correr presets y smoke visible.
2. Materials/textures overrides no existen. Esto bloquea workflows Unity-like de importar modelo, seleccionar submesh/nodo y cambiar material sin tocar asset fuente.
3. Physics no es motor fisico completo. Si el objetivo exige Jolt/terrain collider/character controller/triggers, este modulo esta lejos.
4. Play Mode no es runtime/player independiente. Si el objetivo es "jugar fuera del editor", falta target runtime.

### P1

1. Material node editor solo tiene data model. Falta asset type, storage, UI y renderer backend.
2. Hierarchy no tiene reparent/reorder por drag interno. Backend existe, UX no.
3. Runtime cooking no produce un cooked world/resource explicito; hoy es snapshot y naming de entidades.
4. Terrain texture painting esta desactivado y collider runtime es parcial.
5. Shadow policy/proxy no es authorable ni persistente.

### P2

1. HLOD/LOD settings dependen de env vars, no de proyecto/UI.
2. Profiler no exporta snapshots ni esta validado contra captura actual.
3. Vulkan viewport tiene fallback CPU y plataforma no-Windows incompleta.
4. Build Settings no implementado.
5. Hot reload de scripts detiene Play y no es live reload real.

### P3

1. Docs mezclan roadmap, historial y estado actual.
2. QTreeWidget lazy puede no escalar a proyectos masivos reales.
3. Faltan undo/redo para varias operaciones editor.
4. Falta UI clara para missing assets/scripts/materials.

## 5. Lista de "no te dejes enganar"

- "RuntimeAssetInstance" es nombre de entidad, no clase de asset runtime cocinado.
- "Runtime cooking" no equivale todavia a pipeline standalone de build/player.
- "MaterialGraphAsset" no equivale a material node editor.
- Ver grupos "Materials" y "Textures" en Project Browser/Hierarchy no significa que haya material override editable.
- `RigidbodyComponent` y `ColliderComponent` no significan fisica completa; el propio codigo/logs dicen basic/PARCIAL.
- `ColliderShape::Mesh` existe en enum/UI, pero `stepBasic` no hace mesh collision real.
- `trigger` se serializa, pero no hay eventos trigger implementados.
- "Terrain collider" es raycast/contacto heightfield simple con transform limitado, no Jolt terrain collider.
- "Paint Texture (coming next)" esta deshabilitado.
- HLOD/shadow proxy son optimizaciones renderer-only, no assets/proxies authorables.
- Docs con "Latest check passed" son historicas si no se ejecutan hoy.
- QPainter fallback existe; no asumir que todo frame visto en offscreen prueba Vulkan.
- TinyGizmo/Im3d son dependencias externas de viewport/debug aunque no son Dear ImGui/ImGuizmo.
- Build Settings aparece como accion/log, pero no esta implementado.
- Tests escritos no equivalen a tests verdes en el worktree actual.

## 6. Que debe hacer cada agente despues

### Agente 1 - Assets / Project Browser / Hierarchy / Materials

- Implementar `MaterialSlotOverride` persistente por entidad/sub-nodo/material index.
- Agregar drag/drop de texture/material a slots del Inspector.
- Separar sub-assets visuales de assets editables reales.
- Convertir `MaterialGraphAsset` en `AssetType::MaterialGraph` con save/load y Project Browser create/open.
- Implementar reparent/reorder interno en Hierarchy usando `Scene::setParent`.
- Agregar tests de save/load material overrides y missing material.

### Agente 2 - Renderer / HLOD / LOD / Shadows / Profiler

- Ejecutar smoke visible Vulkan y capturas RenderDoc.
- Validar counters con escenas grandes: chunks visibles, draw packets, LOD reductions, HLOD overview, shadow casters.
- Mover `PROJECTUNITY_HLOD_*` a Project Settings/Profiler UI.
- Exponer shadow policy por light/material/instance y debug overlay de proxy.
- Reducir confusion del fallback QPainter: status visible cuando se usa fallback.
- Agregar export JSON de profiler snapshots.

### Agente 3 - Scripting / Play Mode / Input

- Decidir y documentar si Play Mode es `EditorPlayRuntime` o si se necesita `StandaloneRuntime`.
- Integrar runtime loop con delta real y fixed timestep.
- Mejorar build/reload scripts: errores detallados, version ABI y reload seguro.
- Probar WASD/mouse/jump en ventana runtime visible.
- Agregar input map/rebinding basico.
- Crear tests de fixedUpdate desde Game viewport.

### Agente 4 - Terrain / Physics

- Integrar backend fisico real, preferiblemente Jolt si ese es el objetivo del proyecto.
- Implementar heightfield terrain collider real.
- Implementar body-body collisions, triggers, layers y collision events.
- Agregar CharacterController o controller de jugador fisico.
- Implementar splatmap/texture painting y terrain material shader path.
- Optimizar brush por dirty chunks y undo por stroke.

### Build Coordinator

- Ejecutar:
  - `cmake --build --preset dev-core`
  - `ctest --preset dev-core --output-on-failure`
  - `cmake --build --preset dev-editor-local-qt`
  - `ctest --preset dev-editor-local-qt --output-on-failure`
  - smoke visible `projectunity_editor --smoke-test`
- Guardar logs con fecha, commit/worktree status y GPU.
- Actualizar docs solo con resultados recien ejecutados.
- Marcar docs roadmap como roadmap para que no se lean como estado actual.

## 7. Veredicto final

El motor tiene renderer usable pero editor/runtime incompletos.

La parte mas fuerte del proyecto esta en asset import + Vulkan viewport + RenderWorld/LOD/HLOD/shadow/profiler. Eso no es una maqueta vacia: hay codigo real, contadores reales y tests escritos.

La parte mas debil esta en workflows de editor/runtime que el usuario esperaria de un motor tipo Unity: material overrides, node material editor, fisica completa, terrain painting, Play Mode standalone, hierarchy reparenting y authoring persistente de render/shadow policies.

Mi recomendacion tecnica es no empezar otra capa grande de features hasta cerrar tres bases: build/test actual verificable, overrides de materiales persistentes, y definicion honesta de runtime/play mode. Si esas tres se estabilizan, el renderer que ya existe puede sostener mucho mas producto encima.

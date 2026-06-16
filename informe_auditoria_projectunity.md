Actúa como auditor técnico senior de un motor gráfico/editor llamado ProjectUnity.

NO quiero que programes fixes todavía.
NO quiero que “asumas que está hecho”.
NO quiero una respuesta optimista.
Quiero una auditoría brutalmente honesta, módulo por módulo, revisando el código real del repositorio.

Ruta del proyecto:
C:\Users\fullet\Documents\projectunity

Contexto general:
Este proyecto es un motor/editor en C++20, CMake, Qt Widgets/ADS y Vulkan. La idea es tener un editor tipo Unity/Unreal pero propio, sin Dear ImGui ni ImGuizmo. El editor debe manejar assets GLB/glTF grandes, escenas enormes, jerarquías complejas, terreno editable, Play Mode real, scripting C++ modular, física/colliders reales, materiales/texturas editables por instancia, HLOD/LOD, sombras optimizadas y profiler/counters.

REGLAS IMPORTANTES:

1. Esta tarea es SOLO VERIFICACIÓN/AUDITORÍA.
2. No modifiques archivos.
3. No hagas refactors.
4. No borres código.
5. No “arregles” nada todavía.
6. No ejecutes builds pesados salvo que el usuario lo pida explícitamente.
7. Si necesitas validar algo, usa análisis estático, búsqueda en archivos, lectura de código, grep/ripgrep, revisión de CMake y revisión de flujos.
8. Si algo parece implementado solo en UI pero no tiene lógica real, márcalo como PARCIAL o FALSO POSITIVO.
9. Si algo está hardcodeado, simulado, fake, placeholder o solo documentado, márcalo claramente.
10. Cada conclusión debe tener evidencia: archivo, función/clase, línea aproximada o patrón de código encontrado.
11. No digas “completo” si no puedes encontrar evidencia real de integración.
12. Si una característica depende de tests/build/manual test, marca “requiere prueba manual” pero no la des por completa.
13. El reporte final debe separar: COMPLETO, PARCIAL, BLOQUEADO, FALSO POSITIVO, RIESGO CRÍTICO y SIGUIENTE ACCIÓN.

Objetivo de esta auditoría:
Quiero saber exactamente qué falta del motor gráfico/editor tomando en cuenta todo el plan anterior, qué quedó a medias, qué se implementó de verdad y qué solo parece implementado.

============================================================
FASE 1 — REVISAR ESTRUCTURA GENERAL DEL PROYECTO
================================================

Verifica:

1. Estructura real del repositorio:

   * engine/
   * editor/
   * runtime/
   * assets/
   * renderer/
   * scripting/
   * physics/
   * terrain/
   * docs/
   * tests/
   * CMakeLists.txt principales.

2. Confirmar tecnología real:

   * C++20.
   * CMake.
   * Qt Widgets/ADS para editor.
   * Vulkan para viewport/render real.
   * Sin Dear ImGui.
   * Sin ImGuizmo.
   * Sin soluciones falsas tipo QPainter para render final de escena.

3. Detectar si hay sistemas duplicados:

   * viewport CPU viejo vs viewport Vulkan.
   * Play Mode viejo vs Play Runtime nuevo.
   * scripting built-in vs scripting DLL real.
   * física fake vs física real.
   * terrain panel-only vs terrain brush real.

Salida esperada:

* Tabla con módulos encontrados.
* Archivo principal de cada módulo.
* Estado: COMPLETO / PARCIAL / BLOQUEADO / NO EXISTE.
* Comentario corto y evidencia.

============================================================
FASE 2 — PLAY MODE / RUNTIME REAL
=================================

Verifica si Play Mode es realmente un runtime separado o solo una cámara/game view falsa.

Requisitos esperados:

1. Al presionar Play:

   * Debe crear snapshot/cooked runtime scene separado del Editor World.
   * Debe abrir una ventana/runtime viewport independiente o modo Play claramente separado.
   * El Editor World debe seguir existiendo sin ser destruido.
   * El Play snapshot no debe ser editable desde la jerarquía de editor.
   * Cambios en Play no deben modificar automáticamente la escena de editor salvo sistema explícito.

2. Runtime cooking esperado:

   * Assets estáticos decorativos se convierten en representación runtime optimizada.
   * RuntimeAssetInstance / chunks / LOD / culling / batching / instancing / shadow proxy.
   * Objetos dinámicos, interactivos, físicos o scripted se extraen como RuntimeEntity/GameplayObject.
   * Separación “static cooked world + dynamic breakout entities”.
   * No basta con clonar entidades; verificar si realmente hay cooking/optimización.

3. Verificar:

   * Dónde se crea la snapshot.
   * Dónde se guarda.
   * Qué se copia.
   * Qué se optimiza.
   * Qué componentes entran al runtime.
   * Si scripts corren en esa snapshot.
   * Si físicas corren en esa snapshot.
   * Si la cámara runtime es la CameraComponent de la escena y no la Scene View camera.

Marcar:

* COMPLETO si hay pipeline real editor → cooked runtime → play window → runtime loop.
* PARCIAL si solo hay snapshot/clon pero no cooking.
* FALSO POSITIVO si Play solo mueve la cámara del editor o usa el mundo editable directamente.

Buscar archivos/clases relacionadas:

* PlayMode
* RuntimeScene
* RuntimeWorld
* PlayRuntimeViewport
* GameView
* SceneSnapshot
* CookedScene
* RuntimeAssetInstance
* RendererRuntime
* EditorWorld
* SceneWorld

Preguntas que debes responder:

* ¿Play Mode usa el mundo del editor directamente?
* ¿Hay ventana runtime real?
* ¿Hay snapshot no editable?
* ¿Hay cooking de assets grandes?
* ¿Hay separación entre objetos estáticos y dinámicos?
* ¿El input runtime afecta solo al runtime?
* ¿La cámara del editor está separada de la cámara del juego?

============================================================
FASE 3 — SCRIPTING C++ / DLL / HOT RELOAD
=========================================

Audita si el sistema de scripting C++ está realmente como se pidió.

Requisitos esperados:

1. Scripts de usuario en:
   Project/Assets/Scripts/*.cpp

2. Compilación separada:

   * No debe requerir recompilar todo el editor.
   * Debe generar algo tipo:
     Project/Binaries/Scripts/ProjectUnityGameScripts.dll

3. Carga dinámica:

   * LoadLibrary/dlopen equivalente.
   * Copia a DLL versionada runtime para evitar lock de Windows:
     ProjectScripts_runtime_001.dll, ProjectScripts_runtime_002.dll, etc.
   * Export estable:
     extern "C" __declspec(dllexport) void registerProjectScripts(projectunity::scripting::ScriptRegistry& registry);

4. Registry:

   * ScriptRegistry real.
   * Las clases se registran desde la DLL.
   * Add Component -> Script lista clases cargadas desde la DLL.
   * Si existe .cpp pero la clase no está cargada, debe mostrar:
     “Script asset exists but native class is not loaded. Build/Reload Project Scripts.”

5. Inspector:

   * Campos por metadata/reflection manual.
   * Ejemplo FlyPlayerController:
     speed
     sprintSpeed
     jumpForce
     gravity
     mouseSensitivity
   * No todos los scripts deben mostrar los mismos campos.
   * Debe guardar valores por instancia.

6. Separación correcta:

   * BuiltInScripts.cpp no debe contener gameplay principal del usuario salvo fallback/test.
   * FlyPlayerController debe vivir como script de proyecto.
   * Health, EnemyAI u otros deben ser registrables desde DLL.

7. Hot reload:

   * Botón Build Scripts Module.
   * Botón Reload Scripts.
   * Manejo de errores de compilación.
   * No crashea si DLL falla.
   * Instancias existentes se reconstruyen o quedan marcadas como missing script correctamente.

8. Input/script runtime:

   * El script recibe InputState.
   * Movimiento de Player debe ocurrir solo desde ScriptComponent/ScriptRuntime.
   * Scene View camera no debe mover al Player.
   * Si Player no tiene ScriptComponent activo, WASD/Space no deben moverlo.
   * Si FlyPlayerController está desactivado/removido, no debe moverse.
   * No debe existir doble movimiento por hardcode del viewport + script.

9. Mouse capture:

   * TAB debe poder activar/desactivar captura del mouse si está implementado.
   * ESC debería liberar captura.
   * El script puede pedir capture, pero el viewport/runtime debe aplicar cursor hide/show/relative delta.
   * Si solo existe un bool en el script y no hay soporte real del viewport, marcar PARCIAL/FALSO POSITIVO.

Buscar:

* ScriptModuleLoader
* ScriptRegistry
* ScriptComponent
* BuiltInScripts
* FlyPlayerController
* Build Scripts Module
* Reload Scripts
* ProjectUnityGameScripts
* registerProjectScripts
* LoadLibrary
* FreeLibrary
* mouseCaptured
* setMouseCaptured
* requestMouseCapture
* InputState
* PlayRuntimeViewport

Responder:

* ¿El scripting C++ modular existe de verdad?
* ¿O todavía depende de BuiltInScripts?
* ¿Se puede recompilar script sin recompilar editor?
* ¿La DLL se copia para evitar lock?
* ¿Add Component lista scripts reales?
* ¿Inspector muestra metadata real?
* ¿El movimiento del Player está 100% en scripts?
* ¿TAB/mouse capture funciona realmente o solo está escrito en el script?

============================================================
FASE 4 — PLAYER / CAMERA / INPUT
================================

Auditar específicamente el problema de doble velocidad y hardcode.

Requisitos:

1. Scene View:

   * Cámara de editor puede usar WASD/orbit/pan.
   * Esa cámara no debe modificar transform del Player ni CameraComponent runtime.

2. Play Runtime:

   * Usa CameraComponent de la escena.
   * El input se transforma en InputState.
   * El viewport NO mueve entidades directamente.
   * Solo los scripts/sistemas runtime cambian el transform.

3. Diagnóstico esperado:

   * Buscar cualquier código que modifique Transform de Player/Camera fuera de ScriptRuntime.
   * Buscar nombres:
     player
     camera
     FlyPlayerController
     WASD
     Key_W
     Space
     LeftShift
     mouseDelta
     transform.position
     setPosition
     translate
     moveCamera
     editorCamera
     runtimeCamera

4. Marcar cualquier hardcode:

   * Si Viewport mueve Player directamente: RIESGO CRÍTICO.
   * Si SceneView y Script mueven al mismo tiempo: RIESGO CRÍTICO.
   * Si CameraComponent no se usa en Play: PARCIAL/FALSO POSITIVO.
   * Si no hay separación input editor/runtime: PARCIAL.

Responder:

* ¿Quién mueve el Player?
* ¿Quién mueve la cámara en Play?
* ¿Hay doble aplicación de velocidad?
* ¿Input de editor y runtime están separados?
* ¿TAB para mouse capture está integrado en viewport real?

============================================================
FASE 5 — ASSET PIPELINE / IMPORT GLB / PROJECT BROWSER
======================================================

Auditar importación de assets grandes GLB/glTF.

Requisitos:

1. Import:

   * Importar GLB/glTF sin destruir datos.
   * Meshes, materials, textures, nodes/primitives se conservan.
   * Samplers glTF se respetan.
   * Emissive textures se respetan.
   * alphaMode MASK en shadow pass.
   * doubleSided por material se respeta.
   * baseColor/emissive en sRGB.
   * normal/metallicRoughness/occlusion en linear.
   * textura usada en roles distintos debe tener variantes/cache separadas.

2. Project Browser:

   * Debe mostrar asset importado con estructura:
     AssetRoot
     Meshes (...)
     Materials (...)
     Textures (...)
     Nodes / Primitives (...)
   * No debe ser lista plana.
   * Debe tener iconos reales de editor, no emojis.
   * Debe permitir arrastrar asset a escena/hierarchy.

3. Assets grandes:

   * No debe duplicar memoria pesada por cada instancia.
   * Mesh/material/texture data debe ser compartida.
   * Cada instancia debe tener transform/overrides propios.
   * Soporte para miles de nodes/primitives sin congelar UI.
   * Carga lazy/virtualized en árbol.

Buscar:

* AssetManager
* glTF importer
* TinyGLTF
* MaterialAsset
* TextureAsset
* VulkanTextureCache
* ProjectBrowser
* AssetBrowser
* Import
* GLB
* glTF
* MeshAsset
* Texture cache
* sampler
* emissive
* alphaMode
* doubleSided
* Nodes
* Primitives

Responder:

* ¿El asset browser ya muestra grupos reales?
* ¿Los grupos son reales o solo visuales?
* ¿Importa y preserva materials/textures/samplers?
* ¿Soporta assets enormes sin duplicar memoria?
* ¿Hay lazy loading/virtualization?

============================================================
FASE 6 — HIERARCHY UNITY-LIKE / GAMEOBJECTS / INSTANCIAS
========================================================

Auditar si Hierarchy funciona como Unity y no solo lista entidades.

Requisitos:

1. Para un asset importado en escena, Hierarchy debe mostrar:
   NodePerformanceTest
   Meshes (...)
   Materials (...)
   Textures (...)
   Nodes / Primitives (...)
   Rock_001
   Rock_002
   etc.

2. Los grupos:

   * Meshes / Materials / Textures son referencias/grupos, no GameObjects editables normales.
   * Nodes / Primitives contiene nodos/partes editables.
   * Root asset tiene icono de modelo.
   * Meshes icono de mesh.
   * Materials icono de material.
   * Textures icono/thumbnail.
   * Nodes icono de grupo/nodo.
   * GameObjects, Camera, Light, Player, Terrain tienen iconos propios.

3. Edición por child:

   * Seleccionar un child de Nodes/Primitives.
   * Mover/rotar/escalar individualmente.
   * Guardar override de transform en escena.
   * No modificar GLB original.
   * Al recargar escena, overrides siguen.
   * Debe poder seguir seleccionando cada roca individual aunque en runtime se optimice.

4. Parent-child:

   * Crear Empty/Cube/Sphere/Camera/Light/Player/Terrain.
   * Drag GameObject sobre otro para hacerlo child.
   * Drag out para volver a root.
   * Reordenar.
   * Mantener local/world transform correctamente.
   * Guardar parent IDs, local transforms, orden, componentes.
   * Añadir custom children bajo nodos importados:
     Rock_Granite_001
     InteractionPoint
     PickupCollider

5. Performance:

   * Hierarchy con 10k nodos no debe congelarse.
   * Debe haber lazy expansion/virtualization/paginación.
   * No crear widgets pesados para todos los nodos si no están expandidos.

Buscar:

* Hierarchy
* SceneHierarchy
* TreeView
* QTreeWidget
* QAbstractItemModel
* AssetInstance
* SceneEntity
* Parent
* Child
* LocalTransform
* WorldTransform
* Overrides
* PrimitiveProxy
* NodeProxy
* material group
* mesh group
* texture group

Responder:

* ¿Hierarchy replica la estructura del Project Browser?
* ¿O sigue mostrando assets como un solo objeto cerrado?
* ¿Nodes/Primitives son editables?
* ¿Los overrides se guardan?
* ¿Parent-child real existe?
* ¿Se pueden añadir children custom bajo assets importados?
* ¿Hay virtualización/lazy loading?

============================================================
FASE 7 — MATERIALS / TEXTURES / OVERRIDES / NODE EDITOR
=======================================================

Auditar sistema de materiales y texturas editables.

Requisitos:

1. Selección de root asset:

   * Mostrar slots de materiales.
   * Mostrar referencias a materiales/texturas.
   * Permitir override por instancia.

2. Selección de child/node/primitive:

   * Mostrar Material Slots específicos.
   * Slot 0, Slot 1, etc.
   * Base Color Texture.
   * Normal Texture.
   * Metallic/Roughness.
   * Emissive.
   * Tiling.
   * Offset.
   * Override checkbox.
   * Reset override.

3. Drag/drop:

   * Arrastrar material/texture desde Project Browser a slot.
   * Aplicar solo a instancia/child seleccionado.
   * No modificar asset fuente salvo modo de edición de asset explícito.

4. Persistencia:

   * Overrides guardados en escena.
   * Recarga mantiene cambios.
   * Duplicar instancia no debe romper referencias compartidas.

5. Procedural material/texture node editor:

   * Verificar si existe o no.
   * Si existe, buscar nodos:
     Texture Sample
     Color
     Noise
     Multiply
     Add
     Mix/Lerp
     Normal Map
     UV/Tiling
     Output/BaseColor
   * Si solo hay UI sin shader/material backend, marcar PARCIAL/FALSO POSITIVO.

Buscar:

* MaterialOverride
* MaterialSlot
* TextureSlot
* Inspector
* dragEnterEvent
* dropEvent
* BaseColor
* Normal
* Metallic
* Roughness
* Emissive
* Tiling
* Offset
* Reset override
* MaterialEditor
* TextureNodeEditor
* Procedural
* NodeGraph
* Noise

Responder:

* ¿Se pueden editar materiales por instancia?
* ¿Se pueden editar por child/submesh?
* ¿Se guardan overrides?
* ¿Drag/drop funciona?
* ¿Node editor existe de verdad o es placeholder?

============================================================
FASE 8 — TERRAIN EDITOR REAL
============================

Auditar terreno.

Problema conocido:
El terrain parecía estar como prefab/panel con sliders, pero se necesita edición directa con mouse/brush en Scene View. También hubo problemas de orientación, debug ring gigante/puntos y necesidad de rotar para verlo.

Requisitos:

1. Terrain creation:

   * Crear Terrain desde GameObject/Menu.
   * Terrain aparece en XZ plane.
   * Height va en Y.
   * Normals apuntan +Y.
   * Winding/culling correcto.
   * No requiere rotación oculta ni scale negativo.

2. Terrain edit mode:

   * Seleccionar Terrain.
   * Activar Edit Terrain / Terrain Brush.
   * Brush visible como círculo limpio sobre superficie.
   * Raycast mouse -> terreno.
   * Click/drag modifica altura.
   * Herramientas:
     Raise
     Lower
     Smooth
     Flatten
     Paint Texture, si está implementado de verdad.
   * Brush Size.
   * Brush Strength.
   * Falloff.
   * Target Height.
   * Preview no debe ser gigante ni puntos raros.

3. Runtime/update:

   * Recalcular normals.
   * Actualizar mesh/buffers Vulkan.
   * Actualizar collider/dirty flag.
   * Guardar/load alturas editadas.
   * No solo modificar valores en panel sin editar mesh real.

4. Terrain collider:

   * TerrainColliderComponent o heightfield real.
   * Player/Rigidbody no debe atravesar terreno si física está activa.

Buscar:

* Terrain
* TerrainComponent
* TerrainEditor
* TerrainBrush
* Brush
* Raise
* Lower
* Smooth
* Flatten
* Paint
* Heightmap
* Raycast
* normals
* XZ
* Y-up
* TerrainCollider
* Heightfield

Responder:

* ¿Terrain está orientado correctamente?
* ¿Brush real existe?
* ¿Mouse edita alturas?
* ¿Se actualiza render?
* ¿Se actualiza collider?
* ¿Se guarda/load?
* ¿O solo hay panel visual?

============================================================
FASE 9 — PHYSICS / COLLIDERS / RIGIDBODY
========================================

Auditar física real.

Problema conocido:
Se habló de que colliders/rididbody no funcionaban de verdad, player pasaba a través de terrain/objetos o era fake.

Requisitos:

1. Componentes:

   * RigidbodyComponent.
   * ColliderComponent.
   * TerrainColliderComponent.
   * CharacterController opcional.

2. Rigidbody inspector:

   * Static/Dynamic/Kinematic.
   * Mass.
   * Gravity enabled.
   * Damping.
   * Constraints.
   * Enabled.
   * Remove component.

3. Collider inspector:

   * Box.
   * Sphere.
   * Capsule.
   * Mesh.
   * Terrain.
   * Size/radius/height.
   * Offset.
   * IsTrigger.
   * Physics material.
   * Enabled.
   * Remove component.

4. Backend:

   * Ver si usa Jolt/Bullet/PhysX/custom.
   * Si no hay backend real, marcar BLOQUEADO/PARCIAL.
   * Si solo hay UI/componentes sin simulación, FALSO POSITIVO.

5. Runtime:

   * PhysicsWorld real.
   * Step simulation.
   * Sync transforms.
   * Collision detection.
   * Terrain collision.
   * Dynamic/static bodies.
   * Trigger events si existen.
   * Scripts pueden interactuar o al menos leer estado.

Buscar:

* PhysicsWorld
* RigidbodyComponent
* ColliderComponent
* TerrainCollider
* Jolt
* Bullet
* PhysX
* stepSimulation
* simulate
* collision
* isTrigger
* mass
* gravity
* damping

Responder:

* ¿Hay física real?
* ¿Qué backend usa?
* ¿Colliders afectan movimiento?
* ¿Player puede colisionar?
* ¿Terrain collision existe?
* ¿Inspector es real o solo UI?

============================================================
FASE 10 — ASSET ORIENTATION / CULLING / INVISIBLE FACES
=======================================================

Auditar el bug donde assets/terrain parecían al revés o había que rotarlos para verlos, y caras invisibles.

Requisitos:

1. Motor debe ser Y-up.
2. Terrain en XZ.
3. Height en Y.
4. Normals correctas.
5. Winding correcto.
6. Vulkan frontFace/cullMode correcto.
7. glTF coordinate conversion correcta.
8. doubleSided respetado por material, no global.
9. No usar hack:

   * rotación oculta 120 grados.
   * scale negativo para arreglar inversión.
   * cull none global.
   * doble cara global para esconder bug.
   * cámara hack.

Buscar:

* frontFace
* cullMode
* VK_CULL_MODE
* VK_FRONT_FACE
* doubleSided
* winding
* indices
* normals
* tangent
* coordinate
* Y-up
* Z-up
* rotation
* import transform
* glTF
* negative scale
* 120

Responder:

* ¿El bug de orientación está arreglado de raíz?
* ¿Hay hacks ocultos?
* ¿Terrain y assets comparten el mismo problema?
* ¿doubleSided es per material?
* ¿Hay global cull disable?

============================================================
FASE 11 — HLOD / LOD / CHUNKS / DEBUG COLORS
============================================

Auditar HLOD/LOD.

Contexto:
El asset de 37 MB es un cluster de cientos/miles de rocas. El problema era que al estar en medio de un asset enorme todo se ponía rojo/LOD0, en vez de que solo los chunks cercanos fueran rojos y los lejanos amarillos/morados. También duplicar assets podía cambiar LOD de otra instancia si había estado compartido incorrectamente.

Colores esperados:

* Rojo = muy cerca / LOD0.
* Amarillo = medio.
* Morado = lejos.
* Morado x1 = muy lejos / HLOD single chunk.

Requisitos:

1. LOD debe calcularse por chunk/leaf/local bounds, no por root gigante.
2. Estar dentro del root bounds no debe forzar todo el asset a LOD0.
3. Debug color debe reflejar el LOD real dibujado.
4. Cada instancia debe tener estado LOD propio.
5. No debe haber shared mutable LOD state dentro del asset compartido.
6. Métrica estable:

   * bounding sphere projected radius o error métrico estable.
   * no debe cambiar locamente por ángulo de cámara/near-plane.
   * hysteresis para evitar popping.
7. Culling:

   * per chunk.
   * frustum culling correcto.
   * no debe desaparecer sombras por mover cámara si caster sigue relevante.
8. Config:

   * revisar env vars PROJECTUNITY_HLOD_* si existen.
   * budgets configurables.
   * overlay/profiler de LOD/HLOD.

Buscar:

* HLOD
* LOD
* chunk
* leaf
* bounds
* rootBounds
* screenSize
* projected
* errorMetric
* hysteresis
* debug color
* red
* yellow
* purple
* instance state
* shared state
* PROJECTUNITY_HLOD
* culling
* frustum

Responder:

* ¿LOD se calcula por root o por chunk?
* ¿Debug color coincide con draw real?
* ¿Duplicar assets comparte estado LOD incorrecto?
* ¿Hay hysteresis?
* ¿El ángulo de cámara rompe el LOD?
* ¿Hay overlay/counters útiles?

============================================================
FASE 12 — SHADOWS / SELF-SHADOWING / SHADOW PROXY
=================================================

Auditar sombras.

Contexto:
El asset de rocas de 37 MB puede tener cientos/miles de sub-mallas/rocas individuales. Con una sola instancia puede costar mucho por self-shadowing. Con muchas instancias el budget podía apagar sombras y subir FPS engañosamente.

Requisitos:

1. Shadow pass:

   * Cuenta casters reales.
   * Cuenta draws de shadow pass.
   * Cuenta por asset/instancia/submesh.
   * No apagar todas las sombras visibles simplemente por budget.
   * Budget debe degradar/controlar, no hacer benchmark falso.
   * alphaMode MASK debe funcionar en shadow pass.
   * BLEND policy documentada si no proyecta sombras.

2. Asset-level diagnostics:

   * Para asset 37MB, debe poder contar:

     * número de sub-mallas.
     * shadow caster draw calls.
     * caster instances.
     * shadow triangles.
   * Separar conteo de asset instance vs submesh casters.

3. Per-instance/per-submesh shadow flag:

   * Debe poder marcar rocas pequeñas decorativas como no cast shadow.
   * Flag por sub-malla/instancia.
   * Seguir seleccionando/moviendo la roca individualmente aunque no proyecte sombra.
   * No debe ser solo global cast shadows off.

4. Shadow proxy:

   * Posibilidad de agrupar en shadow proxy simplificado.
   * Mantener visual editable.
   * Runtime puede usar proxy para sombras.

Buscar:

* ShadowDepth
* VulkanShadowPipeline
* shadow caster
* caster count
* shadow draw
* alpha mask
* alphaCutoff
* castShadow
* shadowProxy
* no cast shadow
* self shadow
* budget
* RendererStats
* ViewportRenderWorldShadowPolicy
* cumulative shadow
* lights in last frame

Responder:

* ¿Los counters existen y son correctos?
* ¿Se puede diagnosticar el asset de 37MB?
* ¿Se puede apagar shadow por submesh/instancia?
* ¿Existe shadow proxy?
* ¿La política de budget apaga sombras de forma falsa?

============================================================
FASE 13 — PROFILER / DEBUG OVERLAYS / COUNTERS
==============================================

Auditar herramientas de medición.

Requisitos:

* FPS.
* CPU frame time.
* GPU frame time si existe.
* Draw calls.
* Batches.
* Triangles.
* Visible meshes.
* Culled meshes/chunks.
* LOD/HLOD counts.
* Shadow views.
* Shadow casters.
* Shadow draw calls.
* Lights.
* Texture/memory stats si existe.
* Asset instance count.
* Submesh count.
* Material count.
* Runtime/editor separation counters.

Buscar:

* RendererStats
* Profiler
* overlay
* status bar
* FPS
* draw
* batch
* triangles
* shadows
* lights
* GPU
* CPU
* culling
* HLOD
* LOD

Responder:

* ¿El profiler muestra datos reales?
* ¿O solo valores estimados/fake?
* ¿Los counters vienen del renderer Vulkan real?
* ¿Se separan editor y play/runtime stats?

============================================================
FASE 14 — SAVE / LOAD / SCENE SERIALIZATION
===========================================

Auditar persistencia.

Requisitos:

* Guardar escena.
* Cargar escena.
* Guardar GameObjects.
* Guardar parent-child.
* Guardar local transforms.
* Guardar components.
* Guardar asset instance references.
* Guardar overrides:

  * transform override por child importado.
  * material override.
  * texture override.
  * shadow flag override.
  * terrain heightmap.
  * script component + fields.
  * rigidbody/collider settings.
* No guardar paths absolutos innecesarios.
* No romper si asset no existe.
* Missing assets/scripts deben mostrarse claro.

Buscar:

* SceneSerializer
* saveScene
* loadScene
* JSON
* YAML
* scene file
* asset guid
* uuid
* overrides
* parentId
* localTransform
* components
* terrain height
* script fields

Responder:

* ¿Qué se guarda?
* ¿Qué no se guarda?
* ¿Hay overrides persistentes?
* ¿Se rompe al recargar?
* ¿Usa rutas absolutas?

============================================================
FASE 15 — UI / INSPECTOR / ADD COMPONENT
========================================

Auditar UI de editor.

Requisitos:

* Inspector cambia según selección.
* Add Component real:

  * Script.
  * Rigidbody.
  * Collider.
  * Camera.
  * Light.
  * Terrain.
  * Mesh Renderer.
  * Material slots.
* Remove component.
* Enable/disable component.
* Campos editables afectan runtime/editor real.
* No botones fake.
* No onClick vacíos.
* No opciones pintadas sin lógica.
* Jerarquía usable.
* Project Browser usable.
* Menús funcionan.
* Iconos reales.
* No emojis como iconos principales.
* No freeze en grandes assets.

Buscar:

* Inspector
* AddComponent
* remove component
* enabled
* onClick
* QAction
* QPushButton
* lambda vacía
* TODO
* placeholder
* fake
* icon
* QIcon
* emoji

Responder:

* ¿Qué botones son reales?
* ¿Qué botones son fake?
* ¿Add Component crea componentes reales?
* ¿Inspector edita datos reales?
* ¿Iconos son reales?

============================================================
FASE 16 — TESTS / DOCS / BUILD STATUS
=====================================

Auditar sin ejecutar build pesado salvo permiso.

Requisitos:

* Ver CMake targets.
* Tests existentes.
* Smoke tests.
* Docs actualizados.
* phase6.md.
* renderer_status.md.
* architecture.md.
* Prompt Maestro si existe.
* Ver si docs dicen una cosa y código otra.

Buscar:

* ctest
* tests
* smoke
* asset_tests
* phase6
* renderer_status
* architecture
* docs
* TODO
* FIXME
* PARCIAL

Responder:

* ¿Qué tests existen?
* ¿Qué cubren?
* ¿Qué no cubren?
* ¿Docs están actualizados o mienten?
* ¿Hay TODO/FIXME críticos?

============================================================
FASE 17 — BUSCAR PLACEHOLDERS / FALSOS POSITIVOS
================================================

Haz una búsqueda global por:

* TODO
* FIXME
* placeholder
* stub
* fake
* mock
* not implemented
* unsupported
* temporary
* hack
* hardcoded
* disabled
* return;
* onPress vacío / lambda vacía equivalente
* throw not implemented
* qDebug “not implemented”
* funciones que devuelven true sin hacer nada.

Reporta todos los hallazgos importantes agrupados por módulo.

============================================================
FORMATO DE REPORTE FINAL OBLIGATORIO
====================================

Entrega el reporte con esta estructura exacta:

# AUDITORÍA PROJECTUNITY — ESTADO REAL

## 1. Resumen ejecutivo brutal

Explica en 10-20 líneas:

* qué está más completo,
* qué está parcial,
* qué está fake,
* qué bloquea avanzar,
* qué puede probar manualmente el usuario.

## 2. Tabla global de módulos

Columnas:

* Módulo
* Estado: COMPLETO / PARCIAL / BLOQUEADO / FALSO POSITIVO / NO EXISTE
* Evidencia principal
* Riesgo
* Siguiente acción

Módulos mínimos:

* Arquitectura general
* Vulkan viewport
* Play Mode / Runtime
* Runtime cooking
* Scripting C++ DLL
* Player/camera/input
* Project Browser
* Hierarchy Unity-like
* Parent-child GameObjects
* Asset import GLB/glTF
* Materials/textures overrides
* Material node editor
* Terrain editor
* Terrain brush
* Physics/Rigidbody/Colliders
* Terrain collider
* HLOD/LOD/chunks
* Shadow system
* Shadow proxy
* Profiler/counters
* Save/load scene
* Inspector/Add Component
* Tests/docs

## 3. Detalle por módulo

Para cada módulo usa:

### Módulo: NOMBRE

Estado:
Evidencia:
Archivos revisados:
Qué sí existe:
Qué falta:
Qué parece fake/parcial:
Riesgos:
Cómo probarlo manualmente:
Siguiente fix recomendado:

## 4. Lista de bloqueos críticos

Ordenar por prioridad:
P0 = rompe arquitectura o impide avanzar.
P1 = feature central parcial.
P2 = polish/UX/performance.
P3 = documentación/tests.

## 5. Lista de “no te dejes engañar”

Enumera cosas que parecen hechas pero no lo están:

* UI sin lógica.
* Snapshot sin cooking.
* Script sin hot reload real.
* Física sin backend.
* Terrain panel sin brush real.
* LOD debug color que no coincide con draw.
* Shadows apagadas por budget que suben FPS falsamente.
* Culling global que oculta bug de winding.
* double-sided global.
* movimiento de player hardcodeado fuera del script.
* material slots sin override persistente.

## 6. Qué debe hacer cada agente después

Divide el trabajo en agentes, pero SOLO como recomendación. No modifiques nada.

Agente 1 — Assets / Project Browser / Hierarchy / Materials:

* tareas exactas

Agente 2 — Renderer / HLOD / LOD / Shadows / Profiler:

* tareas exactas

Agente 3 — Scripting / Play Mode / Player / Input:

* tareas exactas

Agente 4 — Terrain / Physics / Colliders:

* tareas exactas

Build Coordinator:

* qué debería compilar después de que los agentes terminen.

## 7. Veredicto final

Usa una de estas frases:

* “El motor está listo para la siguiente fase.”
* “El motor no está listo; hay parciales críticos.”
* “El motor tiene UI avanzada pero backend incompleto.”
* “El motor tiene renderer usable pero editor/runtime incompletos.”
* “El motor requiere estabilización antes de más features.”

Y explica por qué.

IMPORTANTE:
No endulces nada.
No digas que algo está completo sin evidencia.
No programes fixes.
No cambies archivos.
Solo audita y reporta.

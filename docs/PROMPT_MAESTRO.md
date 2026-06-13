# ProjectUnity - Prompt Maestro

Este documento fija el contrato maestro del proyecto. Se usa como referencia de continuidad para no perder el alcance original ni volver a convertir el motor/editor en una demo rota.

## Rol Del Agente

Actuar como:

- Arquitecto senior de motores graficos.
- Desarrollador C++ avanzado.
- Ingeniero de rendering.
- Disenador de editores tipo Unity.
- Ingeniero de networking multiplayer en tiempo real.
- Especialista en seguridad de servidores de videojuegos.

## Objetivo General

Construir un motor grafico/editor 3D modular en C++ moderno, preferiblemente C++20, usando CMake, con editor visual tipo Unity, preparado para:

- Juegos offline.
- Juegos singleplayer.
- Juegos multiplayer en tiempo real.
- Servidores dedicados.
- Herramientas/editor.
- Terrenos 3D.
- Fisica.
- Navmesh/IA.
- Assets.
- Iluminacion.
- UI in-game.
- Seguridad de red.
- Optimizacion real de CPU/GPU/memoria/red.

El proyecto debe separar claramente:

- Editor.
- Runtime.
- Engine Core.
- Server.
- Client.
- Asset Pipeline.
- Tools.

No debe ser una demo rota, un prototipo incompleto ni una coleccion de archivos pegados sin arquitectura.

## Regla Principal Absoluta

Una tarea, modulo, fase o feature solo puede marcarse como terminada si cumple todo esto:

1. Compila correctamente.
2. Esta integrada en el proyecto real.
3. No rompe modulos anteriores.
4. No tiene errores conocidos.
5. No tiene crashes conocidos.
6. No tiene codigo muerto.
7. No tiene TODOs criticos.
8. No tiene hacks temporales sin documentar.
9. Tiene manejo basico de errores.
10. Tiene logs utiles.
11. Tiene pruebas manuales o automaticas segun corresponda.
12. Tiene un ejemplo minimo funcionando.
13. Esta conectada con la UI/editor si aplica.
14. Esta documentada brevemente.
15. Se puede volver a compilar desde cero.
16. No depende de estados magicos o hardcodeados.
17. No tiene rutas absolutas locales del desarrollador.
18. No finge resultados de tests.
19. No dice "terminado" si realmente es "parcial".
20. Si algo falla, se arregla antes de avanzar.

Si una fase no esta completa, reportar:

- Estado: PARCIAL.
- Que funciona.
- Que no funciona.
- Que falta.
- Que errores existen.
- Que archivos estan involucrados.
- Que hay que hacer para terminarla.

No usar frases como:

- "Esto deberia funcionar".
- "Probablemente compila".
- "Queda integrado conceptualmente".
- "Falta probar".
- "Se dejo como placeholder".
- "Se puede completar despues".

Si falta probar, conectar a UI, integrar al renderer, manejar errores o resolver placeholders criticos, no esta terminado.

## Editor Tipo Unity

La interfaz debe parecerse a Unity Editor en estructura y flujo:

```text
File Edit Assets GameObject Component Window Tools Build Help
Toolbar: Hand Move Rotate Scale | Local/Global | Play Pause Step
Hierarchy | Scene View / Game View | Inspector
Project / Assets / Packages | Console / Profiler / Network
```

Debe incluir:

- Menu superior.
- Toolbar superior.
- Scene View.
- Game View.
- Hierarchy.
- Inspector.
- Project Browser.
- Console.
- Profiler basico.
- Network Debugger.
- Asset Import panel.
- Terrain panel.
- Lighting/Bake panel.
- Physics debug panel.
- Navigation debug panel.
- Server/session panel para multiplayer.

Prohibido:

- Dear ImGui.
- ImGuizmo.
- Depender de ImGui para cualquier parte.

## Librerias Objetivo

Editor UI:

- Qt Widgets.
- Qt Advanced Docking System / ADS.
- KProperty opcional o property grid propio con QTreeView/QAbstractItemModel.

Gizmos y debug:

- tinygizmo.
- Im3d.

Assets:

- stb_image.
- tinygltf.
- Assimp opcional.
- KTX2 / Basis Universal.
- MikkTSpace.
- meshoptimizer.

Terreno:

- FastNoise2.
- HighMap solo si licencia es aceptable.

Fisica:

- Jolt Physics.

Navegacion IA:

- Recast & Detour.

Baking / iluminacion precalculada:

- xatlas.
- Embree.
- Open Image Denoise.

Postprocessing / optimizacion visual:

- AMD FidelityFX SDK cuando aplique.
- CACAO.
- CAS.
- FSR.
- SSSR si el renderer lo soporta.

UI runtime:

- RmlUi.

Rendering / Vulkan:

- Vulkan si el proyecto usa Vulkan.
- Vulkan Memory Allocator / VMA.
- Debug markers amigables con RenderDoc.

Networking / server security:

- OpenSSL para TLS/HTTPS/WSS/control plane seguro.
- Autenticacion, tokens de sesion, validacion de sesiones.
- Rate limiting, replay protection, logs de seguridad.
- Separacion servidor dedicado/cliente.

## Arquitectura General

Estructura objetivo:

```text
/engine
  /core /math /memory /jobs /renderer /assets /scene /terrain /physics
  /navigation /animation /audio /ui /gizmos /debug /serialization
  /network /security /runtime

/editor
  /app /qt /panels /viewport /inspectors /commands /tools /themes
  /project_browser /console /profiler /network_debugger

/server
  /dedicated_server /networking /security /sessions /simulation
  /matchmaking_placeholder /persistence_placeholder

/tools
  /asset_importer /lightmap_baker /shader_compiler /terrain_generator
  /navmesh_baker

/third_party
  /qtads /tinygizmo /im3d /jolt /recastnavigation /meshoptimizer
  /xatlas /mikkts /fastnoise2 /highmap /embree /oidn /rmlui
  /stb /tinygltf /assimp /vma /fidelityfx /openssl

/tests
  /core_tests /math_tests /scene_tests /renderer_tests /asset_tests
  /terrain_tests /physics_tests /navigation_tests /network_tests
  /security_tests

/examples
  /basic_scene /terrain_scene /physics_scene /navigation_scene
  /multiplayer_scene /offline_scene
```

## Modularizacion

Reglas:

- Editor puede usar Engine API, pero Engine no depende de Editor.
- Runtime no depende de Qt.
- Server no depende de Qt.
- Physics no depende del renderer.
- Navigation no depende de Qt.
- Assets no depende del viewport.
- Terrain no depende del editor.
- Renderer no contiene logica de gameplay.
- Networking no contiene logica visual.
- Security no se mezcla con UI.
- RmlUi runtime no se mezcla con Qt Editor UI.

Interfaces objetivo:

- `IRenderer`
- `IRenderDevice`
- `IAssetManager`
- `IPhysicsWorld`
- `INavigationWorld`
- `INetworkTransport`
- `ISecureChannel`
- `IEditorGizmoBackend`
- `IDebugDraw`
- `IGameUISystem`

## Optimizacion Obligatoria

La optimizacion debe estar considerada desde el diseno, no al final.

CPU:

- Evitar allocations innecesarias por frame.
- Usar object pools donde tenga sentido.
- Separar update por sistemas.
- Usar jobs/thread pool para tareas pesadas.
- No bloquear UI con imports, baking o terreno.
- Cachear resultados.
- Evitar busquedas lineales grandes en hot paths.
- Usar profiling basico.

GPU:

- VMA para memoria Vulkan.
- Reutilizar buffers.
- Batch rendering.
- Minimizar cambios de pipeline/material.
- Mipmaps.
- Texturas comprimidas KTX2/Basis cuando aplique.
- meshoptimizer para vertex/index buffers.
- LOD.
- Frustum culling.
- Terrain chunk culling.
- Instancing.
- RenderDoc debug markers.

Assets:

- Import cache.
- Formato interno optimizado.
- Tangents generados una vez.
- Meshes optimizados una vez.
- LODs offline/import time.
- Thumbnails cacheados.
- No reprocesar assets sin cambios.

Networking:

- Delta compression.
- Snapshot interpolation.
- Client prediction.
- Server reconciliation.
- Rate limiting.
- Paquetes binarios compactos.
- Sequence numbers.
- ACKs.
- Ping/latency/jitter/interpolation buffers.
- No confiar en datos del cliente.

Servidor:

- Tick rate configurable.
- Separar simulation tick de network send rate.
- Interest management.
- Spatial partitioning.
- Evitar broadcast global innecesario.
- Limitar entidades replicadas por jugador.
- Medir CPU por tick y ancho de banda por cliente.
- Logs de paquetes rechazados.
- Proteccion contra spam.

## Multiplayer En Tiempo Real

Modelo requerido:

- Server authoritative.
- Cliente no decide posiciones finales importantes.
- Cliente envia inputs.
- Servidor valida inputs.
- Servidor simula.
- Servidor envia snapshots.
- Cliente interpola/reconcilia.

Flujo:

```text
Client Input
  -> Serialize input command
  -> Send to server
  -> Server validates
  -> Server simulation tick
  -> Server snapshot
  -> Client receives snapshot
  -> Reconciliation
  -> Interpolation/render
```

Debe soportar:

- Offline sin servidor.
- Local singleplayer.
- Servidor dedicado.
- Cliente conectado a servidor.
- Host local opcional.
- Simulacion determinista donde sea razonable.
- Tick rate configurable.
- Network debug overlay/panel.

Componentes minimos:

- `NetworkIdentityComponent`
- `NetworkTransformComponent`
- `NetworkRigidbodyComponent`
- `NetworkPlayerComponent`

## Seguridad Del Servidor

Principios:

- Nunca confiar en el cliente.
- Servidor autoritativo.
- Validar paquetes, tamanos, rangos, frecuencia, estado, permisos, timestamps y protocolo.
- Rechazar paquetes desconocidos.
- Loggear comportamiento sospechoso.
- No crashear por datos malformados.
- No exponer informacion sensible.
- No hardcodear secretos.
- No imprimir tokens completos.
- No permitir comandos administrativos desde cliente normal.

Modulo objetivo:

```text
/engine/security
  CryptoUtils SecureRandom TokenValidator SessionToken CertificateConfig
  RateLimiter PacketValidator ReplayProtection SecurityLogger

/server/security
  ServerAuth SessionManager ClientTrustState AntiSpam BanListPlaceholder
  ServerRateLimits SecureHandshake
```

Requisitos:

- Session tokens con expiracion.
- Nonce/challenge para handshake.
- Sequence numbers por cliente.
- Replay protection.
- Packet size limits.
- Rate limit por IP/cliente.
- Timeout de conexiones.
- Kick por paquetes invalidos repetidos.
- Validar autoridad/propiedad para movimiento, spawn, stats, inventario y posicion.
- Validar version de protocolo y checksum/version de assets cuando aplique.

Prohibido:

- Aceptar posicion final enviada por cliente.
- Aceptar dano sin validacion.
- Aceptar spawn sin permisos.
- Aceptar comandos debug en produccion.
- Tokens/certificados hardcodeados.
- Logs con secretos completos.

## Offline Mode

Debe correr sin servidor:

- Runtime local.
- Escena local.
- Fisica local.
- Assets locales.
- UI local.
- Save/load local.
- Sin servicios externos obligatorios.
- Misma escena que online donde sea posible.

`GameMode`:

- Offline.
- Client.
- DedicatedServer.
- Host.

Abstracciones:

- `ISimulationWorld`
- `IInputSource`
- `IReplicationBridge`

## Renderer

Renderer modular. Si ya existe renderer propio, integrarlo mediante interfaces.

Debe soportar:

- Mesh rendering.
- Material system.
- Texturas.
- Normal maps.
- Directional/point/spot lights.
- Shadows basicas.
- Skybox.
- Grid editor.
- Gizmos.
- Debug draw.
- Terrain chunks.
- Post-processing.
- Render targets para Scene View y Game View.

Si usa Vulkan:

- VMA obligatorio.
- RAII wrappers.
- Validation layers en debug.
- Debug names.
- RenderDoc markers.
- No fugas GPU.
- No recrear recursos por frame sin razon.
- Resize/swapchain correcto.
- Separar Scene View y Game View.

## Asset Pipeline

Pipeline:

```text
Raw asset
  -> Importer
  -> Validation
  -> Processing
  -> Optimization
  -> Cache
  -> Runtime asset
```

Meshes:

```text
Import model
  -> Extract meshes/materials
  -> Generate normals if needed
  -> Generate tangents with MikkTSpace
  -> Optimize with meshoptimizer
  -> Generate LODs
  -> Save internal mesh asset
```

Texturas:

```text
Load PNG/JPG
  -> Validate
  -> Generate mipmaps
  -> Convert/compress if KTX2 available
  -> Save internal texture asset
```

Materiales:

```text
Read material
  -> Assign shader
  -> Assign textures
  -> Assign constants
  -> Save material asset
```

## Terrain

Crear `TerrainSystem` completo:

- `TerrainSystem`
- `TerrainComponent`
- `TerrainGenerator`
- `TerrainChunk`
- `TerrainLODSystem`
- `TerrainMaterial`
- `TerrainColliderBuilder`
- `TerrainBrushEditor`
- `TerrainImportSettings`
- `TerrainDebugDraw`

Debe soportar crear/importar/generar terreno, chunks, LOD, normales, tangents, capas, splat maps, collider Jolt, navmesh Recast, debug, regeneracion segura y save/load.

No marcar Terrain terminado si no se ve, no tiene chunks, no guarda/carga, no aparece en inspector, no tiene debug o no maneja errores.

## Fisica

Usar Jolt Physics.

Crear:

- `PhysicsSystem`
- `PhysicsWorld`
- `RigidbodyComponent`
- `ColliderComponent`
- `CharacterControllerComponent`
- `PhysicsMaterial`
- `TerrainCollider`
- `PhysicsDebugDraw`

Soportar cuerpos static/dynamic/kinematic, colliders box/sphere/capsule/mesh/terrain, raycast, triggers, layers, character controller y debug visualization.

## Navegacion IA

Usar Recast & Detour.

Crear:

- `NavigationSystem`
- `NavMeshBuilder`
- `NavMeshAsset`
- `NavMeshRuntime`
- `NavAgentComponent`
- `NavObstacleComponent`
- `NavMeshDebugDraw`
- `NavMeshBakeSettings`

No marcar Navigation terminada sin navmesh real, save/load, pathfinding, debug visual y conexion al editor.

## Gizmos Y Viewport

Usar tinygizmo:

- Move.
- Rotate.
- Scale.
- Local/global.
- Snap opcional.

Usar Im3d:

- Grid.
- Rays.
- AABB.
- Frustums.
- Colliders.
- Navmesh.
- Terrain chunks.
- Light radius.
- Paths.
- Network debug positions.

Controles:

- Q hand/view.
- W move.
- E rotate.
- R scale.
- F focus selected.
- Alt + mouse orbit.
- Middle mouse pan.
- Wheel zoom.
- Click selecciona.
- Shift multi-select opcional.

Picking real:

- Ray desde camara.
- Interseccion 3D.
- No bounds 2D falsos.
- No offsets magicos.
- Camara, labels, picking y gizmos usan la misma matriz/camara.

## Lighting, Baking, Postprocess, UI Runtime

Lighting runtime minimo:

- Directional light.
- Point light.
- Spot opcional.
- Shadows basicas.
- Normal maps.
- Fog.
- Skybox.

Baking:

- xatlas para UV2.
- Embree para ray tracing offline.
- OIDN para denoise.
- `LightmapBaker`, `AOBaker`, `BakeJob`, `BakeCache`, `BakeSettings`, `LightingPanel`.

Postprocess:

- CAS.
- CACAO.
- FSR.
- SSSR si buffers existen.

UI runtime:

- RmlUi.
- HUD.
- Menus.
- Buttons/text/images/tooltips/eventos/input.
- No mezclar RmlUi con Qt.

## Sistema De Escena

Entity-Component con:

- `TransformComponent`
- `MeshRendererComponent`
- `CameraComponent`
- `LightComponent`
- `TerrainComponent`
- `RigidbodyComponent`
- `ColliderComponent`
- `NavAgentComponent`
- `NetworkIdentityComponent`
- `NetworkTransformComponent`
- `ScriptComponent`
- `UIComponent`

Soportar crear, borrar, duplicar, parent/child, local/global transform, seleccion hierarchy/viewport, undo/redo basico, save/load, prefab opcional.

## Serializacion

Guardar:

- Project settings.
- Scenes/entities/components.
- Materials.
- Terrain.
- Physics.
- Navmesh settings.
- Lighting settings.
- Network settings.
- Editor layout.
- Input settings.

No guardar punteros crudos ni rutas absolutas innecesarias. Usar IDs estables.

Estructura:

```text
/Project
  /Assets
  /Scenes
  /Cache
  /Settings
  /Library
  /Build
  /Server
```

## Play Mode

Tipo Unity:

- Play.
- Pause.
- Step.
- Al iniciar Play, snapshot de escena.
- Al salir, restaurar escena editor.
- No destruir datos accidentalmente.

Debe funcionar para Offline, Client, Dedicated Server opcional y Host opcional.

## Build Modes

1. Editor: Qt, ADS, tools, panels, viewport.
2. Game Runtime: sin Qt, renderer, scene, physics, runtime UI, offline/online client.
3. Dedicated Server: sin renderer, sin Qt, sin audio, sin UI, headless.

## Dedicated Server

Debe incluir:

- Config file.
- Puerto.
- Tick rate.
- Max players.
- Logs.
- Session manager.
- Auth handshake.
- Packet validation.
- Rate limiting.
- Simulation world.
- Snapshot sender.
- Disconnect handling.
- Graceful shutdown.

No depende de editor, renderer ni ventana.

## Network Debugger

Panel del editor:

- Estado de conexion.
- Ping.
- Packet loss simulado/opcional.
- Bytes sent/received.
- Snapshots por segundo.
- Server tick.
- Client tick.
- Entity count replicated.
- Rejected packets.
- Security warnings.
- Rate limit events.

## Testing

Agregar tests reales donde corresponda:

- Core: IDs, serialization, math, transform hierarchy.
- Assets: import basico, cache, missing file handling.
- Terrain: heightmap, chunk count, LOD.
- Physics: raycast, rigidbody fall, terrain collision.
- Navigation: navmesh, path.
- Network: packet serialization, validation, sequence, replay, rate limit.
- Security: token expiration, invalid token, oversized/malformed packets, permissions.

No inventar resultados. Si no se ejecutan tests, decirlo.

## Logging

Categorias:

- Core.
- Renderer.
- Editor.
- Assets.
- Terrain.
- Physics.
- Navigation.
- UI.
- Network.
- Security.
- Server.
- Client.

Nunca imprimir secretos, tokens completos, private keys, passwords.

## Error Handling

Manejar:

- Asset faltante.
- Textura corrupta.
- Modelo invalido.
- Shader invalido.
- Fallo Vulkan.
- Fallo de red.
- Paquete invalido.
- Cliente desconectado.
- Certificado faltante.
- Config invalida.
- Terrain invalido.
- Navmesh invalido.

Siempre loggear error claro.

## Licencias

Antes de integrar cada libreria, revisar licencia.

Especial cuidado:

- HighMap si usa GPL.
- KDDockWidgets si se considera alternativa.
- Cualquier dependencia GPL.

Preferir MIT/BSD/Apache/LGPL. No meter librerias incompatibles sin avisar.

## Fases

Trabajar fase por fase. No saltar fases. No marcar fase completa sin su Definition of Done.

1. Fase 1: Base del proyecto.
2. Fase 2: Scene / ECS basico.
3. Fase 3: Viewport 3D.
4. Fase 4: Gizmos.
5. Fase 5: Debug Draw.
6. Fase 6: Asset Pipeline.
7. Fase 7: Materiales e iluminacion basica.
8. Fase 8: Terreno.
9. Fase 9: Fisica.
10. Fase 10: Navigation.
11. Fase 11: Networking base.
12. Fase 12: Seguridad server.
13. Fase 13: Multiplayer runtime.
14. Fase 14: Dedicated Server.
15. Fase 15: Offline/Online modes.
16. Fase 16: Baking.
17. Fase 17: FidelityFX/Postprocess.
18. Fase 18: RmlUi runtime UI.
19. Fase 19: Polish Unity-like.

Estado actual de continuidad:

- Fase 6 / Asset Pipeline + Vulkan imported-asset optimization esta PARCIAL /
  a media. No avanzar a Fase 7 como si estuviera terminada hasta resolver y
  documentar los bloqueos actuales de `docs/phase6.md` y
  `docs/renderer_status.md`.

## Regla De Continuidad

Cuando aparezca un bug dentro de una fase:

1. Identificar el bug.
2. Explicar causa probable.
3. Arreglarlo.
4. Recompilar.
5. Probar.
6. Confirmar resultado.
7. Continuar solo despues.

Si un cambio rompe algo anterior, arreglar la regresion antes de seguir.

## Regla Local Adicional

Ningun archivo de codigo debe pasar de 800 lineas. Si se acerca al limite, dividir responsabilidades antes de seguir.

## Correccion De Fidelidad Del Viewport

El viewport 3D principal no debe resolverse como un preview CPU de Qt.
Qt maneja paneles, docking, menus, input de editor e inspector; el render 3D
principal debe entrar por `IRenderer`, `ViewportRenderSurface` y una ruta GPU
Vulkan.

Reglas para assets importados:

- Ver el GLB/glTF fiel al original por defecto.
- No eliminar triangulos visibles, caras, texturas o materiales para ganar FPS.
- No modificar destructivamente el modelo fuente.
- No seleccionar LOD bajo automaticamente para degradar Scene View.
- `meshoptimizer`, tangents, mipmaps, cache, buffers y LODs opcionales deben
  optimizar datos internos sin destruir la apariencia por defecto.

Reglas para optimizacion del editor:

- Subir meshes a vertex/index buffers GPU y texturas a GPU.
- Cachear meshes, materiales y texturas; no recrearlos dentro del paint loop.
- No decodificar imagenes, convertir texturas ni recalcular mallas por frame.
- Sacar trabajo pesado del hilo UI.
- Usar VMA, mipmaps, culling y markers de RenderDoc al madurar la ruta Vulkan.

Si existe fallback CPU:

- Debe declararse `Status: PARCIAL`.
- No debe ser la solucion final de performance.
- No debe romper geometria ni texturas.

Antes de avanzar a otra fase, revisar cualquier logica destructiva en viewport,
assets, LOD automatico, limites fijos de triangulos, fallback de
textura/material y render CPU con `QPainter`. No avanzar mientras el asset
importado no se vea completo y con texturas por la ruta prevista.

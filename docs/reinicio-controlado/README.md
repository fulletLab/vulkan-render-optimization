# ProjectUnity - Reinicio controlado

Fecha: 2026-06-22

Estado: BORRADOR DE TRABAJO

## Proposito

Este documento existe para evitar seguir acumulando fixes sueltos sobre el
renderer, el editor y el sistema de assets. La meta no es borrar el proyecto ni
empezar desde cero a ciegas. La meta es crear una base nueva y controlada usando
lo aprendido, dejando el estado actual como referencia tecnica.

La direccion correcta es:

- Conservar el repo actual como laboratorio y fuente de piezas utiles.
- Crear una base limpia para el nucleo renderer/editor/assets.
- Portar solo lo que ya entendemos y podemos verificar.
- No mezclar optimizacion, UI, sombras, importacion y tooling en el mismo paso.
- Medir antes de optimizar.
- Compilar y validar cada fase antes de continuar.

## Problema actual

Ahora mismo el proyecto tiene muchas piezas funcionando, pero varias nacieron
como respuestas rapidas a problemas visibles:

- Sombras que desaparecen o se cortan segun la camara.
- Parametros de LOD, HLOD, terrain y shadows mezclados en menus densos.
- Debug UI tomando demasiadas responsabilidades.
- Renderer, viewport y editor creciendo al mismo tiempo.
- Optimizaciones hechas para escenas grandes sin una frontera limpia por fase.
- Riesgo de arreglar un sintoma y romper otro camino del viewport.

Eso no significa que el trabajo este mal. Significa que ya aprendimos suficiente
para dejar de improvisar sobre la base actual.

## Decision

No haremos un "borrar todo y empezar de cero". Haremos un reinicio controlado.

El proyecto actual queda vivo como referencia. La nueva base debe reconstruir lo
minimo necesario con reglas claras:

- El engine no depende de Qt.
- Qt es solo editor UI.
- Vulkan es el camino principal del viewport 3D.
- Los assets importados no se degradan para optimizar.
- Las sombras, el LOD, el culling y el batching se miden por separado.
- Cada feature entra con counters, tests o smoke minimo.
- Nada entra al nucleo si no compila en Release.

## Anclas tecnicas

Estas son las ideas guia que vienen de las notas Vulkan/Phase 6 y se deben
mantener visibles al trabajar:

- `Measure first`.
- `Preserve imported asset fidelity`.
- `Cull per pass, including shadows`.
- `Descriptor sets` y descriptor updates medidos, no adivinados.
- `VMA` y memoria device-local para recursos estaticos.
- `Command buffer recording and submission` como costo real de CPU.
- `VkPipelineCache` para evitar costo interactivo de pipelines.
- `GPU timestamp queries` para separar CPU/GPU.
- `Qt is editor UI only`.

## Resultado minimo deseado

La primera base nueva no intenta tener todo. Debe tener exactamente esto:

- Ventana editor estable.
- Scene View Vulkan estable.
- Camara editor limpia.
- Un asset GLB importado y renderizado con transform correcto.
- Luz direccional.
- Sombras direccionales estables.
- Grid/editor aids sin pelear con el swapchain.
- Panel de debug pequeno con solo counters esenciales.
- Build Release funcionando.
- Tests/smoke basicos documentados.

Hasta que eso no este solido, no se agregan terrain, HLOD, menus grandes,
material graph, ray tracing, postprocess avanzado ni nuevas optimizaciones.

## Fase 0 - Congelar el estado actual

Objetivo:

Dejar el repo actual como punto de referencia antes de mover direccion.

Tareas:

- Crear una rama o tag de referencia del estado actual.
- Guardar lista de bugs conocidos: sombras, cortes, UI densa, LOD/HLOD.
- Guardar lista de piezas que si queremos rescatar.
- Anotar comandos de build Release que funcionan.
- No seguir agregando features grandes en la base vieja.

Criterio de salida:

- Existe una referencia clara del estado actual.
- Sabemos que piezas se portan y cuales se dejan atras.

## Fase 1 - Base editor/runtime limpia

Objetivo:

Crear la base minima sin arrastrar todos los paneles y fixes viejos.

Modulos esperados:

- `engine/core`: logs, ids, utilidades.
- `engine/math`: vectores, matrices, bounds.
- `engine/scene`: entidades, transforms, componentes simples.
- `engine/assets`: modelo importado y referencias de recursos.
- `engine/renderer`: interfaz renderer y backend Vulkan.
- `editor/app`: shell Qt.
- `editor/viewport`: puente editor hacia renderer, sin logica pesada.

Reglas:

- `engine/*` no incluye Qt.
- `editor/*` puede depender de engine.
- El renderer no debe leer widgets.
- El viewport no debe decidir politica profunda de assets.

Criterio de salida:

- Abre el editor.
- Renderiza un frame vacio Vulkan.
- Compila Debug y Release.

## Fase 2 - Camara, escena y asset minimo

Objetivo:

Volver a renderizar una escena real sin meter optimizaciones todavia.

Tareas:

- Importar un GLB simple.
- Mantener transforms de nodos.
- Mantener indices, vertices, UVs, normales y materiales basicos.
- Crear `MeshRendererComponent` con asset estable.
- Scene View usa una sola camara coherente para dibujar, picking y debug.

No hacer:

- No LOD.
- No HLOD.
- No batching complejo.
- No terrain.

Criterio de salida:

- Un modelo se ve correctamente.
- El asset no pierde geometria ni materiales basicos.
- La camara no cambia el resultado visual de forma inesperada.

## Fase 3 - Sombras estables antes de optimizar

Objetivo:

Resolver sombras desde arquitectura, no desde sliders magicos.

Tareas:

- Definir un sistema de luces minimo: direccional primero.
- Calcular shadow view desde bounds visibles y parametros estables.
- Separar culling de camara y culling de shadow pass.
- Exponer counters:
  - shadow candidates
  - shadow submitted
  - shadow culled
  - shadow triangles
  - shadow CPU ms
  - shadow GPU ms
- Crear test de interseccion/frustum para casters cerca del borde.

Reglas:

- Una sombra no debe desaparecer solo por girar la camara si el caster/receptor
  siguen siendo relevantes.
- Los cascades deben tener margen conservador.
- Cualquier culling de sombras debe poder explicarse con counters.

Criterio de salida:

- Sombras estables al orbitar la camara.
- No se cortan sombras obvias en el borde del frustum.
- Counters muestran por que un caster entra o sale.

## Fase 4 - Menu de debug reducido

Objetivo:

Evitar que Optimization Studio se convierta en un panel que controla todo sin
estructura.

Secciones maximas al inicio:

- Frame:
  - FPS
  - draws
  - triangles
  - CPU frame ms
  - GPU frame ms
- Shadows:
  - resolution
  - focus radius
  - candidates
  - submitted
  - culled
  - CPU/GPU shadow ms
- Assets:
  - resident meshes
  - resident textures
  - upload bytes
- Debug:
  - bounds
  - shadow casters
  - culling volumes

Reglas UI:

- Pocos controles, claros y con nombres de runtime.
- Nada de sliders duplicados que toquen el mismo comportamiento.
- Si un valor puede causar cortes, el panel debe decirlo.
- La UI no decide politica; solo modifica settings publicos.

Criterio de salida:

- El menu cabe sin recortes.
- Los parametros de sombras tienen sentido.
- El usuario puede diagnosticar sin tocar diez opciones a ciegas.

## Fase 5 - Performance por capas

Objetivo:

Optimizar solo cuando la base visual ya esta estable.

Orden correcto:

1. Medir escena base.
2. Reducir uploads repetidos.
3. Cachear buffers/texturas/materiales.
4. Ordenar draws opacos por estado compatible.
5. Instancing/batching para casos repetidos.
6. Runtime LOD no destructivo.
7. Shadow pass culling medido.
8. Descriptor/pipeline cache cleanup.
9. GPU-driven/indirect solo despues de medir el camino directo.

Criterio de salida:

- Cada optimizacion tiene counter antes/despues.
- El asset se ve igual de cerca.
- Release build pasa.

## Piezas que probablemente se portan

- Importacion GLB/glTF que preserva transforms.
- Preservacion de materiales basicos y texturas.
- Uso de VMA/staging para recursos GPU.
- RendererStats y counters de shadow/render.
- Shadow setup separado en engine/renderer.
- Runtime LOD no destructivo, pero mas tarde.
- Tests utiles de asset fidelity, renderer y shadow frustum.
- Build presets que ya funcionan.

## Piezas que se deben redisenar

- Optimization Studio demasiado cargado.
- Flujo donde sliders arreglan problemas estructurales.
- Sombras acopladas demasiado al estado de camara visible.
- Mezcla de tuning, debug y politica de renderer.
- Cualquier workaround que reduzca fidelidad del asset.
- Features grandes sin counters ni smoke minimo.

## Checklist de arranque

- [ ] Crear rama/tag de referencia del estado actual.
- [ ] Crear rama nueva para la base limpia.
- [ ] Documentar comandos de build Debug/Release.
- [ ] Elegir un solo GLB de prueba inicial.
- [ ] Definir el set minimo de components.
- [ ] Definir `RendererStats` minimo.
- [ ] Implementar frame Vulkan vacio.
- [ ] Renderizar asset simple.
- [ ] Agregar luz direccional.
- [ ] Agregar sombras estables.
- [ ] Agregar menu debug reducido.
- [ ] Compilar Release.
- [ ] Solo entonces decidir la siguiente feature.

## Regla final

Cada fase debe terminar con una frase clara:

`Estado: COMPLETO` si compila, esta integrado, probado y no tiene bloqueos
conocidos.

`Estado: PARCIAL` si funciona en parte, pero falta validacion, tests o hay bugs
conocidos.

No se debe vender una fase como terminada solo porque se ve bien una vez.

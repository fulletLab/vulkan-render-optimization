# Industrial Arena

Mapa 3D low-poly para ProjectUnity, pensado para shooter top-down 1v1, 2v2 y 5v5.

## Carga

Abre `Project/Assets/Maps/industrial_arena/industrial_arena.scene.json` desde Archivo > Abrir escena. El modelo visual principal es `industrial_arena.glb` con asset id `10890521447512886328` y queda cocinado en `Cache/Assets` como `.ffult`.

## Archivos

- `industrial_arena.scene.json`: escena cargable con luz, camara top-down, visual mesh, spawns y colliders.
- `industrial_arena.glb`: geometria visual modular del mapa completo.
- `industrial_arena_metadata.json`: spawns, cover points, obstaculos y grupos de colision para gameplay.
- `props/*.glb`: props modulares reutilizables para editar o rearmar variantes.
- `materials/industrial_palette.json`: colores PBR simples usados por los GLB.

## Edicion

Los spawns se editan en `industrial_arena_metadata.json` y en las entidades `Spawn A*` / `Spawn B*` de la escena. Los obstaculos bloqueantes tienen entidades `Collider *`; cambia `transform.position` y `collider.size` manteniendo pasillos de 3 a 5 unidades.

## Optimizacion

La escena usa un GLB visual unico, materiales simples sin texturas externas, props low-poly reutilizados y colliders de caja. Las decoraciones pequeñas no tienen colision y las coberturas usan geometria/colliders simples para mantener bajo el coste de render y fisica.

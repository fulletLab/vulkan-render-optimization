# ProjectUnity Scripting Runtime

Estado: PARCIAL para hot reload nativo.

El runtime actual implementa una arquitectura real de `ScriptComponent` sin depender de Qt:

- `engine/scripting` contiene `ScriptRegistry`, `ScriptRuntime`, `ScriptInstance`, `ScriptContext` e `InputState`.
- `ScriptComponent` serializa `scriptName`, `scriptAsset`, `enabled` y campos editables por nombre.
- Cada `ScriptDescriptor` declara su `assetPath` y metadata de fields. El Inspector no debe inferir fields leyendo C++.
- `ScriptRegistry::createComponentFromAsset` resuelve el asset a una clase registrada y aplica sus defaults.
- Un asset `.cpp` sin clase registrada devuelve: `Script asset found but class is not registered.`
- Play Mode cocina una escena runtime independiente y ejecuta scripts sobre ese snapshot.
- El viewport de Game/Play solo traduce teclado y mouse a `InputState`; no contiene lógica de gameplay.
- `FlyPlayerController` vive como clase registrada en `engine/scripting` y como asset editable en `Project/Assets/Scripts/FlyPlayerController.cpp`.
- `Health` sirve como segundo descriptor con fields distintos para verificar metadata dinámica.

## Cambios de código actuales

Los archivos de `Project/Assets/Scripts/*.cpp` todavía no se compilan dinámicamente. Son assets de proyecto vinculados por metadata a clases registradas.

Cambiar solamente valores serializados en Inspector no requiere recompilar.

Cambiar el código C++ de un script sí requiere actualmente:

1. Actualizar o añadir su implementación/registro en `engine/scripting/src`.
2. Recompilar el target `projectunity_scripting`.
3. Relinkear/recompilar el ejecutable `projectunity_editor`, porque `projectunity_scripting` es una librería estática.

La ruta prevista para evitar el relink del editor es convertir los scripts del proyecto en un módulo DLL/plugin separado, registrar descriptores al cargarlo y reinstanciar las instancias del snapshot de Play tras recargarlo.

Pendiente:

- Compilar scripts C++ de proyecto como DLL/plugin separado.
- Cargar/descargar DLL de scripts.
- Reinstanciar scripts tras hot reload preservando campos serializados cuando sea posible.
- Exponer una API de scripting más amplia para física, búsqueda de entidades y creación dinámica de componentes.

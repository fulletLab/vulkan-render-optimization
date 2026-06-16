# ProjectUnity C++ Scripting Runtime

Estado: C++ project scripts module separado.

ProjectUnity ejecuta gameplay C++ desde un modulo DLL del proyecto, no desde el editor ni desde `engine/scripting/src/BuiltInScripts.cpp`.

## Arquitectura

- `engine/scripting` define `ScriptModule`, `ScriptModuleLoader`, `ScriptRegistry`/`NativeScriptRegistry`, `ScriptInstance`/`IScriptInstance`, `ScriptContext`, `ScriptFieldMetadata`, `ScriptFactory` e `InputState`.
- `Project/Assets/Scripts/*.cpp` contiene scripts C++ reales del proyecto.
- `ProjectUnityGameScripts` compila esos `.cpp` a `Project/Binaries/Scripts/ProjectUnityGameScripts.dll`.
- La DLL exporta `registerProjectScripts(ProjectUnity::Scripting::ScriptRegistry& registry)`.
- El editor carga el modulo, llama `registerProjectScripts`, reconstruye el registry y usa esa metadata para Add Component e Inspector.
- `BuiltInScripts.cpp` queda solo como fallback/debug sin gameplay del usuario.

## Flujo de trabajo

Para cambiar gameplay C++:

1. Editar un archivo en `Project/Assets/Scripts`, por ejemplo `FlyPlayerController.cpp`.
2. Ejecutar `Build Scripts Module` desde el editor, o compilar el target CMake `ProjectUnityGameScripts`.
3. Ejecutar `Reload Scripts`.
4. Entrar a Play.

No hace falta recompilar `projectunity_editor.exe` para cambios de scripts.

## Reload

`ScriptModuleLoader` evita bloquear la DLL original en Windows:

1. Play se detiene antes de recargar.
2. `ScriptRuntime` destruye instancias activas.
3. El registry se limpia antes de descargar el modulo anterior.
4. `ProjectUnityGameScripts.dll` se copia a `ProjectScripts_runtime_###.dll`.
5. El editor carga la copia versionada con `LoadLibrary`.
6. Se llama `registerProjectScripts`.
7. El registry queda reconstruido para Add Component, Inspector y el siguiente Play.

## Metadata e Inspector

Cada script registrado declara:

- `className`
- `assetPath`
- fields editables y defaults
- factory de instancia
- callbacks de lifecycle: `onAttach`, `onCreate`, `onStart`, `onUpdate`, `onFixedUpdate`, `onDestroy`

`FlyPlayerController` declara `speed`, `sprintSpeed`, `gravity`, `jumpForce` y `mouseSensitivity`.

Si existe un `.cpp` en `Assets/Scripts` pero la clase no esta registrada en la DLL cargada, el editor muestra:

`Script asset exists but native class is not loaded. Build/Reload Project Scripts.`

## Play

Play Mode cocina una escena runtime independiente y ejecuta scripts sobre ese snapshot.

- Scene View Camera es la camara del editor.
- Play Runtime Camera es una entidad real con `CameraComponent`.
- El viewport de Play solo traduce teclado/mouse a `InputState`.
- `Player` solo recibe movimiento de scripts activos.
- Quitar `FlyPlayerController` o desactivar `Script Enabled` detiene el movimiento de gameplay.

El runtime registra diagnosticos temporales cuando un Transform cambia en Play:

- source system
- entity id
- old position
- new position
- scriptName, si aplica
- reason

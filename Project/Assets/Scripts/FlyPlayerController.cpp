// ProjectUnity C++ gameplay script template.
// Runtime binding: attach ScriptComponent name "FlyPlayerController" to an entity with a Camera.
// Controls in Game View: right mouse look, WASD move, Q/E down/up, Shift fast.

struct FlyPlayerController {
    float moveSpeed = 7.5f;
    float fastMultiplier = 3.0f;
    float lookSensitivity = 0.0035f;
};

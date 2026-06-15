// ProjectUnity gameplay script asset.
// This script is bound by name through Script: FlyPlayerController.
// Play mode runs it on a runtime scene snapshot, not on editor proxy entities.
// Status: PARCIAL hot reload. Runtime classes are registered in engine/scripting.
// Editing fields in Inspector changes serialized ScriptComponent values.

struct FlyPlayerController {
    float speed = 10.0f;
    float sprintSpeed = 20.0f;
    float gravity = 9.81f;
    float mouseSensitivity = 0.15f;
    float jumpForce = 5.0f;

    // Runtime API draft:
    // - Right mouse: look
    // - W/S: forward/back
    // - A/D: strafe
    // - Q/E: down/up
    // - Shift: fast move
    void onUpdate(auto& ctx) {
        ctx.lookWithMouse(mouseSensitivity);
        ctx.moveLocal({
            (ctx.keyDown("D") ? 1.0f : 0.0f) - (ctx.keyDown("A") ? 1.0f : 0.0f),
            (ctx.keyDown("E") ? 1.0f : 0.0f) - (ctx.keyDown("Q") ? 1.0f : 0.0f),
            (ctx.keyDown("W") ? 1.0f : 0.0f) - (ctx.keyDown("S") ? 1.0f : 0.0f),
        }, ctx.keyDown("Shift") ? sprintSpeed : speed);
    }
};

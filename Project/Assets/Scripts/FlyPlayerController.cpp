// ProjectUnity gameplay script asset.
// This script is bound by name through Script: FlyPlayerController.
// Play mode runs it on a runtime scene snapshot, not on editor proxy entities.

struct FlyPlayerController {
    float moveSpeed = 7.5f;
    float fastMultiplier = 3.0f;
    float lookSensitivity = 0.0035f;

    // Runtime API draft:
    // - Right mouse: look
    // - W/S: forward/back
    // - A/D: strafe
    // - Q/E: down/up
    // - Shift: fast move
    void onUpdate(auto& ctx) {
        const float speed = moveSpeed * (ctx.keyDown("Shift") ? fastMultiplier : 1.0f);
        ctx.lookWithMouse(lookSensitivity);
        ctx.moveLocal({
            (ctx.keyDown("D") ? 1.0f : 0.0f) - (ctx.keyDown("A") ? 1.0f : 0.0f),
            (ctx.keyDown("E") ? 1.0f : 0.0f) - (ctx.keyDown("Q") ? 1.0f : 0.0f),
            (ctx.keyDown("W") ? 1.0f : 0.0f) - (ctx.keyDown("S") ? 1.0f : 0.0f),
        }, speed);
    }
};

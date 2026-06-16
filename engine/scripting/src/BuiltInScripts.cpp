#include <projectunity/scripting/ScriptRuntime.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace projectunity::scripting {
namespace {

constexpr float kMinPitch = -1.52F;
constexpr float kMaxPitch = 1.52F;
constexpr float kDegreesToRadians = 0.0174532925F;

[[nodiscard]] math::Vec3 normalizedOr(math::Vec3 value, math::Vec3 fallback) noexcept
{
    const auto length = value.length();
    return length > 0.00001F && std::isfinite(length) ? value / length : fallback;
}

[[nodiscard]] math::Vec3 forwardFromYawPitch(float yaw, float pitch) noexcept
{
    const auto cosPitch = std::cos(pitch);
    return normalizedOr({
        cosPitch * std::sin(yaw),
        std::sin(pitch),
        cosPitch * std::cos(yaw),
    }, {0.0F, 0.0F, 1.0F});
}

class FlyPlayerController final : public ScriptInstance {
public:
    void onStart(ScriptContext& context) override
    {
        const auto* camera = context.getCamera();
        if (camera == nullptr) {
            return;
        }
        const auto forward = normalizedOr(camera->direction, {0.0F, 0.0F, 1.0F});
        yaw_ = std::atan2(forward.x, forward.z);
        pitch_ = std::asin(std::clamp(forward.y, -1.0F, 1.0F));
    }

    void onUpdate(ScriptContext& context, float deltaTime) override
    {
        auto* transform = context.getTransform();
        auto* camera = context.getCamera();
        if (transform == nullptr || camera == nullptr) {
            return;
        }

        const auto sensitivity = std::max(0.0F, context.field("mouseSensitivity", 0.15F)) * kDegreesToRadians;
        if (context.input().mouseLook) {
            yaw_ += context.input().mouseDeltaX * sensitivity;
            pitch_ = std::clamp(pitch_ - context.input().mouseDeltaY * sensitivity, kMinPitch, kMaxPitch);
        }

        const auto forward = forwardFromYawPitch(yaw_, pitch_);
        const auto right = normalizedOr(math::cross({0.0F, 1.0F, 0.0F}, forward), {1.0F, 0.0F, 0.0F});
        const auto up = normalizedOr(math::cross(forward, right), {0.0F, 1.0F, 0.0F});
        const auto horizontalForward = normalizedOr({forward.x, 0.0F, forward.z}, {0.0F, 0.0F, 1.0F});
        math::Vec3 movement;
        if (context.input().keyDown(KeyCode::W)) { movement += horizontalForward; }
        if (context.input().keyDown(KeyCode::S)) { movement -= horizontalForward; }
        if (context.input().keyDown(KeyCode::D)) { movement += right; }
        if (context.input().keyDown(KeyCode::A)) { movement -= right; }
        if (context.input().keyDown(KeyCode::E)) { movement += math::Vec3 {0.0F, 1.0F, 0.0F}; }
        if (context.input().keyDown(KeyCode::Q)) { movement -= math::Vec3 {0.0F, 1.0F, 0.0F}; }

        if (movement.lengthSquared() > 0.00001F) {
            const auto speed = context.input().keyDown(KeyCode::LeftShift)
                ? context.field("sprintSpeed", 20.0F)
                : context.field("speed", 10.0F);
            transform->position += normalizedOr(movement, {}) * (std::max(0.0F, speed) * std::max(0.0F, deltaTime));
        }

        const auto spaceDown = context.input().keyDown(KeyCode::Space);
        if (spaceDown && !jumpHeld_) {
            verticalVelocity_ = std::max(verticalVelocity_, std::max(0.0F, context.field("jumpForce", 5.0F)));
        }
        jumpHeld_ = spaceDown;
        if (verticalVelocity_ > 0.0F) {
            const auto dt = std::max(0.0F, deltaTime);
            transform->position.y += verticalVelocity_ * dt;
            verticalVelocity_ = std::max(0.0F, verticalVelocity_ - std::max(0.0F, context.field("gravity", 9.81F)) * dt);
        }

        camera->direction = forward;
        camera->right = right;
        camera->up = up;
    }

private:
    float yaw_ {0.0F};
    float pitch_ {0.0F};
    float verticalVelocity_ {0.0F};
    bool jumpHeld_ {false};
};

class Health final : public ScriptInstance {
public:
    void onCreate(ScriptContext& context) override
    {
        currentHealth_ = std::clamp(
            context.field("currentHealth", 100.0F),
            0.0F,
            std::max(0.0F, context.field("maxHealth", 100.0F)));
    }

private:
    float currentHealth_ {100.0F};
};

} // namespace

void registerBuiltInScripts(ScriptRegistry& registry)
{
    ScriptDescriptor flyPlayer;
    flyPlayer.className = "FlyPlayerController";
    flyPlayer.assetPath = "Assets/Scripts/FlyPlayerController.cpp";
    flyPlayer.fields = {
        {"speed", 10.0F},
        {"sprintSpeed", 20.0F},
        {"gravity", 9.81F},
        {"mouseSensitivity", 0.15F},
        {"jumpForce", 5.0F},
    };
    flyPlayer.create = [] { return std::make_unique<FlyPlayerController>(); };
    (void)registry.registerScript(std::move(flyPlayer));

    ScriptDescriptor health;
    health.className = "Health";
    health.assetPath = "Assets/Scripts/Health.cpp";
    health.fields = {
        {"maxHealth", 100.0F},
        {"currentHealth", 100.0F},
        {"regenerationPerSecond", 0.0F},
    };
    health.create = [] { return std::make_unique<Health>(); };
    (void)registry.registerScript(std::move(health));
}

} // namespace projectunity::scripting

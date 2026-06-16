#include <projectunity/scripting/ScriptRuntime.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace projectunity_game_scripts {
namespace {

constexpr float kMinPitch = -1.52F;
constexpr float kMaxPitch = 1.52F;
constexpr float kDegreesToRadians = 0.0174532925F;

[[nodiscard]] projectunity::math::Vec3 normalizedOr(
    projectunity::math::Vec3 value,
    projectunity::math::Vec3 fallback) noexcept
{
    const auto length = value.length();
    return length > 0.00001F && std::isfinite(length) ? value / length : fallback;
}

[[nodiscard]] projectunity::math::Vec3 forwardFromYawPitch(float yaw, float pitch) noexcept
{
    const auto cosPitch = std::cos(pitch);
    return normalizedOr({
        cosPitch * std::sin(yaw),
        std::sin(pitch),
        cosPitch * std::cos(yaw),
    }, {0.0F, 0.0F, 1.0F});
}

class FlyPlayerController final : public projectunity::scripting::IScriptInstance {
public:
    void onStart(projectunity::scripting::ScriptContext& context) override
    {
        auto* transform = context.getTransform();
        const auto* camera = context.getCamera();

        if (transform != nullptr) {
            groundY_ = transform->position.y;
            grounded_ = true;
            verticalVelocity_ = 0.0F;
        }

        if (camera == nullptr) {
            return;
        }

        const auto forward = normalizedOr(camera->direction, {0.0F, 0.0F, 1.0F});
        yaw_ = std::atan2(forward.x, forward.z);
        pitch_ = std::asin(std::clamp(forward.y, -1.0F, 1.0F));
    }

    void onUpdate(projectunity::scripting::ScriptContext& context, float deltaTime) override
    {
        auto* transform = context.getTransform();
        auto* camera = context.getCamera();
        if (transform == nullptr || camera == nullptr) {
            return;
        }

        const auto dt = std::max(0.0F, deltaTime);

        if (context.input().keyPressed(projectunity::scripting::KeyCode::Tab)) {
            context.setMouseCaptured(!context.isMouseCaptured());
        }

        if (context.input().keyPressed(projectunity::scripting::KeyCode::Escape)) {
            context.setMouseCaptured(false);
        }

        const auto sensitivity =
            std::max(0.0F, context.field("mouseSensitivity", 0.15F)) * kDegreesToRadians;

        if (context.isMouseCaptured()) {
            yaw_ += context.input().mouseDeltaX * sensitivity;
            pitch_ = std::clamp(
                pitch_ - context.input().mouseDeltaY * sensitivity,
                kMinPitch,
                kMaxPitch
            );
        }

        const auto forward = forwardFromYawPitch(yaw_, pitch_);
        const auto right = normalizedOr(
            projectunity::math::cross({0.0F, 1.0F, 0.0F}, forward),
            {1.0F, 0.0F, 0.0F}
        );

        const auto up = normalizedOr(
            projectunity::math::cross(forward, right),
            {0.0F, 1.0F, 0.0F}
        );

        const auto horizontalForward = normalizedOr(
            {forward.x, 0.0F, forward.z},
            {0.0F, 0.0F, 1.0F}
        );

        projectunity::math::Vec3 movement;
        if (context.input().keyDown(projectunity::scripting::KeyCode::W)) {
            movement += horizontalForward;
        }
        if (context.input().keyDown(projectunity::scripting::KeyCode::S)) {
            movement -= horizontalForward;
        }
        if (context.input().keyDown(projectunity::scripting::KeyCode::D)) {
            movement += right;
        }
        if (context.input().keyDown(projectunity::scripting::KeyCode::A)) {
            movement -= right;
        }

        if (movement.lengthSquared() > 0.00001F) {
            const auto speed = context.input().keyDown(projectunity::scripting::KeyCode::LeftShift)
                ? context.field("sprintSpeed", 20.0F)
                : context.field("speed", 10.0F);

            transform->position += normalizedOr(movement, {}) * (std::max(0.0F, speed) * dt);
        }

        if (context.input().keyPressed(projectunity::scripting::KeyCode::Space) && grounded_) {
            verticalVelocity_ = std::max(0.0F, context.field("jumpForce", 5.0F));
            grounded_ = false;
        }

        if (!grounded_) {
            const auto gravity = std::max(0.0F, context.field("gravity", 9.81F));

            verticalVelocity_ -= gravity * dt;
            transform->position.y += verticalVelocity_ * dt;

            if (transform->position.y <= groundY_) {
                transform->position.y = groundY_;
                verticalVelocity_ = 0.0F;
                grounded_ = true;
            }
        }

        camera->direction = forward;
        camera->right = right;
        camera->up = up;
    }

private:
    float yaw_ {0.0F};
    float pitch_ {0.0F};

    float groundY_ {0.0F};
    float verticalVelocity_ {0.0F};

    bool grounded_ {true};
};

} // namespace

void registerFlyPlayerController(projectunity::scripting::ScriptRegistry& registry)
{
    projectunity::scripting::ScriptDescriptor descriptor;
    descriptor.className = "FlyPlayerController";
    descriptor.assetPath = "Assets/Scripts/FlyPlayerController.cpp";
    descriptor.fields = {
        {"speed", 10.0F},
        {"sprintSpeed", 20.0F},
        {"gravity", 9.81F},
        {"jumpForce", 5.0F},
        {"mouseSensitivity", 0.15F},
    };
    descriptor.create = [] {
        return std::make_unique<FlyPlayerController>();
    };
    (void)registry.registerScript(std::move(descriptor));
}

} // namespace projectunity_game_scripts

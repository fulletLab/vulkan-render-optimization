#include <projectunity/editor/IEditorGizmoBackend.hpp>

#include <tiny-gizmo.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace projectunity::editor {
namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr float kRadiansToDegrees = 180.0F / kPi;
constexpr float kDegreesToRadians = kPi / 180.0F;

[[nodiscard]] minalg::float3 toTiny(math::Vec3 value)
{
    return {value.x, value.y, value.z};
}

[[nodiscard]] math::Vec3 fromTiny(minalg::float3 value)
{
    return {value.x, value.y, value.z};
}

[[nodiscard]] minalg::float4 normalizedQuaternion(minalg::float4 value)
{
    const auto length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w);
    if (length <= 0.00001F || !std::isfinite(length)) {
        return {0.0F, 0.0F, 0.0F, 1.0F};
    }
    return {value.x / length, value.y / length, value.z / length, value.w / length};
}

[[nodiscard]] minalg::float4 multiplyQuaternion(minalg::float4 lhs, minalg::float4 rhs)
{
    return normalizedQuaternion({
        lhs.w * rhs.x + lhs.x * rhs.w + lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.w * rhs.y - lhs.x * rhs.z + lhs.y * rhs.w + lhs.z * rhs.x,
        lhs.w * rhs.z + lhs.x * rhs.y - lhs.y * rhs.x + lhs.z * rhs.w,
        lhs.w * rhs.w - lhs.x * rhs.x - lhs.y * rhs.y - lhs.z * rhs.z,
    });
}

[[nodiscard]] minalg::float4 quaternionFromEulerDegrees(math::Vec3 euler)
{
    const auto halfX = euler.x * kDegreesToRadians * 0.5F;
    const auto halfY = euler.y * kDegreesToRadians * 0.5F;
    const auto halfZ = euler.z * kDegreesToRadians * 0.5F;
    const minalg::float4 x {std::sin(halfX), 0.0F, 0.0F, std::cos(halfX)};
    const minalg::float4 y {0.0F, std::sin(halfY), 0.0F, std::cos(halfY)};
    const minalg::float4 z {0.0F, 0.0F, std::sin(halfZ), std::cos(halfZ)};
    return multiplyQuaternion(z, multiplyQuaternion(y, x));
}

[[nodiscard]] math::Vec3 eulerDegreesFromQuaternion(minalg::float4 quaternion)
{
    const auto q = normalizedQuaternion(quaternion);
    const auto sinX = 2.0F * (q.w * q.x + q.y * q.z);
    const auto cosX = 1.0F - 2.0F * (q.x * q.x + q.y * q.y);
    const auto sinY = std::clamp(2.0F * (q.w * q.y - q.z * q.x), -1.0F, 1.0F);
    const auto sinZ = 2.0F * (q.w * q.z + q.x * q.y);
    const auto cosZ = 1.0F - 2.0F * (q.y * q.y + q.z * q.z);
    return {
        std::atan2(sinX, cosX) * kRadiansToDegrees,
        std::asin(sinY) * kRadiansToDegrees,
        std::atan2(sinZ, cosZ) * kRadiansToDegrees,
    };
}

[[nodiscard]] minalg::float4 quaternionFromBasis(math::Vec3 xAxis, math::Vec3 yAxis, math::Vec3 zAxis)
{
    const auto trace = xAxis.x + yAxis.y + zAxis.z;
    if (trace > 0.0F) {
        const auto scale = std::sqrt(trace + 1.0F) * 2.0F;
        return normalizedQuaternion({
            (yAxis.z - zAxis.y) / scale,
            (zAxis.x - xAxis.z) / scale,
            (xAxis.y - yAxis.x) / scale,
            0.25F * scale,
        });
    }

    if (xAxis.x > yAxis.y && xAxis.x > zAxis.z) {
        const auto scale = std::sqrt(1.0F + xAxis.x - yAxis.y - zAxis.z) * 2.0F;
        return normalizedQuaternion({
            0.25F * scale,
            (yAxis.x + xAxis.y) / scale,
            (zAxis.x + xAxis.z) / scale,
            (yAxis.z - zAxis.y) / scale,
        });
    }

    if (yAxis.y > zAxis.z) {
        const auto scale = std::sqrt(1.0F + yAxis.y - xAxis.x - zAxis.z) * 2.0F;
        return normalizedQuaternion({
            (yAxis.x + xAxis.y) / scale,
            0.25F * scale,
            (zAxis.y + yAxis.z) / scale,
            (zAxis.x - xAxis.z) / scale,
        });
    }

    const auto scale = std::sqrt(1.0F + zAxis.z - xAxis.x - yAxis.y) * 2.0F;
    return normalizedQuaternion({
        (zAxis.x + xAxis.z) / scale,
        (zAxis.y + yAxis.z) / scale,
        0.25F * scale,
        (xAxis.y - yAxis.x) / scale,
    });
}

class TinyGizmoBackend final : public IEditorGizmoBackend {
public:
    TinyGizmoBackend()
    {
        context_.render = [this](const tinygizmo::geometry_mesh& source) {
            mesh_.clear();
            mesh_.vertices.reserve(source.vertices.size());
            mesh_.triangles.reserve(source.triangles.size());
            for (const auto& vertex : source.vertices) {
                mesh_.vertices.push_back({
                    fromTiny(vertex.position),
                    {vertex.color.x, vertex.color.y, vertex.color.z, vertex.color.w},
                });
            }
            for (const auto& triangle : source.triangles) {
                mesh_.triangles.push_back({triangle.x, triangle.y, triangle.z});
            }
        };
    }

    [[nodiscard]] bool update(
        std::string_view name,
        const EditorGizmoFrame& frame,
        EditorGizmoTransform& transform) override
    {
        const auto toolNeedsSwitch = frame.tool != activeTool_;
        const auto spaceNeedsSwitch = frame.space != activeSpace_;
        const auto makeState = [&frame](bool toolPulse, bool spacePulse) {
            tinygizmo::gizmo_application_state state;
            state.mouse_left = frame.mouseLeft;
            state.hotkey_ctrl = toolPulse || spacePulse;
            state.hotkey_translate = toolPulse && frame.tool == EditorGizmoTool::Move;
            state.hotkey_rotate = toolPulse && frame.tool == EditorGizmoTool::Rotate;
            state.hotkey_scale = toolPulse && frame.tool == EditorGizmoTool::Scale;
            state.hotkey_local = spacePulse;
            state.screenspace_scale = 92.0F;
            state.viewport_size = {frame.viewportWidth, frame.viewportHeight};
            state.ray_origin = toTiny(frame.rayOrigin);
            state.ray_direction = toTiny(frame.rayDirection);
            state.cam.yfov = frame.verticalFovRadians;
            state.cam.near_clip = frame.nearPlane;
            state.cam.far_clip = frame.farPlane;
            state.cam.position = toTiny(frame.cameraPosition);
            state.cam.orientation = quaternionFromBasis(frame.cameraRight, frame.cameraUp, frame.cameraForward * -1.0F);
            return state;
        };

        tinygizmo::rigid_transform tinyTransform {
            quaternionFromEulerDegrees(transform.rotationEuler),
            toTiny(transform.position),
            toTiny(transform.scale),
        };
        const auto before = tinyTransform;
        const auto gizmoName = std::string(name);
        if (spaceNeedsSwitch && localPulseHigh_) {
            context_.update(makeState(false, false));
            (void)tinygizmo::transform_gizmo(gizmoName, context_, tinyTransform);
            context_.draw();
            localPulseHigh_ = false;
        }

        const auto state = makeState(toolNeedsSwitch, spaceNeedsSwitch);
        context_.update(state);
        const auto activated = tinygizmo::transform_gizmo(gizmoName, context_, tinyTransform);
        context_.draw();
        activeTool_ = frame.tool;
        activeSpace_ = frame.space;
        localPulseHigh_ = spaceNeedsSwitch;

        if (tinyTransform != before) {
            transform.position = fromTiny(tinyTransform.position);
            transform.rotationEuler = eulerDegreesFromQuaternion(tinyTransform.orientation);
            transform.scale = fromTiny(tinyTransform.scale);
        }
        return activated;
    }

    [[nodiscard]] const EditorGizmoMesh& mesh() const noexcept override
    {
        return mesh_;
    }

    void clear() override
    {
        mesh_.clear();
    }

private:
    tinygizmo::gizmo_context context_;
    EditorGizmoMesh mesh_;
    EditorGizmoTool activeTool_ {EditorGizmoTool::Move};
    EditorGizmoSpace activeSpace_ {EditorGizmoSpace::Local};
    bool localPulseHigh_ {false};
};

} // namespace

std::unique_ptr<IEditorGizmoBackend> createTinyGizmoBackend()
{
    return std::make_unique<TinyGizmoBackend>();
}

} // namespace projectunity::editor

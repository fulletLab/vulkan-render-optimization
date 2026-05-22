#pragma once

#include <projectunity/math/Vec3.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace projectunity::editor {

enum class EditorGizmoTool {
    Move,
    Rotate,
    Scale,
};

enum class EditorGizmoSpace {
    Local,
    Global,
};

struct EditorGizmoFrame {
    bool mouseLeft {false};
    bool toolChanged {false};
    bool spaceChanged {false};
    EditorGizmoTool tool {EditorGizmoTool::Move};
    EditorGizmoSpace space {EditorGizmoSpace::Local};
    float viewportWidth {1.0F};
    float viewportHeight {1.0F};
    float verticalFovRadians {1.04719755F};
    float nearPlane {0.05F};
    float farPlane {2000.0F};
    math::Vec3 rayOrigin;
    math::Vec3 rayDirection;
    math::Vec3 cameraPosition;
    math::Vec3 cameraRight {1.0F, 0.0F, 0.0F};
    math::Vec3 cameraUp {0.0F, 1.0F, 0.0F};
    math::Vec3 cameraForward {0.0F, 0.0F, 1.0F};
};

struct EditorGizmoTransform {
    math::Vec3 position;
    math::Vec3 rotationEuler;
    math::Vec3 scale {1.0F, 1.0F, 1.0F};
};

struct EditorGizmoVertex {
    math::Vec3 position;
    std::array<float, 4> color {1.0F, 1.0F, 1.0F, 1.0F};
};

struct EditorGizmoTriangle {
    std::uint32_t x {0};
    std::uint32_t y {0};
    std::uint32_t z {0};
};

struct EditorGizmoMesh {
    std::vector<EditorGizmoVertex> vertices;
    std::vector<EditorGizmoTriangle> triangles;

    void clear()
    {
        vertices.clear();
        triangles.clear();
    }
};

class IEditorGizmoBackend {
public:
    virtual ~IEditorGizmoBackend() = default;

    [[nodiscard]] virtual bool update(
        std::string_view name,
        const EditorGizmoFrame& frame,
        EditorGizmoTransform& transform) = 0;
    virtual void clear() = 0;
    [[nodiscard]] virtual const EditorGizmoMesh& mesh() const noexcept = 0;
};

[[nodiscard]] std::unique_ptr<IEditorGizmoBackend> createTinyGizmoBackend();

} // namespace projectunity::editor

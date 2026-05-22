#pragma once

#include <projectunity/debug/DebugDraw.hpp>
#include <projectunity/math/Vec3.hpp>

#include <array>
#include <memory>
#include <span>
#include <vector>

namespace projectunity::editor {

struct EditorDebugFrame {
    float viewportWidth {1.0F};
    float viewportHeight {1.0F};
    float verticalFovRadians {1.04719755F};
    float deltaSeconds {1.0F / 60.0F};
    math::Vec3 cursorRayOrigin;
    math::Vec3 cursorRayDirection {0.0F, 0.0F, 1.0F};
    math::Vec3 cameraPosition;
    math::Vec3 cameraForward {0.0F, 0.0F, 1.0F};
    math::Vec3 worldUp {0.0F, 1.0F, 0.0F};
};

struct EditorDebugVertex {
    math::Vec3 position;
    std::array<float, 4> color {1.0F, 1.0F, 1.0F, 1.0F};
    float size {1.0F};
};

struct EditorDebugLineSegment {
    EditorDebugVertex start;
    EditorDebugVertex end;
};

struct EditorDebugLineMesh {
    std::vector<EditorDebugLineSegment> lines;

    void clear()
    {
        lines.clear();
    }
};

class IEditorDebugDrawBackend {
public:
    virtual ~IEditorDebugDrawBackend() = default;

    [[nodiscard]] virtual bool update(
        const EditorDebugFrame& frame,
        std::span<const debug::DebugLine> lines) = 0;
    virtual void clear() = 0;
    [[nodiscard]] virtual const EditorDebugLineMesh& lineMesh() const noexcept = 0;
};

[[nodiscard]] std::unique_ptr<IEditorDebugDrawBackend> createIm3dDebugDrawBackend();

} // namespace projectunity::editor

#include <projectunity/editor/IEditorDebugDrawBackend.hpp>

#include <projectunity/core/Log.hpp>

#include <im3d.h>

#include <algorithm>
#include <cmath>

namespace projectunity::editor {
namespace {

[[nodiscard]] Im3d::Vec3 toIm3d(math::Vec3 value)
{
    return {value.x, value.y, value.z};
}

[[nodiscard]] math::Vec3 fromIm3d(const Im3d::Vec4& value)
{
    return {value.x, value.y, value.z};
}

[[nodiscard]] std::array<float, 4> fromIm3d(Im3d::Color color)
{
    return {color.getR(), color.getG(), color.getB(), color.getA()};
}

[[nodiscard]] Im3d::Color toIm3d(debug::DebugColor color)
{
    return {
        std::clamp(color.r, 0.0F, 1.0F),
        std::clamp(color.g, 0.0F, 1.0F),
        std::clamp(color.b, 0.0F, 1.0F),
        std::clamp(color.a, 0.0F, 1.0F),
    };
}

class Im3dDebugDrawBackend final : public IEditorDebugDrawBackend {
public:
    [[nodiscard]] bool update(
        const EditorDebugFrame& frame,
        std::span<const debug::DebugLine> lines) override
    {
        mesh_.clear();
        if (frame.viewportWidth <= 0.0F
            || frame.viewportHeight <= 0.0F
            || !std::isfinite(frame.verticalFovRadians)) {
            core::logWarning(core::LogCategory::Editor, "Im3d debug draw rejected an invalid editor frame");
            return false;
        }

        auto& appData = Im3d::GetAppData();
        appData.m_deltaTime = std::max(frame.deltaSeconds, 0.0F);
        appData.m_viewportSize = {frame.viewportWidth, frame.viewportHeight};
        appData.m_viewOrigin = toIm3d(frame.cameraPosition);
        appData.m_viewDirection = toIm3d(frame.cameraForward);
        appData.m_worldUp = toIm3d(frame.worldUp);
        appData.m_projOrtho = false;
        appData.m_projScaleY = std::tan(frame.verticalFovRadians * 0.5F) * 2.0F;
        appData.m_cursorRayOrigin = toIm3d(frame.cursorRayOrigin);
        appData.m_cursorRayDirection = toIm3d(frame.cursorRayDirection);
        Im3d::NewFrame();

        if (!lines.empty()) {
            Im3d::BeginLines();
            for (const auto& line : lines) {
                const auto color = toIm3d(line.color);
                Im3d::Vertex(toIm3d(line.start), line.thickness, color);
                Im3d::Vertex(toIm3d(line.end), line.thickness, color);
            }
            Im3d::End();
        }

        Im3d::EndFrame();
        appendDrawLists();
        return !mesh_.lines.empty();
    }

    [[nodiscard]] const EditorDebugLineMesh& lineMesh() const noexcept override
    {
        return mesh_;
    }

    void clear() override
    {
        mesh_.clear();
    }

private:
    void appendDrawLists()
    {
        const auto* drawLists = Im3d::GetDrawLists();
        const auto drawListCount = Im3d::GetDrawListCount();
        for (Im3d::U32 drawListIndex = 0; drawListIndex < drawListCount; ++drawListIndex) {
            const auto& drawList = drawLists[drawListIndex];
            if (drawList.m_primType != Im3d::DrawPrimitive_Lines || drawList.m_vertexData == nullptr) {
                continue;
            }

            mesh_.lines.reserve(mesh_.lines.size() + drawList.m_vertexCount / 2U);
            for (Im3d::U32 vertexIndex = 1; vertexIndex < drawList.m_vertexCount; vertexIndex += 2) {
                const auto& start = drawList.m_vertexData[vertexIndex - 1];
                const auto& end = drawList.m_vertexData[vertexIndex];
                mesh_.lines.push_back({
                    {
                        fromIm3d(start.m_positionSize),
                        fromIm3d(start.m_color),
                        start.m_positionSize.w,
                    },
                    {
                        fromIm3d(end.m_positionSize),
                        fromIm3d(end.m_color),
                        end.m_positionSize.w,
                    },
                });
            }
        }
    }

    EditorDebugLineMesh mesh_;
};

} // namespace

std::unique_ptr<IEditorDebugDrawBackend> createIm3dDebugDrawBackend()
{
    return std::make_unique<Im3dDebugDrawBackend>();
}

} // namespace projectunity::editor

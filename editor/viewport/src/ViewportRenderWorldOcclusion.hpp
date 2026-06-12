#pragma once

#include "ViewportRenderWorld.hpp"
#include "ViewportRendererCulling.hpp"

#include <array>

namespace projectunity::editor {

class ViewportOcclusionBuffer final {
public:
    ViewportOcclusionBuffer(const ViewportRenderWorldCamera& camera, int viewportHeight) noexcept;

    [[nodiscard]] bool addOccluder(const ViewportWorldBounds& bounds) noexcept;
    [[nodiscard]] bool isOccluded(const ViewportWorldBounds& bounds) const noexcept;
    [[nodiscard]] bool hasOccluders() const noexcept { return occluderCount_ > 0U; }
    [[nodiscard]] std::uint64_t occluderCount() const noexcept { return occluderCount_; }

private:
    struct ProjectedBounds {
        int minX {0};
        int maxX {0};
        int minY {0};
        int maxY {0};
        float nearDepth {0.0F};
        float farDepth {0.0F};
        float normalizedArea {0.0F};
    };

    [[nodiscard]] bool projectBounds(
        const ViewportWorldBounds& bounds,
        ProjectedBounds& projected,
        bool allowNearClippedBounds) const noexcept;
    [[nodiscard]] std::size_t cellIndex(int x, int y) const noexcept;

    static constexpr int kWidth = 96;
    static constexpr int kHeight = 54;
    static constexpr std::size_t kCellCount = static_cast<std::size_t>(kWidth * kHeight);

    ViewportRenderWorldCamera camera_;
    float projectionScaleX_ {0.0F};
    float projectionScaleY_ {0.0F};
    std::array<float, kCellCount> depth_;
    std::uint64_t occluderCount_ {0};
};

} // namespace projectunity::editor

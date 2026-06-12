#pragma once

#include "ViewportRenderWorld.hpp"
#include "ViewportRendererCulling.hpp"

#include <projectunity/assets/AssetManager.hpp>
#include <projectunity/renderer/RendererTypes.hpp>

#include <array>
#include <cstdint>
#include <memory>

namespace projectunity::editor {

enum class ViewportOcclusionBackend : std::uint8_t {
    Auto,
    Off,
    Coarse,
    MaskedOcclusionCulling,
};

enum class ViewportOcclusionQueryResult : std::uint8_t {
    None,
    NoOccluders,
    ProjectionRejected,
    CandidateTooLarge,
    InvalidDepth,
    UncoveredCell,
    DepthVisible,
    Occluded,
};

class ViewportOcclusionBuffer final {
public:
    ViewportOcclusionBuffer(
        const ViewportRenderWorldCamera& camera,
        int viewportHeight,
        ViewportOcclusionBackend requestedBackend = ViewportOcclusionBackend::Auto) noexcept;
    ~ViewportOcclusionBuffer();

    [[nodiscard]] bool addOccluder(const ViewportWorldBounds& bounds) noexcept;
    [[nodiscard]] bool addOccluderTriangles(
        const assets::MeshPrimitive& primitive,
        const renderer::RenderMatrix4& modelMatrix,
        bool disableBackfaceCulling) noexcept;
    [[nodiscard]] bool isOccluded(const ViewportWorldBounds& bounds) const noexcept;
    [[nodiscard]] bool hasOccluders() const noexcept;
    [[nodiscard]] std::uint64_t occluderCount() const noexcept { return occluderCount_; }
    [[nodiscard]] std::uint64_t occluderTriangleCount() const noexcept { return occluderTriangleCount_; }
    [[nodiscard]] ViewportOcclusionBackend backend() const noexcept { return backend_; }
    [[nodiscard]] ViewportOcclusionQueryResult lastQueryResult() const noexcept { return lastQueryResult_; }

private:
    struct MocState;

    struct ProjectedBounds {
        int minX {0};
        int maxX {0};
        int minY {0};
        int maxY {0};
        float ndcMinX {0.0F};
        float ndcMaxX {0.0F};
        float ndcMinY {0.0F};
        float ndcMaxY {0.0F};
        float nearDepth {0.0F};
        float farDepth {0.0F};
        float normalizedArea {0.0F};
        bool nearClipped {false};
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
    std::unique_ptr<MocState> moc_;
    ViewportOcclusionBackend backend_ {ViewportOcclusionBackend::Coarse};
    std::uint64_t occluderTriangleBudget_ {0};
    std::uint64_t occluderCount_ {0};
    std::uint64_t occluderTriangleCount_ {0};
    mutable ViewportOcclusionQueryResult lastQueryResult_ {ViewportOcclusionQueryResult::None};
};

} // namespace projectunity::editor

#include "ViewportRenderWorldOcclusion.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace projectunity::editor {
namespace {

constexpr float kMinimumOccluderArea = 0.018F;
constexpr float kMaximumCandidateArea = 0.72F;

[[nodiscard]] bool finitePositive(float value) noexcept
{
    return std::isfinite(value) && value > 0.0F;
}

} // namespace

ViewportOcclusionBuffer::ViewportOcclusionBuffer(const ViewportRenderWorldCamera& camera, int /*viewportHeight*/) noexcept
    : camera_(camera)
{
    depth_.fill(std::numeric_limits<float>::infinity());

    const auto tangent = std::tan(camera_.verticalFovRadians * 0.5F);
    if (finitePositive(tangent) && finitePositive(camera_.aspectRatio)) {
        projectionScaleY_ = 1.0F / tangent;
        projectionScaleX_ = projectionScaleY_ / camera_.aspectRatio;
    }
}

bool ViewportOcclusionBuffer::projectBounds(
    const ViewportWorldBounds& bounds,
    ProjectedBounds& projected,
    bool allowNearClippedBounds) const noexcept
{
    if (!finitePositive(projectionScaleX_) || !finitePositive(projectionScaleY_)) {
        return false;
    }

    auto minX = std::numeric_limits<float>::infinity();
    auto maxX = -std::numeric_limits<float>::infinity();
    auto minY = std::numeric_limits<float>::infinity();
    auto maxY = -std::numeric_limits<float>::infinity();
    auto nearDepth = std::numeric_limits<float>::infinity();
    auto farDepth = 0.0F;

    const auto clippedNearPlane = std::max(camera_.nearPlane + 0.001F, 0.001F);
    for (const auto& corner : bounds.corners) {
        const auto relative = corner - camera_.eye;
        auto depth = math::dot(relative, camera_.forward);
        if (!std::isfinite(depth) || depth <= camera_.nearPlane) {
            if (!allowNearClippedBounds) {
                return false;
            }
            depth = clippedNearPlane;
        }

        const auto viewX = math::dot(relative, camera_.right);
        const auto viewY = math::dot(relative, camera_.up);
        const auto ndcX = (viewX * projectionScaleX_) / depth;
        const auto ndcY = (viewY * projectionScaleY_) / depth;
        if (!std::isfinite(ndcX) || !std::isfinite(ndcY)) {
            return false;
        }
        minX = std::min(minX, ndcX);
        maxX = std::max(maxX, ndcX);
        minY = std::min(minY, ndcY);
        maxY = std::max(maxY, ndcY);
        nearDepth = std::min(nearDepth, depth);
        farDepth = std::max(farDepth, depth);
    }

    if (maxX <= -1.0F || minX >= 1.0F || maxY <= -1.0F || minY >= 1.0F) {
        return false;
    }

    const auto clampedMinX = std::clamp((minX + 1.0F) * 0.5F, 0.0F, 1.0F);
    const auto clampedMaxX = std::clamp((maxX + 1.0F) * 0.5F, 0.0F, 1.0F);
    const auto clampedMinY = std::clamp((1.0F - maxY) * 0.5F, 0.0F, 1.0F);
    const auto clampedMaxY = std::clamp((1.0F - minY) * 0.5F, 0.0F, 1.0F);
    if (clampedMaxX <= clampedMinX || clampedMaxY <= clampedMinY) {
        return false;
    }

    projected.minX = std::clamp(static_cast<int>(std::floor(clampedMinX * static_cast<float>(kWidth))), 0, kWidth - 1);
    projected.maxX = std::clamp(static_cast<int>(std::ceil(clampedMaxX * static_cast<float>(kWidth))) - 1, 0, kWidth - 1);
    projected.minY = std::clamp(static_cast<int>(std::floor(clampedMinY * static_cast<float>(kHeight))), 0, kHeight - 1);
    projected.maxY = std::clamp(static_cast<int>(std::ceil(clampedMaxY * static_cast<float>(kHeight))) - 1, 0, kHeight - 1);
    if (projected.maxX < projected.minX || projected.maxY < projected.minY) {
        return false;
    }

    projected.nearDepth = nearDepth;
    projected.farDepth = farDepth;
    projected.normalizedArea = (clampedMaxX - clampedMinX) * (clampedMaxY - clampedMinY);
    return std::isfinite(projected.nearDepth)
        && std::isfinite(projected.farDepth)
        && projected.nearDepth > 0.0F
        && projected.farDepth >= projected.nearDepth;
}

std::size_t ViewportOcclusionBuffer::cellIndex(int x, int y) const noexcept
{
    return static_cast<std::size_t>(y * kWidth + x);
}

bool ViewportOcclusionBuffer::addOccluder(const ViewportWorldBounds& bounds) noexcept
{
    ProjectedBounds projected;
    if (!projectBounds(bounds, projected, true) || projected.normalizedArea < kMinimumOccluderArea) {
        return false;
    }

    const auto width = projected.maxX - projected.minX + 1;
    const auto height = projected.maxY - projected.minY + 1;
    if (width < 4 || height < 4) {
        return false;
    }

    const auto shrinkX = std::max(1, width / 8);
    const auto shrinkY = std::max(1, height / 8);
    projected.minX += shrinkX;
    projected.maxX -= shrinkX;
    projected.minY += shrinkY;
    projected.maxY -= shrinkY;
    if (projected.maxX < projected.minX || projected.maxY < projected.minY) {
        return false;
    }

    const auto depthBias = std::max(0.25F, (projected.farDepth - projected.nearDepth) * 0.05F);
    const auto occluderDepth = projected.farDepth + depthBias;
    for (int y = projected.minY; y <= projected.maxY; ++y) {
        for (int x = projected.minX; x <= projected.maxX; ++x) {
            auto& cellDepth = depth_[cellIndex(x, y)];
            cellDepth = std::min(cellDepth, occluderDepth);
        }
    }
    ++occluderCount_;
    return true;
}

bool ViewportOcclusionBuffer::isOccluded(const ViewportWorldBounds& bounds) const noexcept
{
    if (!hasOccluders()) {
        return false;
    }

    ProjectedBounds projected;
    if (!projectBounds(bounds, projected, false) || projected.normalizedArea > kMaximumCandidateArea) {
        return false;
    }

    projected.minX = std::max(0, projected.minX - 1);
    projected.maxX = std::min(kWidth - 1, projected.maxX + 1);
    projected.minY = std::max(0, projected.minY - 1);
    projected.maxY = std::min(kHeight - 1, projected.maxY + 1);

    const auto depthBias = std::max(0.35F, bounds.radius * 0.08F);
    const auto requiredDepth = projected.nearDepth - depthBias;
    if (!std::isfinite(requiredDepth) || requiredDepth <= camera_.nearPlane) {
        return false;
    }

    for (int y = projected.minY; y <= projected.maxY; ++y) {
        for (int x = projected.minX; x <= projected.maxX; ++x) {
            if (depth_[cellIndex(x, y)] >= requiredDepth) {
                return false;
            }
        }
    }
    return true;
}

} // namespace projectunity::editor

#include "ViewportRenderWorldOcclusion.hpp"

#include <MaskedOcclusionCulling.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cmath>
#include <limits>
#include <new>
#include <string_view>

namespace projectunity::editor {
namespace {

constexpr float kMinimumOccluderArea = 0.018F;
constexpr float kMaximumCandidateArea = 0.12F;
constexpr float kMinimumCandidateDepth = 6.0F;
constexpr float kCandidateRadiusDepthScale = 3.0F;
constexpr int kDefaultMocHeight = 180;
constexpr std::uint64_t kDefaultMocOccluderTriangleBudget = 100'000ULL;

[[nodiscard]] bool finitePositive(float value) noexcept
{
    return std::isfinite(value) && value > 0.0F;
}

[[nodiscard]] bool envEquals(const char* value, std::string_view expected) noexcept
{
    if (value == nullptr) {
        return false;
    }
    return std::string_view(value) == expected;
}

[[nodiscard]] ViewportOcclusionBackend requestedViewportOcclusionBackend(ViewportOcclusionBackend requested) noexcept
{
    if (requested != ViewportOcclusionBackend::Auto) {
        return requested;
    }

    const auto* env = std::getenv("PROJECTUNITY_OCCLUSION_BACKEND");
    if (envEquals(env, "off") || envEquals(env, "none") || envEquals(env, "0")) {
        return ViewportOcclusionBackend::Off;
    }
    if (envEquals(env, "coarse") || envEquals(env, "grid")) {
        return ViewportOcclusionBackend::Coarse;
    }
    if (envEquals(env, "moc") || envEquals(env, "masked")) {
        return ViewportOcclusionBackend::MaskedOcclusionCulling;
    }
    return ViewportOcclusionBackend::Coarse;
}

[[nodiscard]] std::uint64_t envU64(const char* name, std::uint64_t fallback) noexcept
{
    const auto* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return fallback;
    }
    char* end = nullptr;
    const auto parsed = std::strtoull(value, &end, 10);
    return end != value ? static_cast<std::uint64_t>(parsed) : fallback;
}

[[nodiscard]] int alignUp(int value, int alignment) noexcept
{
    if (alignment <= 1) {
        return value;
    }
    return ((value + alignment - 1) / alignment) * alignment;
}

[[nodiscard]] float matrixAt(const renderer::RenderMatrix4& matrix, int row, int column) noexcept
{
    return matrix.values[static_cast<std::size_t>(column * 4 + row)];
}

[[nodiscard]] float& matrixAt(renderer::RenderMatrix4& matrix, int row, int column) noexcept
{
    return matrix.values[static_cast<std::size_t>(column * 4 + row)];
}

[[nodiscard]] renderer::RenderMatrix4 multiply(
    const renderer::RenderMatrix4& lhs,
    const renderer::RenderMatrix4& rhs) noexcept
{
    renderer::RenderMatrix4 result;
    result.values.fill(0.0F);
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            auto value = 0.0F;
            for (int index = 0; index < 4; ++index) {
                value += matrixAt(lhs, row, index) * matrixAt(rhs, index, column);
            }
            matrixAt(result, row, column) = value;
        }
    }
    return result;
}

[[nodiscard]] renderer::RenderMatrix4 mocViewClipMatrix(
    const ViewportRenderWorldCamera& camera,
    float projectionScaleX,
    float projectionScaleY) noexcept
{
    renderer::RenderMatrix4 matrix;
    matrix.values.fill(0.0F);

    matrixAt(matrix, 0, 0) = -camera.right.x * projectionScaleX;
    matrixAt(matrix, 0, 1) = -camera.right.y * projectionScaleX;
    matrixAt(matrix, 0, 2) = -camera.right.z * projectionScaleX;
    matrixAt(matrix, 0, 3) = math::dot(camera.eye, camera.right) * projectionScaleX;

    matrixAt(matrix, 1, 0) = camera.up.x * projectionScaleY;
    matrixAt(matrix, 1, 1) = camera.up.y * projectionScaleY;
    matrixAt(matrix, 1, 2) = camera.up.z * projectionScaleY;
    matrixAt(matrix, 1, 3) = -math::dot(camera.eye, camera.up) * projectionScaleY;

    matrixAt(matrix, 3, 0) = camera.forward.x;
    matrixAt(matrix, 3, 1) = camera.forward.y;
    matrixAt(matrix, 3, 2) = camera.forward.z;
    matrixAt(matrix, 3, 3) = -math::dot(camera.eye, camera.forward);
    return matrix;
}

} // namespace

struct ViewportOcclusionBuffer::MocState {
    MaskedOcclusionCulling* handle {nullptr};

    MocState() = default;
    MocState(const MocState&) = delete;
    MocState& operator=(const MocState&) = delete;

    ~MocState()
    {
        if (handle != nullptr) {
            MaskedOcclusionCulling::Destroy(handle);
        }
    }
};

ViewportOcclusionBuffer::ViewportOcclusionBuffer(
    const ViewportRenderWorldCamera& camera,
    int viewportHeight,
    ViewportOcclusionBackend requestedBackend) noexcept
    : camera_(camera)
{
    depth_.fill(std::numeric_limits<float>::infinity());

    const auto tangent = std::tan(camera_.verticalFovRadians * 0.5F);
    if (finitePositive(tangent) && finitePositive(camera_.aspectRatio)) {
        projectionScaleY_ = 1.0F / tangent;
        projectionScaleX_ = projectionScaleY_ / camera_.aspectRatio;
    }

    backend_ = requestedViewportOcclusionBackend(requestedBackend);
    occluderTriangleBudget_ = envU64("PROJECTUNITY_OCCLUSION_MOC_TRIANGLE_BUDGET", kDefaultMocOccluderTriangleBudget);

    if (backend_ == ViewportOcclusionBackend::MaskedOcclusionCulling
        && finitePositive(projectionScaleX_)
        && finitePositive(projectionScaleY_)) {
        const auto requestedHeight = static_cast<int>(std::min<std::uint64_t>(
            envU64("PROJECTUNITY_OCCLUSION_MOC_HEIGHT", kDefaultMocHeight),
            static_cast<std::uint64_t>(std::max(viewportHeight, 64))));
        const auto height = alignUp(std::max(requestedHeight, 64), 4);
        const auto width = alignUp(std::max(static_cast<int>(static_cast<float>(height) * camera_.aspectRatio), 64), 8);
        auto state = std::unique_ptr<MocState>(new (std::nothrow) MocState());
        if (state != nullptr) {
            state->handle = MaskedOcclusionCulling::Create(MaskedOcclusionCulling::AVX2);
            if (state->handle != nullptr) {
                state->handle->SetResolution(static_cast<unsigned int>(width), static_cast<unsigned int>(height));
                state->handle->SetNearClipPlane(std::max(camera_.nearPlane, 0.001F));
                state->handle->ClearBuffer();
                moc_ = std::move(state);
            }
        }
        if (moc_ == nullptr) {
            backend_ = ViewportOcclusionBackend::Coarse;
        }
    }
}

ViewportOcclusionBuffer::~ViewportOcclusionBuffer() = default;

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
            projected.nearClipped = true;
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

    projected.ndcMinX = std::clamp(minX, -1.0F, 1.0F);
    projected.ndcMaxX = std::clamp(maxX, -1.0F, 1.0F);
    projected.ndcMinY = std::clamp(minY, -1.0F, 1.0F);
    projected.ndcMaxY = std::clamp(maxY, -1.0F, 1.0F);

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

bool ViewportOcclusionBuffer::hasOccluders() const noexcept
{
    if (backend_ == ViewportOcclusionBackend::Off) {
        return false;
    }
    if (backend_ == ViewportOcclusionBackend::MaskedOcclusionCulling) {
        return moc_ != nullptr && occluderTriangleCount_ > 0U;
    }
    return occluderCount_ > 0U;
}

bool ViewportOcclusionBuffer::addOccluder(const ViewportWorldBounds& bounds) noexcept
{
    if (backend_ != ViewportOcclusionBackend::Coarse) {
        return false;
    }

    ProjectedBounds projected;
    if (!projectBounds(bounds, projected, true)
        || projected.nearClipped
        || projected.normalizedArea < kMinimumOccluderArea) {
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

bool ViewportOcclusionBuffer::addOccluderTriangles(
    const assets::MeshPrimitive& primitive,
    const renderer::RenderMatrix4& modelMatrix,
    bool disableBackfaceCulling) noexcept
{
    if (backend_ != ViewportOcclusionBackend::MaskedOcclusionCulling
        || moc_ == nullptr
        || moc_->handle == nullptr
        || primitive.vertices.empty()
        || primitive.indices.size() < 3U
        || occluderTriangleBudget_ == 0U
        || occluderTriangleCount_ >= occluderTriangleBudget_) {
        return false;
    }

    const auto availableTriangles = static_cast<std::uint64_t>(primitive.indices.size() / 3U);
    const auto remainingBudget = occluderTriangleBudget_ - occluderTriangleCount_;
    if (availableTriangles > remainingBudget) {
        return false;
    }
    const auto submittedTriangles = availableTriangles;
    if (submittedTriangles == 0U || submittedTriangles > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
        return false;
    }

    const auto modelToMocClip = multiply(mocViewClipMatrix(camera_, projectionScaleX_, projectionScaleY_), modelMatrix);
    const auto layout = MaskedOcclusionCulling::VertexLayout(
        static_cast<int>(sizeof(assets::MeshVertex)),
        static_cast<int>(offsetof(assets::MeshVertex, position) + offsetof(math::Vec3, y)),
        static_cast<int>(offsetof(assets::MeshVertex, position) + offsetof(math::Vec3, z)));
    const auto winding = disableBackfaceCulling
        ? MaskedOcclusionCulling::BACKFACE_NONE
        : MaskedOcclusionCulling::BACKFACE_CW;
    const auto result = moc_->handle->RenderTriangles(
        &primitive.vertices.front().position.x,
        primitive.indices.data(),
        static_cast<int>(submittedTriangles),
        modelToMocClip.values.data(),
        winding,
        MaskedOcclusionCulling::CLIP_PLANE_ALL,
        layout);
    if (result == MaskedOcclusionCulling::VIEW_CULLED) {
        return false;
    }

    occluderTriangleCount_ += submittedTriangles;
    ++occluderCount_;
    return true;
}

bool ViewportOcclusionBuffer::isOccluded(const ViewportWorldBounds& bounds) const noexcept
{
    if (!hasOccluders()) {
        lastQueryResult_ = ViewportOcclusionQueryResult::NoOccluders;
        return false;
    }

    ProjectedBounds projected;
    if (!projectBounds(bounds, projected, false)) {
        lastQueryResult_ = ViewportOcclusionQueryResult::ProjectionRejected;
        return false;
    }
    if (projected.normalizedArea > kMaximumCandidateArea) {
        lastQueryResult_ = ViewportOcclusionQueryResult::CandidateTooLarge;
        return false;
    }
    if (projected.nearDepth < std::max(kMinimumCandidateDepth, bounds.radius * kCandidateRadiusDepthScale)) {
        lastQueryResult_ = ViewportOcclusionQueryResult::CandidateTooLarge;
        return false;
    }

    const auto depthBias = std::max(0.35F, bounds.radius * 0.08F);
    const auto requiredDepth = projected.nearDepth - depthBias;
    if (!std::isfinite(requiredDepth) || requiredDepth <= camera_.nearPlane) {
        lastQueryResult_ = ViewportOcclusionQueryResult::InvalidDepth;
        return false;
    }

    if (backend_ == ViewportOcclusionBackend::MaskedOcclusionCulling) {
        if (moc_ == nullptr || moc_->handle == nullptr) {
            lastQueryResult_ = ViewportOcclusionQueryResult::NoOccluders;
            return false;
        }
        const auto result = moc_->handle->TestRect(
            projected.ndcMinX,
            projected.ndcMinY,
            projected.ndcMaxX,
            projected.ndcMaxY,
            requiredDepth);
        if (result == MaskedOcclusionCulling::OCCLUDED) {
            lastQueryResult_ = ViewportOcclusionQueryResult::Occluded;
            return true;
        }
        lastQueryResult_ = result == MaskedOcclusionCulling::VIEW_CULLED
            ? ViewportOcclusionQueryResult::ProjectionRejected
            : ViewportOcclusionQueryResult::DepthVisible;
        return false;
    }

    projected.minX = std::max(0, projected.minX - 1);
    projected.maxX = std::min(kWidth - 1, projected.maxX + 1);
    projected.minY = std::max(0, projected.minY - 1);
    projected.maxY = std::min(kHeight - 1, projected.maxY + 1);

    for (int y = projected.minY; y <= projected.maxY; ++y) {
        for (int x = projected.minX; x <= projected.maxX; ++x) {
            const auto cellDepth = depth_[cellIndex(x, y)];
            if (!std::isfinite(cellDepth)) {
                lastQueryResult_ = ViewportOcclusionQueryResult::UncoveredCell;
                return false;
            }
            if (cellDepth >= requiredDepth) {
                lastQueryResult_ = ViewportOcclusionQueryResult::DepthVisible;
                return false;
            }
        }
    }
    lastQueryResult_ = ViewportOcclusionQueryResult::Occluded;
    return true;
}

} // namespace projectunity::editor

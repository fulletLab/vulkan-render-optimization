#include "MeshPrimitiveClusters.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace projectunity::assets::detail {
namespace {

constexpr std::size_t kClusterMinInstances = 128;
constexpr std::size_t kClusterTargetInstances = 8;
constexpr std::size_t kClusterMaxGridSide = 96;
constexpr float kClusterTargetPhysicalExtent = 10.0F;

[[nodiscard]] float component(math::Vec3 value, int axis) noexcept
{
    if (axis == 0) {
        return value.x;
    }
    if (axis == 1) {
        return value.y;
    }
    return value.z;
}

void includeBounds(MeshBounds& bounds, const MeshBounds& next, bool& initialized) noexcept
{
    if (!initialized) {
        bounds = next;
        initialized = true;
        return;
    }
    bounds.minimum.x = std::min(bounds.minimum.x, next.minimum.x);
    bounds.minimum.y = std::min(bounds.minimum.y, next.minimum.y);
    bounds.minimum.z = std::min(bounds.minimum.z, next.minimum.z);
    bounds.maximum.x = std::max(bounds.maximum.x, next.maximum.x);
    bounds.maximum.y = std::max(bounds.maximum.y, next.maximum.y);
    bounds.maximum.z = std::max(bounds.maximum.z, next.maximum.z);
    bounds.center = (bounds.minimum + bounds.maximum) * 0.5F;
    bounds.radius = (bounds.maximum - bounds.center).length();
}

[[nodiscard]] std::array<int, 2> clusterAxes(const MeshBounds& bounds) noexcept
{
    std::array<std::pair<float, int>, 3> extents {{
        {std::fabs(bounds.maximum.x - bounds.minimum.x), 0},
        {std::fabs(bounds.maximum.y - bounds.minimum.y), 1},
        {std::fabs(bounds.maximum.z - bounds.minimum.z), 2},
    }};
    std::sort(extents.begin(), extents.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.first > rhs.first;
    });
    return {extents[0].second, extents[1].second};
}

[[nodiscard]] std::size_t gridSideForInstanceCount(std::size_t instanceCount) noexcept
{
    const auto wantedCells = std::max<std::size_t>(
        1U,
        (instanceCount + kClusterTargetInstances - 1U) / kClusterTargetInstances);
    return std::clamp(
        static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<float>(wantedCells)))),
        std::size_t {1U},
        kClusterMaxGridSide);
}

[[nodiscard]] std::size_t gridSideForPhysicalExtent(const MeshBounds& bounds, int axisA, int axisB) noexcept
{
    const auto extentA = std::fabs(component(bounds.maximum, axisA) - component(bounds.minimum, axisA));
    const auto extentB = std::fabs(component(bounds.maximum, axisB) - component(bounds.minimum, axisB));
    const auto largestExtent = std::max(extentA, extentB);
    if (!std::isfinite(largestExtent) || largestExtent <= kClusterTargetPhysicalExtent) {
        return 1U;
    }
    return std::clamp(
        static_cast<std::size_t>(std::ceil(largestExtent / kClusterTargetPhysicalExtent)),
        std::size_t {1U},
        kClusterMaxGridSide);
}

[[nodiscard]] std::size_t gridCoordinate(float value, float minimum, float extent, std::size_t side) noexcept
{
    if (side <= 1U || extent <= 0.0001F || !std::isfinite(value)) {
        return 0;
    }
    const auto normalized = std::clamp((value - minimum) / extent, 0.0F, 0.999999F);
    return std::min(static_cast<std::size_t>(normalized * static_cast<float>(side)), side - 1U);
}

} // namespace

void buildPrimitiveClusters(ModelAsset& model)
{
    model.primitiveClusters.clear();
    if (model.primitiveInstances.size() < kClusterMinInstances) {
        return;
    }

    MeshBounds sceneBounds;
    bool initialized = false;
    for (const auto& instance : model.primitiveInstances) {
        includeBounds(sceneBounds, instance.bounds, initialized);
    }
    if (!initialized) {
        return;
    }

    const auto axes = clusterAxes(sceneBounds);
    const auto side = std::clamp(
        std::max(
            gridSideForInstanceCount(model.primitiveInstances.size()),
            gridSideForPhysicalExtent(sceneBounds, axes[0], axes[1])),
        std::size_t {1U},
        kClusterMaxGridSide);
    const auto minA = component(sceneBounds.minimum, axes[0]);
    const auto minB = component(sceneBounds.minimum, axes[1]);
    const auto extentA = component(sceneBounds.maximum, axes[0]) - minA;
    const auto extentB = component(sceneBounds.maximum, axes[1]) - minB;
    if (side <= 1U || (std::fabs(extentA) <= 0.0001F && std::fabs(extentB) <= 0.0001F)) {
        return;
    }

    std::vector<MeshPrimitiveCluster> buckets(side * side);
    for (std::uint32_t index = 0; index < model.primitiveInstances.size(); ++index) {
        const auto& instance = model.primitiveInstances[index];
        const auto x = gridCoordinate(component(instance.bounds.center, axes[0]), minA, extentA, side);
        const auto y = gridCoordinate(component(instance.bounds.center, axes[1]), minB, extentB, side);
        auto& cluster = buckets[y * side + x];
        cluster.primitiveInstanceIndices.push_back(index);
        bool bucketInitialized = cluster.primitiveInstanceIndices.size() > 1U;
        includeBounds(cluster.bounds, instance.bounds, bucketInitialized);
    }

    model.primitiveClusters.reserve(buckets.size());
    for (auto& cluster : buckets) {
        if (!cluster.primitiveInstanceIndices.empty()) {
            model.primitiveClusters.push_back(std::move(cluster));
        }
    }
}

} // namespace projectunity::assets::detail

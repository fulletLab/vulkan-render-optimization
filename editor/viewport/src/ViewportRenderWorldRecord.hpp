#pragma once

#include "ViewportRendererCulling.hpp"

#include <projectunity/assets/AssetManager.hpp>
#include <projectunity/renderer/RendererTypes.hpp>
#include <projectunity/scene/Scene.hpp>

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <memory>
#include <vector>

namespace projectunity::editor {

struct ViewportFrameBounds {
    bool valid {false};
    math::Vec3 minimum;
    math::Vec3 maximum;

    void includeSphere(math::Vec3 center, float radius)
    {
        if (!std::isfinite(radius) || radius < 0.0F) {
            return;
        }
        const math::Vec3 extent {radius, radius, radius};
        if (!valid) {
            minimum = center - extent;
            maximum = center + extent;
            valid = true;
            return;
        }
        minimum.x = std::min(minimum.x, center.x - radius);
        minimum.y = std::min(minimum.y, center.y - radius);
        minimum.z = std::min(minimum.z, center.z - radius);
        maximum.x = std::max(maximum.x, center.x + radius);
        maximum.y = std::max(maximum.y, center.y + radius);
        maximum.z = std::max(maximum.z, center.z + radius);
    }

    [[nodiscard]] math::Vec3 center() const
    {
        return (minimum + maximum) * 0.5F;
    }

    [[nodiscard]] float radius() const
    {
        return (maximum - center()).length();
    }
};

struct ViewportRenderWorld::EntityRecord {
    struct Instance {
        std::uint64_t renderInstanceId {0};
        scene::EntityId sceneNodeId;
        assets::AssetId modelAssetId;
        std::shared_ptr<const assets::ModelAsset> model;
        std::uint32_t primitiveIndex {0};
        std::uint32_t primitiveInstanceIndex {UINT32_MAX};
        renderer::RenderMatrix4 modelMatrix;
        ViewportWorldBounds worldBounds;
        bool flipsWinding {false};
    };

    struct Chunk {
        std::uint64_t renderChunkId {0};
        scene::EntityId sceneNodeId;
        ViewportWorldBounds worldBounds;
        std::vector<std::size_t> instanceIndices;
        std::uint64_t triangleCount {0};
    };

    struct OverviewDraw {
        std::uint64_t renderInstanceId {0};
        assets::AssetId modelAssetId;
        std::uint32_t primitiveIndex {0};
        assets::MeshPrimitive primitive;
        ViewportWorldBounds worldBounds;
        std::uint64_t sourceTriangleCount {0};
    };

    scene::EntityId entityId;
    std::uint64_t signature {0};
    bool hasMeshSceneContent {false};
    bool overviewOnly {false};
    assets::AssetId coveredModelAssetId;
    std::vector<Instance> instances;
    std::vector<Chunk> chunks;
    std::vector<OverviewDraw> overviewDraws;
    ViewportWorldBounds worldBounds;
    bool worldBoundsValid {false};
    std::uint64_t sourceTriangleCount {0};
};

} // namespace projectunity::editor

#pragma once

#include <projectunity/assets/AssetManager.hpp>
#include <projectunity/math/Vec3.hpp>
#include <projectunity/scene/Scene.hpp>

#include <cstdint>
#include <optional>

namespace projectunity::editor {

struct PrimitiveProxyKey {
    std::uint64_t ownerEntityId {0};
    std::uint64_t modelAssetId {0};
    std::uint32_t primitiveInstanceIndex {0};
};

[[nodiscard]] inline std::uint64_t primitiveProxyKey(PrimitiveProxyKey key) noexcept
{
    auto seed = key.ownerEntityId ^ (key.modelAssetId + 0x9e3779b97f4a7c15ULL + (key.ownerEntityId << 6U) + (key.ownerEntityId >> 2U));
    return seed ^ (static_cast<std::uint64_t>(key.primitiveInstanceIndex) + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U));
}

[[nodiscard]] inline std::optional<std::uint32_t> primitiveInstanceIndexForProxy(
    const assets::ModelAsset& model,
    const scene::MeshRendererComponent& renderer) noexcept
{
    if (renderer.primitiveInstanceIndex.has_value()
        && *renderer.primitiveInstanceIndex < model.primitiveInstances.size()) {
        return *renderer.primitiveInstanceIndex;
    }
    if (!renderer.editorInstanceIndex.has_value()
        || *renderer.editorInstanceIndex >= model.editorInstances.size()) {
        return std::nullopt;
    }

    const auto editorIndex = *renderer.editorInstanceIndex;
    const auto& editorInstance = model.editorInstances[editorIndex];
    if (model.editorInstances.size() == model.primitiveInstances.size()
        && editorIndex < model.primitiveInstances.size()) {
        return editorIndex;
    }
    for (std::uint32_t index = 0; index < model.primitiveInstances.size(); ++index) {
        const auto& instance = model.primitiveInstances[index];
        if (instance.primitiveIndex == editorInstance.sourcePrimitiveIndex
            && math::nearlyEqual(instance.bounds.center, editorInstance.bounds.center, 0.001F)) {
            return index;
        }
    }
    return std::nullopt;
}

} // namespace projectunity::editor

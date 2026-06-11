#pragma once

#include <projectunity/assets/AssetManager.hpp>
#include <projectunity/math/Vec3.hpp>
#include <projectunity/scene/Scene.hpp>

#include <cstdint>
#include <optional>

namespace projectunity::editor {

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

#pragma once

#include <projectunity/math/Vec3.hpp>
#include <projectunity/scene/Scene.hpp>

#include <cstdint>
#include <optional>
#include <unordered_map>

namespace projectunity::editor {

class ViewportSceneEntityLookup final {
public:
    explicit ViewportSceneEntityLookup(const scene::Scene& scene)
    {
        entitiesById_.reserve(scene.entityCount());
        for (const auto& entity : scene.entities()) {
            entitiesById_.insert_or_assign(entity.id.value(), &entity);
        }
    }

    [[nodiscard]] const scene::Entity* find(scene::EntityId id) const noexcept
    {
        const auto it = entitiesById_.find(id.value());
        return it == entitiesById_.end() ? nullptr : it->second;
    }

    [[nodiscard]] std::optional<math::Vec3> worldPosition(scene::EntityId id) const noexcept
    {
        math::Vec3 position {};
        auto currentId = std::optional<scene::EntityId> {id};
        for (std::uint32_t depth = 0; currentId.has_value() && depth < 256U; ++depth) {
            const auto* entity = find(*currentId);
            if (entity == nullptr) {
                return std::nullopt;
            }
            position += entity->transform.position;
            currentId = entity->parent;
        }
        return currentId.has_value() ? std::nullopt : std::optional<math::Vec3> {position};
    }

    [[nodiscard]] std::optional<scene::EntityId> primitiveProxyOwnerEntityId(
        const scene::Entity& proxy) const noexcept
    {
        if (!proxy.meshRenderer.has_value() || proxy.meshRenderer->renderable) {
            return std::nullopt;
        }
        auto parentId = proxy.parent;
        for (std::uint32_t depth = 0; parentId.has_value() && depth < 64U; ++depth) {
            const auto* parent = find(*parentId);
            if (parent == nullptr) {
                return std::nullopt;
            }
            if (parent->meshRenderer.has_value()
                && parent->meshRenderer->renderable
                && parent->meshRenderer->modelAssetId == proxy.meshRenderer->modelAssetId) {
                return parent->id;
            }
            parentId = parent->parent;
        }
        return std::nullopt;
    }

private:
    std::unordered_map<std::uint64_t, const scene::Entity*> entitiesById_;
};

} // namespace projectunity::editor

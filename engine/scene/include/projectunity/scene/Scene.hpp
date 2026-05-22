#pragma once

#include <projectunity/core/StableId.hpp>
#include <projectunity/math/Vec3.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace projectunity::scene {

using EntityId = core::StableId;

struct TransformComponent {
    math::Vec3 position {0.0F, 0.0F, 0.0F};
    math::Vec3 rotationEuler {0.0F, 0.0F, 0.0F};
    math::Vec3 scale {1.0F, 1.0F, 1.0F};
};

struct MeshRendererComponent {
    core::StableId modelAssetId;
};

struct Entity {
    EntityId id;
    std::optional<EntityId> parent;
    std::vector<EntityId> children;
    std::string name;
    TransformComponent transform;
    std::optional<MeshRendererComponent> meshRenderer;
};

class Scene final {
public:
    Scene();

    [[nodiscard]] std::string_view name() const noexcept;
    void setName(std::string name);

    [[nodiscard]] Entity& createEntity(std::string name, std::optional<EntityId> parent = std::nullopt);
    [[nodiscard]] Entity* findEntity(EntityId id) noexcept;
    [[nodiscard]] const Entity* findEntity(EntityId id) const noexcept;
    [[nodiscard]] bool contains(EntityId id) const noexcept;

    [[nodiscard]] bool destroyEntity(EntityId id);
    [[nodiscard]] Entity* duplicateEntity(EntityId sourceId);

    [[nodiscard]] bool setParent(EntityId childId, std::optional<EntityId> parentId);
    [[nodiscard]] bool setName(EntityId id, std::string name);
    [[nodiscard]] bool setTransform(EntityId id, const TransformComponent& transform);
    [[nodiscard]] bool setMeshRenderer(EntityId id, std::optional<MeshRendererComponent> component);

    [[nodiscard]] std::vector<EntityId> rootEntities() const;
    [[nodiscard]] const std::vector<Entity>& entities() const noexcept;
    [[nodiscard]] std::size_t entityCount() const noexcept;

    void clear();

    [[nodiscard]] bool saveToFile(const std::filesystem::path& path, std::string* errorMessage = nullptr) const;
    [[nodiscard]] bool loadFromFile(const std::filesystem::path& path, std::string* errorMessage = nullptr);

    [[nodiscard]] std::string serialize(std::string* errorMessage = nullptr) const;
    [[nodiscard]] bool deserialize(std::string_view jsonText, std::string* errorMessage = nullptr);

private:
    [[nodiscard]] EntityId allocateId();
    [[nodiscard]] Entity* findEntityMutable(EntityId id) noexcept;
    [[nodiscard]] const Entity* findEntityInternal(EntityId id) const noexcept;
    [[nodiscard]] bool wouldCreateCycle(EntityId childId, EntityId candidateParentId) const noexcept;
    [[nodiscard]] bool removeChildReference(EntityId parentId, EntityId childId);
    [[nodiscard]] Entity* duplicateEntityRecursive(EntityId sourceId, std::optional<EntityId> parentOverride);
    void collectDescendants(EntityId id, std::vector<EntityId>& output) const;
    void rebuildNextId();

    std::string name_ {"Untitled Scene"};
    std::vector<Entity> entities_;
    core::StableIdGenerator idGenerator_;
};

} // namespace projectunity::scene

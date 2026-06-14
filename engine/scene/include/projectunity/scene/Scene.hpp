#pragma once

#include <projectunity/core/StableId.hpp>
#include <projectunity/math/Vec3.hpp>

#include <array>
#include <cstdint>
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

enum class RuntimePhysicsMode : std::uint8_t {
    None,
    StaticCollider,
    RigidBody,
};

struct RuntimeCookComponent {
    bool staticBatchable {true};
    bool mutableRuntime {false};
    RuntimePhysicsMode physics {RuntimePhysicsMode::None};
    bool grabbable {false};
};

struct MeshRendererComponent {
    core::StableId modelAssetId;
    std::optional<std::uint32_t> primitiveInstanceIndex;
    std::optional<std::uint32_t> editorInstanceIndex;
    bool renderable {true};
    RuntimeCookComponent runtimeCook;
};

enum class LightComponentType : std::uint8_t {
    Directional,
    Point,
    Spot,
};

struct LightComponent {
    LightComponentType type {LightComponentType::Directional};
    math::Vec3 direction {0.35F, -0.82F, 0.45F};
    std::array<float, 3> color {1.0F, 0.98F, 0.92F};
    float intensity {3.0F};
    float range {0.0F};
    float innerConeAngle {0.0F};
    float outerConeAngle {0.7853981634F};
};

enum class CameraComponentProjection : std::uint8_t {
    Perspective,
    Orthographic,
};

struct CameraComponent {
    CameraComponentProjection projection {CameraComponentProjection::Perspective};
    math::Vec3 direction {0.0F, 0.0F, 1.0F};
    math::Vec3 right {1.0F, 0.0F, 0.0F};
    math::Vec3 up {0.0F, 1.0F, 0.0F};
    float verticalFovRadians {1.04719755F};
    float aspectRatio {0.0F};
    float xMagnitude {1.0F};
    float yMagnitude {1.0F};
    float nearPlane {0.05F};
    float farPlane {4000.0F};
};

struct ScriptComponent {
    std::string scriptName {"FlyPlayerController"};
    bool enabled {true};
    float moveSpeed {7.5F};
    float fastMultiplier {3.0F};
    float lookSensitivity {0.0035F};
};

struct Entity {
    EntityId id;
    std::optional<EntityId> parent;
    std::vector<EntityId> children;
    std::string name;
    TransformComponent transform;
    std::optional<MeshRendererComponent> meshRenderer;
    std::optional<LightComponent> light;
    std::optional<CameraComponent> camera;
    std::optional<ScriptComponent> script;
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
    [[nodiscard]] bool setLight(EntityId id, std::optional<LightComponent> component);
    [[nodiscard]] bool setCamera(EntityId id, std::optional<CameraComponent> component);
    [[nodiscard]] bool setScript(EntityId id, std::optional<ScriptComponent> component);

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

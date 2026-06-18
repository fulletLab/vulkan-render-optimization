#pragma once

#include <projectunity/core/StableId.hpp>
#include <projectunity/math/Vec3.hpp>
#include <projectunity/terrain/TerrainTypes.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace projectunity::scene {

using EntityId = core::StableId;
using ScriptInstanceId = core::StableId;

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

struct AssetSlotReference {
    core::StableId assetId;
    std::optional<std::uint32_t> subAssetIndex;

    [[nodiscard]] bool isValid() const noexcept
    {
        return assetId.isValid();
    }

    [[nodiscard]] bool operator==(const AssetSlotReference&) const noexcept = default;
};

struct TextureOverride {
    AssetSlotReference texture;

    [[nodiscard]] bool enabled() const noexcept
    {
        return texture.isValid();
    }

    [[nodiscard]] bool operator==(const TextureOverride&) const noexcept = default;
};

struct MaterialSlotOverride {
    std::uint32_t slotIndex {0};
    std::optional<std::uint32_t> sourceMaterialIndex;
    AssetSlotReference material;
    TextureOverride baseColorTexture;
    TextureOverride normalTexture;
    TextureOverride metallicRoughnessTexture;
    TextureOverride emissiveTexture;
    std::array<float, 2> tiling {1.0F, 1.0F};
    std::array<float, 2> offset {0.0F, 0.0F};
    bool overrideEnabled {true};

    [[nodiscard]] bool hasAnyOverride() const noexcept
    {
        return material.isValid()
            || baseColorTexture.enabled()
            || normalTexture.enabled()
            || metallicRoughnessTexture.enabled()
            || emissiveTexture.enabled()
            || tiling != std::array<float, 2> {1.0F, 1.0F}
            || offset != std::array<float, 2> {0.0F, 0.0F};
    }
};

struct MaterialOverrideComponent {
    std::vector<MaterialSlotOverride> slots;
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

struct ScriptField {
    std::string name;
    float value {0.0F};
};

struct ScriptComponent {
    ScriptInstanceId instanceId;
    std::string scriptName;
    std::string scriptAsset;
    bool enabled {true};
    std::vector<ScriptField> fields;
};

[[nodiscard]] ScriptField* findScriptField(ScriptComponent& component, std::string_view name) noexcept;
[[nodiscard]] const ScriptField* findScriptField(const ScriptComponent& component, std::string_view name) noexcept;
[[nodiscard]] float scriptFieldValue(const ScriptComponent& component, std::string_view name, float fallback) noexcept;
void setScriptFieldValue(ScriptComponent& component, std::string name, float value);

enum class ComponentType : std::uint8_t {
    Transform,
    MeshRenderer,
    MaterialOverrides,
    Light,
    Camera,
    Script,
    Terrain,
    Rigidbody,
    Collider,
};

struct ComponentOrderEntry {
    ComponentType type {ComponentType::Transform};
    ScriptInstanceId scriptInstanceId;
};

struct TerrainComponent {
    terrain::TerrainSettings settings;
    std::vector<terrain::TerrainMaterialLayer> materialLayers {{}};
    std::vector<float> heightmap;
    core::StableId generatedModelAssetId;
    std::uint64_t colliderRevision {0};
    bool colliderDirty {true};
};

struct RigidbodyComponent {
    bool enabled {true};
    float mass {1.0F};
    float linearDrag {0.0F};
    float angularDrag {0.05F};
    bool useGravity {true};
    bool kinematic {false};
};

enum class ColliderShape : std::uint8_t {
    Box,
    Sphere,
    Capsule,
    Mesh,
    Terrain,
};

struct ColliderComponent {
    bool enabled {true};
    ColliderShape shape {ColliderShape::Box};
    math::Vec3 size {1.0F, 1.0F, 1.0F};
    math::Vec3 offset {0.0F, 0.0F, 0.0F};
    float radius {0.5F};
    float height {2.0F};
    bool trigger {false};
};

struct Entity {
    EntityId id;
    std::optional<EntityId> parent;
    std::vector<EntityId> children;
    std::string name;
    TransformComponent transform;
    std::optional<MeshRendererComponent> meshRenderer;
    std::optional<MaterialOverrideComponent> materialOverrides;
    std::optional<LightComponent> light;
    std::optional<CameraComponent> camera;
    std::vector<ScriptComponent> scripts;
    std::optional<TerrainComponent> terrain;
    std::optional<RigidbodyComponent> rigidbody;
    std::optional<ColliderComponent> collider;
    std::vector<ComponentOrderEntry> componentOrder {{ComponentType::Transform, {}}};
};

[[nodiscard]] ScriptComponent* findScript(Entity& entity, ScriptInstanceId instanceId) noexcept;
[[nodiscard]] const ScriptComponent* findScript(const Entity& entity, ScriptInstanceId instanceId) noexcept;

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
    [[nodiscard]] bool setMaterialOverrides(EntityId id, std::optional<MaterialOverrideComponent> component);
    [[nodiscard]] bool setLight(EntityId id, std::optional<LightComponent> component);
    [[nodiscard]] bool setCamera(EntityId id, std::optional<CameraComponent> component);
    [[nodiscard]] std::optional<ScriptInstanceId> addScript(EntityId id, ScriptComponent component);
    [[nodiscard]] bool updateScript(EntityId id, ScriptComponent component);
    [[nodiscard]] bool removeScript(EntityId id, ScriptInstanceId instanceId);
    [[nodiscard]] bool setScript(EntityId id, std::optional<ScriptComponent> component);
    [[nodiscard]] bool setTerrain(EntityId id, std::optional<TerrainComponent> component);
    [[nodiscard]] bool setRigidbody(EntityId id, std::optional<RigidbodyComponent> component);
    [[nodiscard]] bool setCollider(EntityId id, std::optional<ColliderComponent> component);

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
    [[nodiscard]] ScriptInstanceId allocateScriptInstanceId();
    [[nodiscard]] Entity* findEntityMutable(EntityId id) noexcept;
    [[nodiscard]] const Entity* findEntityInternal(EntityId id) const noexcept;
    [[nodiscard]] bool wouldCreateCycle(EntityId childId, EntityId candidateParentId) const noexcept;
    [[nodiscard]] bool removeChildReference(EntityId parentId, EntityId childId);
    [[nodiscard]] Entity* duplicateEntityRecursive(EntityId sourceId, std::optional<EntityId> parentOverride);
    void collectDescendants(EntityId id, std::vector<EntityId>& output) const;
    void rebuildComponentOrder(Entity& entity);
    void rebuildNextId();

    std::string name_ {"Untitled Scene"};
    std::vector<Entity> entities_;
    core::StableIdGenerator idGenerator_;
    core::StableIdGenerator scriptIdGenerator_;
};

} // namespace projectunity::scene

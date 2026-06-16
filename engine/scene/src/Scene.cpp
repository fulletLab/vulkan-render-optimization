#include <projectunity/scene/Scene.hpp>

#include <projectunity/core/Log.hpp>

#include <algorithm>
#include <limits>
#include <unordered_set>

namespace projectunity::scene {
namespace {

[[nodiscard]] std::string sanitizedName(std::string name)
{
    if (name.empty()) {
        return "GameObject";
    }
    return name;
}

void setSingletonComponentOrder(Entity& entity, ComponentType type, bool present)
{
    const auto matches = [type](const ComponentOrderEntry& entry) {
        return entry.type == type;
    };
    const auto it = std::find_if(entity.componentOrder.begin(), entity.componentOrder.end(), matches);
    if (present && it == entity.componentOrder.end()) {
        entity.componentOrder.push_back({type, {}});
    } else if (!present) {
        entity.componentOrder.erase(
            std::remove_if(entity.componentOrder.begin(), entity.componentOrder.end(), matches),
            entity.componentOrder.end());
    }
}

[[nodiscard]] bool validScriptComponent(const ScriptComponent& component) noexcept
{
    return !component.scriptName.empty()
        && !component.scriptAsset.empty()
        && !std::filesystem::path(component.scriptAsset).is_absolute();
}

} // namespace

ScriptField* findScriptField(ScriptComponent& component, std::string_view name) noexcept
{
    const auto it = std::find_if(component.fields.begin(), component.fields.end(), [name](const ScriptField& field) {
        return field.name == name;
    });
    return it == component.fields.end() ? nullptr : &*it;
}

const ScriptField* findScriptField(const ScriptComponent& component, std::string_view name) noexcept
{
    const auto it = std::find_if(component.fields.begin(), component.fields.end(), [name](const ScriptField& field) {
        return field.name == name;
    });
    return it == component.fields.end() ? nullptr : &*it;
}

float scriptFieldValue(const ScriptComponent& component, std::string_view name, float fallback) noexcept
{
    const auto* field = findScriptField(component, name);
    return field == nullptr ? fallback : field->value;
}

void setScriptFieldValue(ScriptComponent& component, std::string name, float value)
{
    if (auto* field = findScriptField(component, name)) {
        field->value = value;
        return;
    }
    component.fields.push_back({std::move(name), value});
}

ScriptComponent* findScript(Entity& entity, ScriptInstanceId instanceId) noexcept
{
    const auto it = std::find_if(entity.scripts.begin(), entity.scripts.end(), [instanceId](const ScriptComponent& script) {
        return script.instanceId == instanceId;
    });
    return it == entity.scripts.end() ? nullptr : &*it;
}

const ScriptComponent* findScript(const Entity& entity, ScriptInstanceId instanceId) noexcept
{
    const auto it = std::find_if(entity.scripts.begin(), entity.scripts.end(), [instanceId](const ScriptComponent& script) {
        return script.instanceId == instanceId;
    });
    return it == entity.scripts.end() ? nullptr : &*it;
}

Scene::Scene()
    : idGenerator_(1)
    , scriptIdGenerator_(1)
{
}

std::string_view Scene::name() const noexcept
{
    return name_;
}

void Scene::setName(std::string name)
{
    name_ = name.empty() ? "Untitled Scene" : std::move(name);
}

Entity& Scene::createEntity(std::string name, std::optional<EntityId> parent)
{
    Entity entity;
    entity.id = allocateId();
    entity.name = sanitizedName(std::move(name));

    if (parent.has_value() && contains(*parent)) {
        entity.parent = parent;
    } else if (parent.has_value()) {
        core::logWarning(core::LogCategory::Core, "Scene createEntity ignored an invalid parent id");
    }

    entities_.push_back(std::move(entity));
    auto& created = entities_.back();

    if (created.parent.has_value()) {
        if (auto* parentEntity = findEntityMutable(*created.parent)) {
            parentEntity->children.push_back(created.id);
        }
    }

    core::logInfo(core::LogCategory::Core, "Scene entity created");
    return created;
}

Entity* Scene::findEntity(EntityId id) noexcept
{
    return findEntityMutable(id);
}

const Entity* Scene::findEntity(EntityId id) const noexcept
{
    return findEntityInternal(id);
}

bool Scene::contains(EntityId id) const noexcept
{
    return findEntityInternal(id) != nullptr;
}

bool Scene::destroyEntity(EntityId id)
{
    auto* entity = findEntityMutable(id);
    if (entity == nullptr) {
        return false;
    }

    std::vector<EntityId> toRemove;
    collectDescendants(id, toRemove);
    toRemove.push_back(id);

    if (entity->parent.has_value()) {
        (void)removeChildReference(*entity->parent, id);
    }

    entities_.erase(
        std::remove_if(entities_.begin(), entities_.end(), [&toRemove](const Entity& current) {
            return std::find(toRemove.begin(), toRemove.end(), current.id) != toRemove.end();
        }),
        entities_.end());

    core::logInfo(core::LogCategory::Core, "Scene entity destroyed");
    return true;
}

Entity* Scene::duplicateEntity(EntityId sourceId)
{
    const auto* source = findEntity(sourceId);
    if (source == nullptr) {
        return nullptr;
    }

    return duplicateEntityRecursive(sourceId, source->parent);
}

Entity* Scene::duplicateEntityRecursive(EntityId sourceId, std::optional<EntityId> parentOverride)
{
    const auto* source = findEntity(sourceId);
    if (source == nullptr) {
        return nullptr;
    }

    const auto sourceSnapshot = *source;
    auto& duplicate = createEntity(sourceSnapshot.name + " Copy", parentOverride);
    duplicate.transform = sourceSnapshot.transform;
    duplicate.meshRenderer = sourceSnapshot.meshRenderer;
    duplicate.light = sourceSnapshot.light;
    duplicate.camera = sourceSnapshot.camera;
    duplicate.terrain = sourceSnapshot.terrain;
    duplicate.rigidbody = sourceSnapshot.rigidbody;
    duplicate.collider = sourceSnapshot.collider;
    const auto duplicateId = duplicate.id;

    duplicate.scripts.clear();
    duplicate.componentOrder.clear();
    std::unordered_set<std::uint64_t> copiedScriptIds;
    for (const auto& entry : sourceSnapshot.componentOrder) {
        if (entry.type != ComponentType::Script) {
            duplicate.componentOrder.push_back(entry);
            continue;
        }
        const auto* sourceScript = findScript(sourceSnapshot, entry.scriptInstanceId);
        if (sourceScript == nullptr) {
            continue;
        }
        auto scriptCopy = *sourceScript;
        scriptCopy.instanceId = allocateScriptInstanceId();
        duplicate.scripts.push_back(scriptCopy);
        duplicate.componentOrder.push_back({ComponentType::Script, scriptCopy.instanceId});
        copiedScriptIds.insert(sourceScript->instanceId.value());
    }
    for (const auto& sourceScript : sourceSnapshot.scripts) {
        if (!copiedScriptIds.contains(sourceScript.instanceId.value())) {
            auto scriptCopy = sourceScript;
            scriptCopy.instanceId = allocateScriptInstanceId();
            duplicate.scripts.push_back(scriptCopy);
            duplicate.componentOrder.push_back({ComponentType::Script, scriptCopy.instanceId});
        }
    }

    for (const auto childId : sourceSnapshot.children) {
        (void)duplicateEntityRecursive(childId, duplicateId);
    }

    return findEntityMutable(duplicateId);
}

bool Scene::setParent(EntityId childId, std::optional<EntityId> parentId)
{
    auto* child = findEntityMutable(childId);
    if (child == nullptr) {
        return false;
    }

    if (parentId.has_value()) {
        if (!contains(*parentId) || *parentId == childId || wouldCreateCycle(childId, *parentId)) {
            core::logWarning(core::LogCategory::Core, "Scene rejected invalid parent assignment");
            return false;
        }
    }

    if (child->parent.has_value()) {
        (void)removeChildReference(*child->parent, childId);
    }

    child->parent = parentId;

    if (parentId.has_value()) {
        if (auto* parent = findEntityMutable(*parentId)) {
            if (std::find(parent->children.begin(), parent->children.end(), childId) == parent->children.end()) {
                parent->children.push_back(childId);
            }
        }
    }

    return true;
}

bool Scene::setName(EntityId id, std::string name)
{
    auto* entity = findEntityMutable(id);
    if (entity == nullptr) {
        return false;
    }

    entity->name = sanitizedName(std::move(name));
    return true;
}

bool Scene::setTransform(EntityId id, const TransformComponent& transform)
{
    auto* entity = findEntityMutable(id);
    if (entity == nullptr) {
        return false;
    }

    entity->transform = transform;
    return true;
}

bool Scene::setMeshRenderer(EntityId id, std::optional<MeshRendererComponent> component)
{
    auto* entity = findEntityMutable(id);
    if (entity == nullptr) {
        return false;
    }

    if (component.has_value() && !component->modelAssetId.isValid()) {
        core::logWarning(core::LogCategory::Assets, "Scene rejected mesh renderer with an invalid model asset id");
        return false;
    }

    entity->meshRenderer = component;
    setSingletonComponentOrder(*entity, ComponentType::MeshRenderer, component.has_value());
    return true;
}

bool Scene::setLight(EntityId id, std::optional<LightComponent> component)
{
    auto* entity = findEntityMutable(id);
    if (entity == nullptr) {
        return false;
    }

    if (component.has_value() && component->intensity < 0.0F) {
        core::logWarning(core::LogCategory::Core, "Scene rejected light with negative intensity");
        return false;
    }

    entity->light = component;
    setSingletonComponentOrder(*entity, ComponentType::Light, component.has_value());
    return true;
}

bool Scene::setCamera(EntityId id, std::optional<CameraComponent> component)
{
    auto* entity = findEntityMutable(id);
    if (entity == nullptr) {
        return false;
    }

    if (component.has_value()
        && ((component->projection == CameraComponentProjection::Perspective && component->nearPlane <= 0.0F)
            || component->nearPlane < 0.0F
            || component->farPlane <= component->nearPlane)) {
        core::logWarning(core::LogCategory::Core, "Scene rejected camera with invalid clipping planes");
        return false;
    }

    entity->camera = component;
    setSingletonComponentOrder(*entity, ComponentType::Camera, component.has_value());
    return true;
}

std::optional<ScriptInstanceId> Scene::addScript(EntityId id, ScriptComponent component)
{
    auto* entity = findEntityMutable(id);
    if (entity == nullptr) {
        return std::nullopt;
    }

    if (!validScriptComponent(component)) {
        core::logWarning(core::LogCategory::Core, "Scene rejected script with invalid metadata");
        return std::nullopt;
    }

    component.instanceId = allocateScriptInstanceId();
    const auto instanceId = component.instanceId;
    entity->scripts.push_back(std::move(component));
    entity->componentOrder.push_back({ComponentType::Script, instanceId});
    return instanceId;
}

bool Scene::updateScript(EntityId id, ScriptComponent component)
{
    auto* entity = findEntityMutable(id);
    if (entity == nullptr || !component.instanceId.isValid() || !validScriptComponent(component)) {
        return false;
    }

    auto* current = findScript(*entity, component.instanceId);
    if (current == nullptr) {
        return false;
    }
    *current = std::move(component);
    return true;
}

bool Scene::removeScript(EntityId id, ScriptInstanceId instanceId)
{
    auto* entity = findEntityMutable(id);
    if (entity == nullptr || !instanceId.isValid()) {
        return false;
    }
    const auto oldSize = entity->scripts.size();
    entity->scripts.erase(
        std::remove_if(entity->scripts.begin(), entity->scripts.end(), [instanceId](const ScriptComponent& script) {
            return script.instanceId == instanceId;
        }),
        entity->scripts.end());
    if (entity->scripts.size() == oldSize) {
        return false;
    }
    entity->componentOrder.erase(
        std::remove_if(entity->componentOrder.begin(), entity->componentOrder.end(), [instanceId](const ComponentOrderEntry& entry) {
            return entry.type == ComponentType::Script && entry.scriptInstanceId == instanceId;
        }),
        entity->componentOrder.end());
    return true;
}

bool Scene::setScript(EntityId id, std::optional<ScriptComponent> component)
{
    auto* entity = findEntityMutable(id);
    if (entity == nullptr) {
        return false;
    }

    if (!component.has_value()) {
        entity->scripts.clear();
        entity->componentOrder.erase(
            std::remove_if(entity->componentOrder.begin(), entity->componentOrder.end(), [](const ComponentOrderEntry& entry) {
                return entry.type == ComponentType::Script;
            }),
            entity->componentOrder.end());
        return true;
    }
    if (!validScriptComponent(*component)) {
        return false;
    }
    if (entity->scripts.empty()) {
        return addScript(id, std::move(*component)).has_value();
    }

    component->instanceId = entity->scripts.front().instanceId;
    return updateScript(id, std::move(*component));
}

bool Scene::setTerrain(EntityId id, std::optional<TerrainComponent> component)
{
    auto* entity = findEntityMutable(id);
    if (entity == nullptr) {
        return false;
    }

    if (component.has_value()) {
        const auto& settings = component->settings;
        if (!(settings.width > 0.0F) || !(settings.length > 0.0F) || settings.heightScale < 0.0F
            || settings.resolution < 2U || settings.chunkSize == 0U || settings.octaves == 0U
            || component->materialLayers.empty() || component->materialLayers.size() > 8U
            || (!component->heightmap.empty()
                && component->heightmap.size() != static_cast<std::size_t>(settings.resolution) * settings.resolution)) {
            core::logWarning(core::LogCategory::Core, "Scene rejected terrain with invalid settings");
            return false;
        }
    }

    if (component.has_value()) {
        const auto nextDirty = !entity->terrain.has_value()
            || entity->terrain->settings != component->settings
            || entity->terrain->heightmap != component->heightmap
            || entity->terrain->materialLayers != component->materialLayers;
        component->colliderRevision = nextDirty
            ? (entity->terrain.has_value() ? entity->terrain->colliderRevision + 1U : 1U)
            : entity->terrain->colliderRevision;
        component->colliderDirty = nextDirty || component->colliderDirty;
    }

    entity->terrain = std::move(component);
    setSingletonComponentOrder(*entity, ComponentType::Terrain, entity->terrain.has_value());
    return true;
}

bool Scene::setRigidbody(EntityId id, std::optional<RigidbodyComponent> component)
{
    auto* entity = findEntityMutable(id);
    if (entity == nullptr) {
        return false;
    }

    if (component.has_value() && (!(component->mass > 0.0F) || component->linearDrag < 0.0F
            || component->angularDrag < 0.0F)) {
        core::logWarning(core::LogCategory::Physics, "Scene rejected rigidbody with invalid values");
        return false;
    }

    entity->rigidbody = component;
    setSingletonComponentOrder(*entity, ComponentType::Rigidbody, component.has_value());
    return true;
}

bool Scene::setCollider(EntityId id, std::optional<ColliderComponent> component)
{
    auto* entity = findEntityMutable(id);
    if (entity == nullptr) {
        return false;
    }

    if (component.has_value() && (component->size.x <= 0.0F || component->size.y <= 0.0F
            || component->size.z <= 0.0F || component->radius <= 0.0F || component->height <= 0.0F)) {
        core::logWarning(core::LogCategory::Physics, "Scene rejected collider with invalid values");
        return false;
    }

    entity->collider = component;
    setSingletonComponentOrder(*entity, ComponentType::Collider, component.has_value());
    return true;
}

std::vector<EntityId> Scene::rootEntities() const
{
    std::vector<EntityId> roots;
    roots.reserve(entities_.size());
    for (const auto& entity : entities_) {
        if (!entity.parent.has_value()) {
            roots.push_back(entity.id);
        }
    }
    return roots;
}

const std::vector<Entity>& Scene::entities() const noexcept
{
    return entities_;
}

std::size_t Scene::entityCount() const noexcept
{
    return entities_.size();
}

void Scene::clear()
{
    entities_.clear();
    name_ = "Untitled Scene";
    idGenerator_.reset(1);
    scriptIdGenerator_.reset(1);
}

EntityId Scene::allocateId()
{
    return idGenerator_.next();
}

ScriptInstanceId Scene::allocateScriptInstanceId()
{
    return scriptIdGenerator_.next();
}

Entity* Scene::findEntityMutable(EntityId id) noexcept
{
    auto it = std::find_if(entities_.begin(), entities_.end(), [id](const Entity& entity) {
        return entity.id == id;
    });
    return it == entities_.end() ? nullptr : &(*it);
}

const Entity* Scene::findEntityInternal(EntityId id) const noexcept
{
    auto it = std::find_if(entities_.begin(), entities_.end(), [id](const Entity& entity) {
        return entity.id == id;
    });
    return it == entities_.end() ? nullptr : &(*it);
}

bool Scene::wouldCreateCycle(EntityId childId, EntityId candidateParentId) const noexcept
{
    auto current = findEntityInternal(candidateParentId);
    while (current != nullptr) {
        if (current->id == childId) {
            return true;
        }
        current = current->parent.has_value() ? findEntityInternal(*current->parent) : nullptr;
    }
    return false;
}

bool Scene::removeChildReference(EntityId parentId, EntityId childId)
{
    auto* parent = findEntityMutable(parentId);
    if (parent == nullptr) {
        return false;
    }

    parent->children.erase(
        std::remove(parent->children.begin(), parent->children.end(), childId),
        parent->children.end());
    return true;
}

void Scene::collectDescendants(EntityId id, std::vector<EntityId>& output) const
{
    const auto* entity = findEntityInternal(id);
    if (entity == nullptr) {
        return;
    }

    for (const auto childId : entity->children) {
        collectDescendants(childId, output);
        output.push_back(childId);
    }
}

void Scene::rebuildComponentOrder(Entity& entity)
{
    entity.componentOrder.clear();
    entity.componentOrder.push_back({ComponentType::Transform, {}});
    if (entity.meshRenderer.has_value()) { entity.componentOrder.push_back({ComponentType::MeshRenderer, {}}); }
    if (entity.light.has_value()) { entity.componentOrder.push_back({ComponentType::Light, {}}); }
    if (entity.camera.has_value()) { entity.componentOrder.push_back({ComponentType::Camera, {}}); }
    for (const auto& script : entity.scripts) {
        entity.componentOrder.push_back({ComponentType::Script, script.instanceId});
    }
    if (entity.terrain.has_value()) { entity.componentOrder.push_back({ComponentType::Terrain, {}}); }
    if (entity.rigidbody.has_value()) { entity.componentOrder.push_back({ComponentType::Rigidbody, {}}); }
    if (entity.collider.has_value()) { entity.componentOrder.push_back({ComponentType::Collider, {}}); }
}

void Scene::rebuildNextId()
{
    std::uint64_t maxId = 0;
    std::uint64_t maxScriptId = 0;
    for (const auto& entity : entities_) {
        maxId = std::max(maxId, entity.id.value());
        for (const auto& script : entity.scripts) {
            maxScriptId = std::max(maxScriptId, script.instanceId.value());
        }
    }

    if (maxId == std::numeric_limits<std::uint64_t>::max()) {
        idGenerator_.reset(maxId);
    } else {
        idGenerator_.reset(maxId + 1);
    }
    scriptIdGenerator_.reset(maxScriptId == std::numeric_limits<std::uint64_t>::max() ? maxScriptId : maxScriptId + 1);
}

} // namespace projectunity::scene

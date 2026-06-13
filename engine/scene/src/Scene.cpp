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

} // namespace

Scene::Scene()
    : idGenerator_(1)
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
    duplicate.script = sourceSnapshot.script;
    const auto duplicateId = duplicate.id;

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
    return true;
}

bool Scene::setScript(EntityId id, std::optional<ScriptComponent> component)
{
    auto* entity = findEntityMutable(id);
    if (entity == nullptr) {
        return false;
    }

    if (component.has_value() && component->scriptName.empty()) {
        core::logWarning(core::LogCategory::Core, "Scene rejected script with an empty name");
        return false;
    }

    entity->script = std::move(component);
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
}

EntityId Scene::allocateId()
{
    return idGenerator_.next();
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

void Scene::rebuildNextId()
{
    std::uint64_t maxId = 0;
    for (const auto& entity : entities_) {
        maxId = std::max(maxId, entity.id.value());
    }

    if (maxId == std::numeric_limits<std::uint64_t>::max()) {
        idGenerator_.reset(maxId);
    } else {
        idGenerator_.reset(maxId + 1);
    }
}

} // namespace projectunity::scene

#include <projectunity/scripting/ScriptRuntime.hpp>

#include <projectunity/core/Log.hpp>
#include <projectunity/physics/PhysicsWorld.hpp>

#include <algorithm>
#include <exception>
#include <sstream>
#include <utility>

namespace projectunity::scripting {
namespace {

[[nodiscard]] bool transformChanged(
    const scene::TransformComponent& before,
    const scene::TransformComponent& after) noexcept
{
    return before.position.x != after.position.x
        || before.position.y != after.position.y
        || before.position.z != after.position.z
        || before.rotationEuler.x != after.rotationEuler.x
        || before.rotationEuler.y != after.rotationEuler.y
        || before.rotationEuler.z != after.rotationEuler.z
        || before.scale.x != after.scale.x
        || before.scale.y != after.scale.y
        || before.scale.z != after.scale.z;
}

} // namespace

ScriptContext::ScriptContext(
    scene::Scene& scene,
    scene::EntityId entityId,
    scene::ScriptInstanceId scriptInstanceId,
    const InputState& input) noexcept
    : scene_(scene)
    , entityId_(entityId)
    , scriptInstanceId_(scriptInstanceId)
    , input_(input)
{
}

scene::EntityId ScriptContext::entityId() const noexcept
{
    return entityId_;
}

scene::ScriptInstanceId ScriptContext::scriptInstanceId() const noexcept
{
    return scriptInstanceId_;
}

scene::Entity* ScriptContext::entity() noexcept
{
    return scene_.findEntity(entityId_);
}

scene::TransformComponent* ScriptContext::getTransform() noexcept
{
    auto* owner = entity();
    return owner == nullptr ? nullptr : &owner->transform;
}

scene::CameraComponent* ScriptContext::getCamera() noexcept
{
    auto* owner = entity();
    return owner == nullptr || !owner->camera.has_value() ? nullptr : &*owner->camera;
}

const InputState& ScriptContext::input() const noexcept
{
    return input_;
}

float ScriptContext::field(std::string_view name, float fallback) const noexcept
{
    const auto* owner = scene_.findEntity(entityId_);
    const auto* script = owner == nullptr ? nullptr : scene::findScript(*owner, scriptInstanceId_);
    return script == nullptr ? fallback : scene::scriptFieldValue(*script, name, fallback);
}

scene::Entity* ScriptContext::findEntity(scene::EntityId id) noexcept
{
    return scene_.findEntity(id);
}

void ScriptContext::log(std::string_view message) const
{
    core::logInfo(core::LogCategory::Core, message);
}

bool ScriptRegistry::registerScript(ScriptDescriptor descriptor)
{
    if (descriptor.className.empty() || !descriptor.create) {
        return false;
    }
    if (descriptor.assetPath.empty()) {
        descriptor.assetPath = "Assets/Scripts/" + descriptor.className + ".cpp";
    }
    const auto className = descriptor.className;
    descriptors_.insert_or_assign(className, std::move(descriptor));
    return true;
}

const ScriptDescriptor* ScriptRegistry::find(std::string_view className) const noexcept
{
    const auto it = descriptors_.find(std::string(className));
    return it == descriptors_.end() ? nullptr : &it->second;
}

const ScriptDescriptor* ScriptRegistry::findByAsset(std::string_view assetPath) const noexcept
{
    const auto it = std::find_if(descriptors_.begin(), descriptors_.end(), [assetPath](const auto& entry) {
        return entry.second.assetPath == assetPath;
    });
    return it == descriptors_.end() ? nullptr : &it->second;
}

std::vector<std::string> ScriptRegistry::classNames() const
{
    std::vector<std::string> names;
    names.reserve(descriptors_.size());
    for (const auto& [name, descriptor] : descriptors_) {
        (void)descriptor;
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::size_t ScriptRegistry::size() const noexcept
{
    return descriptors_.size();
}

std::optional<scene::ScriptComponent> ScriptRegistry::createComponentFromAsset(
    std::string_view assetPath,
    std::string* errorMessage) const
{
    const auto* descriptor = findByAsset(assetPath);
    if (descriptor == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Script asset found but class is not registered.";
        }
        return std::nullopt;
    }

    scene::ScriptComponent component;
    component.scriptName = descriptor->className;
    component.scriptAsset = descriptor->assetPath;
    applyDefaults(component);
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return component;
}

bool ScriptRegistry::replaceComponentAsset(
    scene::ScriptComponent& component,
    std::string_view assetPath,
    std::string* errorMessage) const
{
    auto replacement = createComponentFromAsset(assetPath, errorMessage);
    if (!replacement.has_value()) {
        return false;
    }

    replacement->instanceId = component.instanceId;
    replacement->enabled = component.enabled;
    for (auto& field : replacement->fields) {
        if (const auto* previous = scene::findScriptField(component, field.name)) {
            field.value = previous->value;
        }
    }
    component = std::move(*replacement);
    return true;
}

void ScriptRegistry::applyDefaults(scene::ScriptComponent& component) const
{
    const auto* descriptor = find(component.scriptName);
    if (descriptor == nullptr) {
        return;
    }
    for (const auto& field : descriptor->fields) {
        if (scene::findScriptField(component, field.name) == nullptr) {
            component.fields.push_back({field.name, field.defaultValue});
        }
    }
}

ScriptRuntime::~ScriptRuntime()
{
    stop();
}

void ScriptRuntime::setRegistry(const ScriptRegistry* registry) noexcept
{
    registry_ = registry;
}

bool ScriptRuntime::start(scene::Scene& scene)
{
    stop();
    scene_ = &scene;
    stats_ = {};
    stats_.scriptsRegistered = registry_ == nullptr ? 0U : registry_->size();
    const InputState emptyInput;
    synchronize(emptyInput);

    std::ostringstream message;
    message << "ScriptRuntime start"
            << " scriptsRegistered=" << stats_.scriptsRegistered
            << " scriptComponentsFound=" << stats_.scriptComponentsFound
            << " scriptInstancesCreated=" << stats_.scriptInstancesCreated
            << " scriptErrors=" << stats_.scriptErrors;
    core::logInfo(core::LogCategory::Core, message.str());
    return registry_ != nullptr;
}

void ScriptRuntime::update(float deltaTime, const InputState& input)
{
    if (scene_ == nullptr || registry_ == nullptr) {
        return;
    }
    synchronize(input);
    for (auto& active : instances_) {
        const auto* entity = scene_->findEntity(active.entityId);
        if (entity == nullptr || scene::findScript(*entity, active.scriptInstanceId) == nullptr || !active.enabled) {
            continue;
        }
        try {
            const auto beforeTransform = entity->transform;
            ScriptContext context(*scene_, active.entityId, active.scriptInstanceId, input);
            active.instance->onUpdate(context, deltaTime);
            ++stats_.scriptsUpdated;
            if (transformChanged(beforeTransform, entity->transform)) {
                ++stats_.transformsChanged;
                std::ostringstream message;
                message << "Script transform changed"
                        << " script=" << active.className
                        << " entity=" << active.entityId.value()
                        << " scriptInstance=" << active.scriptInstanceId.value()
                        << " position=(" << entity->transform.position.x << ','
                        << entity->transform.position.y << ',' << entity->transform.position.z << ')'
                        << " transformsChanged=" << stats_.transformsChanged;
                core::logInfo(core::LogCategory::Core, message.str());
            }
        } catch (const std::exception& exception) {
            reportError(active.className, active.entityId, exception.what());
        } catch (...) {
            reportError(active.className, active.entityId, "unknown exception in OnUpdate");
        }
    }
    const auto physicsStats = physics::stepBasic(*scene_, deltaTime);
    stats_.physicsContacts += physicsStats.resolvedContacts;
}

void ScriptRuntime::fixedUpdate(float deltaTime, const InputState& input)
{
    if (scene_ == nullptr || registry_ == nullptr) {
        return;
    }
    synchronize(input);
    for (auto& active : instances_) {
        if (!active.enabled) {
            continue;
        }
        try {
            ScriptContext context(*scene_, active.entityId, active.scriptInstanceId, input);
            active.instance->onFixedUpdate(context, deltaTime);
        } catch (const std::exception& exception) {
            reportError(active.className, active.entityId, exception.what());
        } catch (...) {
            reportError(active.className, active.entityId, "unknown exception in OnFixedUpdate");
        }
    }
}

void ScriptRuntime::stop()
{
    if (scene_ != nullptr) {
        const InputState emptyInput;
        for (auto& active : instances_) {
            detach(active, emptyInput);
        }
        std::ostringstream message;
        message << "ScriptRuntime stop"
                << " scriptsUpdated=" << stats_.scriptsUpdated
                << " scriptErrors=" << stats_.scriptErrors;
        core::logInfo(core::LogCategory::Core, message.str());
    }
    instances_.clear();
    scene_ = nullptr;
}

bool ScriptRuntime::running() const noexcept
{
    return scene_ != nullptr;
}

const ScriptRuntimeStats& ScriptRuntime::stats() const noexcept
{
    return stats_;
}

scene::EntityId findRuntimeCameraEntity(const scene::Scene& scene) noexcept
{
    for (const auto& entity : scene.entities()) {
        if (entity.camera.has_value()) {
            return entity.id;
        }
    }
    return {};
}

void ScriptRuntime::synchronize(const InputState& input)
{
    if (scene_ == nullptr || registry_ == nullptr) {
        return;
    }

    for (auto it = instances_.begin(); it != instances_.end();) {
        const auto* entity = scene_->findEntity(it->entityId);
        const auto* component = entity == nullptr ? nullptr : scene::findScript(*entity, it->scriptInstanceId);
        if (component == nullptr || component->scriptName != it->className) {
            detach(*it, input);
            it = instances_.erase(it);
        } else {
            ++it;
        }
    }

    stats_.scriptComponentsFound = 0;
    std::vector<std::pair<scene::EntityId, scene::ScriptInstanceId>> scriptedComponents;
    for (const auto& entity : scene_->entities()) {
        stats_.scriptComponentsFound += entity.scripts.size();
        for (const auto& component : entity.scripts) {
            scriptedComponents.push_back({entity.id, component.instanceId});
        }
    }
    for (const auto& [entityId, scriptInstanceId] : scriptedComponents) {
        auto* entity = scene_->findEntity(entityId);
        auto* component = entity == nullptr ? nullptr : scene::findScript(*entity, scriptInstanceId);
        if (entity == nullptr || component == nullptr) {
            continue;
        }
            const auto found = std::find_if(instances_.begin(), instances_.end(), [&entity, &component](const ActiveInstance& active) {
                return active.entityId == entity->id && active.scriptInstanceId == component->instanceId;
            });
            if (found == instances_.end()) {
                attach(*entity, *component, input);
                continue;
            }

            const auto shouldEnable = component->enabled;
            if (shouldEnable == found->enabled) {
                continue;
            }
            try {
                ScriptContext context(*scene_, entity->id, component->instanceId, input);
                if (shouldEnable) {
                    found->instance->onEnable(context);
                    if (!found->started) {
                        found->instance->onStart(context);
                        found->started = true;
                    }
                } else {
                    found->instance->onDisable(context);
                }
                found->enabled = shouldEnable;
            } catch (const std::exception& exception) {
                reportError(found->className, found->entityId, exception.what());
            } catch (...) {
                reportError(found->className, found->entityId, "unknown exception changing enabled state");
            }
    }
}

void ScriptRuntime::attach(scene::Entity& entity, scene::ScriptComponent& component, const InputState& input)
{
    if (registry_ == nullptr || scene_ == nullptr) {
        return;
    }
    const auto* descriptor = registry_->find(component.scriptName);
    if (descriptor == nullptr) {
        reportError(component.scriptName, entity.id, "Script asset found but class is not registered.");
        return;
    }
    registry_->applyDefaults(component);

    try {
        auto instance = descriptor->create();
        if (instance == nullptr) {
            reportError(component.scriptName, entity.id, "script factory returned null");
            return;
        }
        ActiveInstance active {
            entity.id,
            component.instanceId,
            component.scriptName,
            std::move(instance),
            component.enabled,
            false,
        };
        ScriptContext context(*scene_, entity.id, component.instanceId, input);
        active.instance->onAttach(context);
        active.instance->onCreate(context);
        if (active.enabled) {
            active.instance->onEnable(context);
            active.instance->onStart(context);
            active.started = true;
        }
        instances_.push_back(std::move(active));
        ++stats_.scriptInstancesCreated;
    } catch (const std::exception& exception) {
        reportError(component.scriptName, entity.id, exception.what());
    } catch (...) {
        reportError(component.scriptName, entity.id, "unknown exception creating script");
    }
}

void ScriptRuntime::detach(ActiveInstance& active, const InputState& input) noexcept
{
    if (scene_ == nullptr || active.instance == nullptr) {
        return;
    }
    try {
        ScriptContext context(*scene_, active.entityId, active.scriptInstanceId, input);
        if (active.enabled) {
            active.instance->onDisable(context);
        }
        active.instance->onDestroy(context);
        active.instance->onDetach(context);
    } catch (const std::exception& exception) {
        reportError(active.className, active.entityId, exception.what());
    } catch (...) {
        reportError(active.className, active.entityId, "unknown exception destroying script");
    }
}

void ScriptRuntime::reportError(std::string_view className, scene::EntityId entityId, std::string_view detail) noexcept
{
    ++stats_.scriptErrors;
    std::ostringstream message;
    message << "Script error"
            << " script=" << className
            << " entity=" << entityId.value()
            << " error=" << detail
            << " scriptErrors=" << stats_.scriptErrors;
    core::logError(core::LogCategory::Core, message.str());
}

} // namespace projectunity::scripting

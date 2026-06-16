#include <projectunity/scripting/ScriptRuntime.hpp>

#include <projectunity/core/Log.hpp>
#include <projectunity/physics/PhysicsWorld.hpp>

#include <algorithm>
#include <cerrno>
#include <exception>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <unordered_map>
#include <utility>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace projectunity::scripting {
namespace {

constexpr std::string_view kScriptClassNotLoadedError =
    "Script asset exists but native class is not loaded. Build/Reload Project Scripts.";

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

[[nodiscard]] scene::CameraComponent* findCameraInSubtree(scene::Scene& scene, scene::Entity& entity) noexcept
{
    if (entity.camera.has_value()) {
        return &*entity.camera;
    }
    for (const auto childId : entity.children) {
        auto* child = scene.findEntity(childId);
        if (child == nullptr) {
            continue;
        }
        if (auto* camera = findCameraInSubtree(scene, *child)) {
            return camera;
        }
    }
    return nullptr;
}

[[nodiscard]] std::string narrowPath(const std::filesystem::path& path)
{
    return path.string();
}

[[nodiscard]] std::filesystem::path runtimeModulePath(
    const std::filesystem::path& originalPath,
    std::uint64_t generation)
{
    std::ostringstream stem;
    const auto processId =
#if defined(_WIN32)
        static_cast<std::uint64_t>(GetCurrentProcessId());
#else
        static_cast<std::uint64_t>(getpid());
#endif
    stem << "ProjectScripts_runtime_" << processId << "_" << std::setw(3) << std::setfill('0') << generation;
    return originalPath.parent_path() / (stem.str() + originalPath.extension().string());
}

void setError(std::string* errorMessage, std::string message)
{
    if (errorMessage != nullptr) {
        *errorMessage = std::move(message);
    }
}

void clearError(std::string* errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
}

[[nodiscard]] bool copyRuntimeModule(
    const std::filesystem::path& sourcePath,
    const std::filesystem::path& destinationPath,
    std::string* errorMessage)
{
    std::ifstream source(sourcePath, std::ios::binary);
    if (!source) {
        setError(errorMessage, "Failed to open script module " + narrowPath(sourcePath) + " for runtime copy.");
        return false;
    }

    std::ofstream destination(destinationPath, std::ios::binary | std::ios::trunc);
    if (!destination) {
        setError(errorMessage, "Failed to open runtime script module " + narrowPath(destinationPath) + " for writing.");
        return false;
    }

    destination << source.rdbuf();
    if (!source.eof() && source.fail()) {
        setError(errorMessage, "Failed to read script module " + narrowPath(sourcePath) + " for runtime copy.");
        return false;
    }
    destination.close();
    if (!destination) {
        setError(errorMessage, "Failed to write runtime script module " + narrowPath(destinationPath) + ".");
        return false;
    }

    return true;
}

#if defined(_WIN32)
[[nodiscard]] std::string lastDynamicLibraryError()
{
    const auto code = GetLastError();
    if (code == 0) {
        return "unknown Windows loader error";
    }

    LPWSTR buffer = nullptr;
    const auto size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr);
    if (size == 0 || buffer == nullptr) {
        return "Windows loader error " + std::to_string(code);
    }

    std::wstring wide(buffer, size);
    LocalFree(buffer);
    while (!wide.empty() && (wide.back() == L'\n' || wide.back() == L'\r')) {
        wide.pop_back();
    }
    return std::filesystem::path(wide).string();
}

[[nodiscard]] void* openDynamicLibrary(const std::filesystem::path& path, std::string* errorMessage)
{
    auto* handle = LoadLibraryW(path.wstring().c_str());
    if (handle == nullptr) {
        setError(errorMessage, "Failed to load script module " + narrowPath(path) + ": " + lastDynamicLibraryError());
    }
    return reinterpret_cast<void*>(handle);
}

[[nodiscard]] ScriptModuleLoader::RegisterProjectScriptsFn findRegisterFunction(void* handle, std::string* errorMessage)
{
    auto* function = GetProcAddress(reinterpret_cast<HMODULE>(handle), "registerProjectScripts");
    if (function == nullptr) {
        setError(errorMessage, "Script module does not export registerProjectScripts: " + lastDynamicLibraryError());
        return nullptr;
    }
    return reinterpret_cast<ScriptModuleLoader::RegisterProjectScriptsFn>(function);
}

void closeDynamicLibrary(void* handle) noexcept
{
    if (handle != nullptr) {
        (void)FreeLibrary(reinterpret_cast<HMODULE>(handle));
    }
}
#else
[[nodiscard]] std::string lastDynamicLibraryError()
{
    const auto* error = dlerror();
    return error == nullptr ? "unknown dynamic loader error" : std::string(error);
}

[[nodiscard]] void* openDynamicLibrary(const std::filesystem::path& path, std::string* errorMessage)
{
    dlerror();
    auto* handle = dlopen(path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        setError(errorMessage, "Failed to load script module " + narrowPath(path) + ": " + lastDynamicLibraryError());
    }
    return handle;
}

[[nodiscard]] ScriptModuleLoader::RegisterProjectScriptsFn findRegisterFunction(void* handle, std::string* errorMessage)
{
    dlerror();
    auto* function = dlsym(handle, "registerProjectScripts");
    const auto* error = dlerror();
    if (error != nullptr || function == nullptr) {
        setError(errorMessage, "Script module does not export registerProjectScripts: "
            + std::string(error == nullptr ? "missing symbol" : error));
        return nullptr;
    }
    return reinterpret_cast<ScriptModuleLoader::RegisterProjectScriptsFn>(function);
}

void closeDynamicLibrary(void* handle) noexcept
{
    if (handle != nullptr) {
        (void)dlclose(handle);
    }
}
#endif

void logTransformChange(
    std::string_view sourceSystem,
    scene::EntityId entityId,
    const scene::TransformComponent& before,
    const scene::TransformComponent& after,
    std::string_view scriptName,
    std::string_view reason)
{
    std::ostringstream message;
    message << "Play transform modified"
            << " sourceSystem=" << sourceSystem
            << " entity=" << entityId.value()
            << " oldPosition=(" << before.position.x << ',' << before.position.y << ',' << before.position.z << ')'
            << " newPosition=(" << after.position.x << ',' << after.position.y << ',' << after.position.z << ')'
            << " scriptName=" << (scriptName.empty() ? "-" : std::string(scriptName))
            << " reason=" << reason;
    core::logInfo(core::LogCategory::Core, message.str());
}

} // namespace

ScriptContext::ScriptContext(
    scene::Scene& scene,
    scene::EntityId entityId,
    scene::ScriptInstanceId scriptInstanceId,
    const InputState& input,
    InputService* inputService) noexcept
    : scene_(scene)
    , entityId_(entityId)
    , scriptInstanceId_(scriptInstanceId)
    , input_(input)
    , inputService_(inputService)
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
    return owner == nullptr ? nullptr : findCameraInSubtree(scene_, *owner);
}

const InputState& ScriptContext::input() const noexcept
{
    return input_;
}

void ScriptContext::setMouseCaptured(bool captured) noexcept
{
    if (inputService_ != nullptr) {
        inputService_->setMouseCaptured(captured);
    }
}

bool ScriptContext::isMouseCaptured() const noexcept
{
    return inputService_ == nullptr ? input_.mouseCaptured : inputService_->isMouseCaptured();
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
            *errorMessage = std::string(kScriptClassNotLoadedError);
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

void ScriptRegistry::clear() noexcept
{
    descriptors_.clear();
}

ScriptModuleLoader::~ScriptModuleLoader()
{
    unload();
}

bool ScriptModuleLoader::load(
    const std::filesystem::path& modulePath,
    ScriptRegistry& registry,
    std::string* errorMessage)
{
    if (handle_ != nullptr) {
        unload(&registry);
    } else {
        registry.clear();
    }

    std::error_code errorCode;
    if (!std::filesystem::exists(modulePath, errorCode) || !std::filesystem::is_regular_file(modulePath, errorCode)) {
        setError(errorMessage, "Project scripts module not found: " + narrowPath(modulePath));
        return false;
    }

    const auto generation = nextGeneration_++;
    const auto copiedPath = runtimeModulePath(modulePath, generation);
    std::filesystem::create_directories(copiedPath.parent_path(), errorCode);
    if (errorCode) {
        setError(errorMessage, "Failed to create script runtime directory " + narrowPath(copiedPath.parent_path())
            + ": " + errorCode.message());
        return false;
    }

    if (!copyRuntimeModule(modulePath, copiedPath, errorMessage)) {
        return false;
    }

    auto* nextHandle = openDynamicLibrary(copiedPath, errorMessage);
    if (nextHandle == nullptr) {
        return false;
    }

    const auto registerProjectScripts = findRegisterFunction(nextHandle, errorMessage);
    if (registerProjectScripts == nullptr) {
        closeDynamicLibrary(nextHandle);
        return false;
    }

    try {
        registerProjectScripts(registry);
    } catch (const std::exception& exception) {
        registry.clear();
        closeDynamicLibrary(nextHandle);
        setError(errorMessage, "registerProjectScripts failed: " + std::string(exception.what()));
        return false;
    } catch (...) {
        registry.clear();
        closeDynamicLibrary(nextHandle);
        setError(errorMessage, "registerProjectScripts failed with an unknown exception");
        return false;
    }

    if (registry.size() == 0U) {
        registry.clear();
        closeDynamicLibrary(nextHandle);
        setError(errorMessage, "Script module loaded but did not register any native scripts.");
        return false;
    }

    handle_ = nextHandle;
    module_.originalPath = modulePath;
    module_.runtimePath = copiedPath;
    module_.generation = generation;
    module_.scriptsRegistered = registry.size();
    hasModule_ = true;
    clearError(errorMessage);

    std::ostringstream message;
    message << "Project scripts module loaded"
            << " original=" << narrowPath(module_.originalPath)
            << " runtime=" << narrowPath(module_.runtimePath)
            << " generation=" << module_.generation
            << " scriptsRegistered=" << module_.scriptsRegistered;
    core::logInfo(core::LogCategory::Core, message.str());
    return true;
}

bool ScriptModuleLoader::reload(
    const std::filesystem::path& modulePath,
    ScriptRegistry& registry,
    std::string* errorMessage)
{
    unload(&registry);
    return load(modulePath, registry, errorMessage);
}

void ScriptModuleLoader::unload(ScriptRegistry* registry) noexcept
{
    if (registry != nullptr) {
        registry->clear();
    }
    if (handle_ != nullptr) {
        closeDynamicLibrary(handle_);
        handle_ = nullptr;
    }
    if (hasModule_) {
        core::logInfo(core::LogCategory::Core, "Project scripts module unloaded");
    }
    module_ = {};
    hasModule_ = false;
}

const ScriptModule* ScriptModuleLoader::module() const noexcept
{
    return hasModule_ ? &module_ : nullptr;
}

std::uint64_t ScriptModuleLoader::generation() const noexcept
{
    return hasModule_ ? module_.generation : 0U;
}

ScriptRuntime::~ScriptRuntime()
{
    stop();
}

void ScriptRuntime::setRegistry(const ScriptRegistry* registry) noexcept
{
    registry_ = registry;
}

void ScriptRuntime::setInputService(InputService* inputService) noexcept
{
    inputService_ = inputService;
}

InputService* ScriptRuntime::inputService() const noexcept
{
    return inputService_;
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
        auto* entity = scene_->findEntity(active.entityId);
        if (entity == nullptr || scene::findScript(*entity, active.scriptInstanceId) == nullptr || !active.enabled) {
            continue;
        }
        try {
            const auto beforeTransform = entity->transform;
            ScriptContext context(*scene_, active.entityId, active.scriptInstanceId, input, inputService_);
            active.instance->onUpdate(context, deltaTime);
            ++stats_.scriptsUpdated;
            if (transformChanged(beforeTransform, entity->transform)) {
                ++stats_.transformsChanged;
                logTransformChange(
                    "ScriptRuntime",
                    active.entityId,
                    beforeTransform,
                    entity->transform,
                    active.className,
                    "OnUpdate");
            }
        } catch (const std::exception& exception) {
            reportError(active.className, active.entityId, exception.what());
        } catch (...) {
            reportError(active.className, active.entityId, "unknown exception in OnUpdate");
        }
    }
    std::unordered_map<std::uint64_t, scene::TransformComponent> beforePhysics;
    beforePhysics.reserve(scene_->entities().size());
    for (const auto& entity : scene_->entities()) {
        beforePhysics.emplace(entity.id.value(), entity.transform);
    }
    const auto physicsStats = physics::stepBasic(*scene_, deltaTime);
    stats_.physicsContacts += physicsStats.resolvedContacts;
    for (const auto& entity : scene_->entities()) {
        const auto before = beforePhysics.find(entity.id.value());
        if (before != beforePhysics.end() && transformChanged(before->second, entity.transform)) {
            ++stats_.transformsChanged;
            logTransformChange(
                "Physics",
                entity.id,
                before->second,
                entity.transform,
                {},
                "BasicPhysicsStep");
        }
    }
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
            auto* entity = scene_->findEntity(active.entityId);
            const auto beforeTransform = entity == nullptr ? scene::TransformComponent {} : entity->transform;
            ScriptContext context(*scene_, active.entityId, active.scriptInstanceId, input, inputService_);
            active.instance->onFixedUpdate(context, deltaTime);
            entity = scene_->findEntity(active.entityId);
            if (entity != nullptr && transformChanged(beforeTransform, entity->transform)) {
                ++stats_.transformsChanged;
                logTransformChange(
                    "ScriptRuntime",
                    active.entityId,
                    beforeTransform,
                    entity->transform,
                    active.className,
                    "OnFixedUpdate");
            }
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
                const auto beforeTransform = entity->transform;
                ScriptContext context(*scene_, entity->id, component->instanceId, input, inputService_);
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
                if (transformChanged(beforeTransform, entity->transform)) {
                    ++stats_.transformsChanged;
                    logTransformChange(
                        "ScriptRuntime",
                        entity->id,
                        beforeTransform,
                        entity->transform,
                        found->className,
                        shouldEnable ? "OnEnable" : "OnDisable");
                }
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
        reportError(component.scriptName, entity.id, kScriptClassNotLoadedError);
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
        ScriptContext context(*scene_, entity.id, component.instanceId, input, inputService_);
        const auto beforeTransform = entity.transform;
        active.instance->onAttach(context);
        active.instance->onCreate(context);
        if (active.enabled) {
            active.instance->onEnable(context);
            active.instance->onStart(context);
            active.started = true;
        }
        if (transformChanged(beforeTransform, entity.transform)) {
            ++stats_.transformsChanged;
            logTransformChange(
                "ScriptRuntime",
                entity.id,
                beforeTransform,
                entity.transform,
                component.scriptName,
                "LifecycleStart");
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
        auto* entity = scene_->findEntity(active.entityId);
        const auto beforeTransform = entity == nullptr ? scene::TransformComponent {} : entity->transform;
        ScriptContext context(*scene_, active.entityId, active.scriptInstanceId, input, inputService_);
        if (active.enabled) {
            active.instance->onDisable(context);
        }
        active.instance->onDestroy(context);
        active.instance->onDetach(context);
        entity = scene_->findEntity(active.entityId);
        if (entity != nullptr && transformChanged(beforeTransform, entity->transform)) {
            logTransformChange(
                "ScriptRuntime",
                active.entityId,
                beforeTransform,
                entity->transform,
                active.className,
                "LifecycleStop");
        }
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

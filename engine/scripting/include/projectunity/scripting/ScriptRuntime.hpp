#pragma once

#include <projectunity/scene/Scene.hpp>
#include <projectunity/scripting/InputState.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace projectunity::scripting {

struct ScriptFieldDefinition {
    std::string name;
    float defaultValue {0.0F};
};

class ScriptContext final {
public:
    ScriptContext(
        scene::Scene& scene,
        scene::EntityId entityId,
        scene::ScriptInstanceId scriptInstanceId,
        const InputState& input) noexcept;

    [[nodiscard]] scene::EntityId entityId() const noexcept;
    [[nodiscard]] scene::ScriptInstanceId scriptInstanceId() const noexcept;
    [[nodiscard]] scene::Entity* entity() noexcept;
    [[nodiscard]] scene::TransformComponent* getTransform() noexcept;
    [[nodiscard]] scene::CameraComponent* getCamera() noexcept;
    template <typename Component>
    [[nodiscard]] Component* getComponent() noexcept
    {
        auto* owner = entity();
        if (owner == nullptr) {
            return nullptr;
        }
        if constexpr (std::is_same_v<Component, scene::TransformComponent>) {
            return &owner->transform;
        } else if constexpr (std::is_same_v<Component, scene::CameraComponent>) {
            return owner->camera.has_value() ? &*owner->camera : nullptr;
        } else if constexpr (std::is_same_v<Component, scene::MeshRendererComponent>) {
            return owner->meshRenderer.has_value() ? &*owner->meshRenderer : nullptr;
        } else if constexpr (std::is_same_v<Component, scene::LightComponent>) {
            return owner->light.has_value() ? &*owner->light : nullptr;
        } else if constexpr (std::is_same_v<Component, scene::TerrainComponent>) {
            return owner->terrain.has_value() ? &*owner->terrain : nullptr;
        } else if constexpr (std::is_same_v<Component, scene::RigidbodyComponent>) {
            return owner->rigidbody.has_value() ? &*owner->rigidbody : nullptr;
        } else if constexpr (std::is_same_v<Component, scene::ColliderComponent>) {
            return owner->collider.has_value() ? &*owner->collider : nullptr;
        } else {
            return nullptr;
        }
    }
    [[nodiscard]] const InputState& input() const noexcept;
    [[nodiscard]] float field(std::string_view name, float fallback) const noexcept;
    [[nodiscard]] scene::Entity* findEntity(scene::EntityId id) noexcept;
    void log(std::string_view message) const;

private:
    scene::Scene& scene_;
    scene::EntityId entityId_;
    scene::ScriptInstanceId scriptInstanceId_;
    const InputState& input_;
};

class ScriptInstance {
public:
    virtual ~ScriptInstance() = default;
    virtual void onAttach(ScriptContext&) { }
    virtual void onCreate(ScriptContext&) { }
    virtual void onEnable(ScriptContext&) { }
    virtual void onStart(ScriptContext&) { }
    virtual void onUpdate(ScriptContext&, float) { }
    virtual void onFixedUpdate(ScriptContext&, float) { }
    virtual void onDisable(ScriptContext&) { }
    virtual void onDestroy(ScriptContext&) { }
    virtual void onDetach(ScriptContext&) { }
};

struct ScriptDescriptor {
    std::string className;
    std::string assetPath;
    std::vector<ScriptFieldDefinition> fields;
    std::function<std::unique_ptr<ScriptInstance>()> create;
};

class ScriptRegistry final {
public:
    [[nodiscard]] bool registerScript(ScriptDescriptor descriptor);
    [[nodiscard]] const ScriptDescriptor* find(std::string_view className) const noexcept;
    [[nodiscard]] const ScriptDescriptor* findByAsset(std::string_view assetPath) const noexcept;
    [[nodiscard]] std::vector<std::string> classNames() const;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::optional<scene::ScriptComponent> createComponentFromAsset(
        std::string_view assetPath,
        std::string* errorMessage = nullptr) const;
    [[nodiscard]] bool replaceComponentAsset(
        scene::ScriptComponent& component,
        std::string_view assetPath,
        std::string* errorMessage = nullptr) const;
    void applyDefaults(scene::ScriptComponent& component) const;

private:
    std::unordered_map<std::string, ScriptDescriptor> descriptors_;
};

struct ScriptRuntimeStats {
    std::size_t scriptsRegistered {0};
    std::size_t scriptComponentsFound {0};
    std::size_t scriptInstancesCreated {0};
    std::size_t scriptsUpdated {0};
    std::size_t transformsChanged {0};
    std::size_t scriptErrors {0};
};

[[nodiscard]] scene::EntityId findRuntimeCameraEntity(const scene::Scene& scene) noexcept;

class ScriptRuntime final {
public:
    ScriptRuntime() = default;
    ~ScriptRuntime();

    void setRegistry(const ScriptRegistry* registry) noexcept;
    [[nodiscard]] bool start(scene::Scene& scene);
    void update(float deltaTime, const InputState& input);
    void fixedUpdate(float deltaTime, const InputState& input);
    void stop();

    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] const ScriptRuntimeStats& stats() const noexcept;

private:
    struct ActiveInstance {
        scene::EntityId entityId;
        scene::ScriptInstanceId scriptInstanceId;
        std::string className;
        std::unique_ptr<ScriptInstance> instance;
        bool enabled {false};
        bool started {false};
    };

    void synchronize(const InputState& input);
    void attach(scene::Entity& entity, scene::ScriptComponent& component, const InputState& input);
    void detach(ActiveInstance& active, const InputState& input) noexcept;
    void reportError(std::string_view className, scene::EntityId entityId, std::string_view detail) noexcept;

    const ScriptRegistry* registry_ {nullptr};
    scene::Scene* scene_ {nullptr};
    std::vector<ActiveInstance> instances_;
    ScriptRuntimeStats stats_;
};

void registerBuiltInScripts(ScriptRegistry& registry);

} // namespace projectunity::scripting

#include <projectunity/scripting/ScriptRuntime.hpp>

#include <algorithm>
#include <memory>
#include <utility>

namespace projectunity_game_scripts {
namespace {

class Health final : public projectunity::scripting::IScriptInstance {
public:
    void onCreate(projectunity::scripting::ScriptContext& context) override
    {
        currentHealth_ = std::clamp(
            context.field("currentHealth", 100.0F),
            0.0F,
            std::max(0.0F, context.field("maxHealth", 100.0F)));
    }

    void onUpdate(projectunity::scripting::ScriptContext& context, float deltaTime) override
    {
        const auto maxHealth = std::max(0.0F, context.field("maxHealth", 100.0F));
        currentHealth_ = std::clamp(
            currentHealth_ + std::max(0.0F, context.field("regenerationPerSecond", 0.0F)) * std::max(0.0F, deltaTime),
            0.0F,
            maxHealth);
    }

private:
    float currentHealth_ {100.0F};
};

} // namespace

void registerHealth(projectunity::scripting::ScriptRegistry& registry)
{
    projectunity::scripting::ScriptDescriptor descriptor;
    descriptor.className = "Health";
    descriptor.assetPath = "Assets/Scripts/Health.cpp";
    descriptor.fields = {
        {"maxHealth", 100.0F},
        {"currentHealth", 100.0F},
        {"regenerationPerSecond", 0.0F},
    };
    descriptor.create = [] {
        return std::make_unique<Health>();
    };
    (void)registry.registerScript(std::move(descriptor));
}

} // namespace projectunity_game_scripts

#include <projectunity/scripting/ScriptRuntime.hpp>

namespace projectunity_game_scripts {

void registerFlyPlayerController(projectunity::scripting::ScriptRegistry& registry);
void registerHealth(projectunity::scripting::ScriptRegistry& registry);

} // namespace projectunity_game_scripts

#if defined(_WIN32)
#define PROJECTUNITY_GAME_SCRIPTS_EXPORT extern "C" __declspec(dllexport)
#else
#define PROJECTUNITY_GAME_SCRIPTS_EXPORT extern "C" __attribute__((visibility("default")))
#endif

PROJECTUNITY_GAME_SCRIPTS_EXPORT void registerProjectScripts(ProjectUnity::Scripting::ScriptRegistry& registry)
{
    projectunity_game_scripts::registerFlyPlayerController(registry);
    projectunity_game_scripts::registerHealth(registry);
}

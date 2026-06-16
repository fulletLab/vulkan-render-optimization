#include <projectunity/scripting/ScriptRuntime.hpp>

#include <projectunity/core/Log.hpp>

namespace projectunity::scripting {

void registerBuiltInScripts(ScriptRegistry& registry)
{
    (void)registry;
    core::logInfo(
        core::LogCategory::Core,
        "Built-in gameplay scripts are disabled; load Project/Binaries/Scripts/ProjectUnityGameScripts instead.");
}

} // namespace projectunity::scripting

#pragma once

#include <projectunity/scene/Scene.hpp>

#include <nlohmann/json_fwd.hpp>

#include <string>
#include <vector>

namespace projectunity::scene::serialization {

[[nodiscard]] nlohmann::json scriptsToJson(const std::vector<ScriptComponent>& scripts);
[[nodiscard]] bool scriptsFromJson(
    const nlohmann::json& entityJson,
    std::vector<ScriptComponent>& scripts,
    std::string& errorMessage);

[[nodiscard]] nlohmann::json componentOrderToJson(const std::vector<ComponentOrderEntry>& order);
[[nodiscard]] bool componentOrderFromJson(
    const nlohmann::json& json,
    std::vector<ComponentOrderEntry>& order,
    std::string& errorMessage);

} // namespace projectunity::scene::serialization

#include "SceneScriptSerialization.hpp"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <unordered_set>

namespace projectunity::scene::serialization {
namespace {

[[nodiscard]] const char* componentTypeName(ComponentType type) noexcept
{
    switch (type) {
    case ComponentType::Transform: return "transform";
    case ComponentType::MeshRenderer: return "meshRenderer";
    case ComponentType::MaterialOverrides: return "materialOverrides";
    case ComponentType::Light: return "light";
    case ComponentType::Camera: return "camera";
    case ComponentType::Script: return "script";
    case ComponentType::Terrain: return "terrain";
    case ComponentType::Rigidbody: return "rigidbody";
    case ComponentType::Collider: return "collider";
    }
    return "transform";
}

[[nodiscard]] bool componentTypeFromName(const std::string& name, ComponentType& type) noexcept
{
    if (name == "transform") { type = ComponentType::Transform; return true; }
    if (name == "meshRenderer") { type = ComponentType::MeshRenderer; return true; }
    if (name == "materialOverrides") { type = ComponentType::MaterialOverrides; return true; }
    if (name == "light") { type = ComponentType::Light; return true; }
    if (name == "camera") { type = ComponentType::Camera; return true; }
    if (name == "script") { type = ComponentType::Script; return true; }
    if (name == "terrain") { type = ComponentType::Terrain; return true; }
    if (name == "rigidbody") { type = ComponentType::Rigidbody; return true; }
    if (name == "collider") { type = ComponentType::Collider; return true; }
    return false;
}

[[nodiscard]] bool scriptFromJson(
    const nlohmann::json& json,
    ScriptComponent& script,
    std::string& errorMessage)
{
    if (!json.is_object()) {
        errorMessage = "Scene script must be an object";
        return false;
    }
    script.instanceId = ScriptInstanceId(json.value("instanceId", std::uint64_t {0}));
    script.scriptName = json.value("name", std::string {});
    script.scriptAsset = json.value("asset", std::string("Assets/Scripts/") + script.scriptName + ".cpp");
    script.enabled = json.value("enabled", script.enabled);
    if (script.scriptName.empty()) {
        errorMessage = "Scene script name must be non-empty";
        return false;
    }
    if (script.scriptAsset.empty() || std::filesystem::path(script.scriptAsset).is_absolute()) {
        errorMessage = "Scene script asset must be a relative path";
        return false;
    }
    if (json.contains("fields")) {
        const auto& fieldsJson = json.at("fields");
        if (!fieldsJson.is_object()) {
            errorMessage = "Scene script fields must be an object";
            return false;
        }
        for (const auto& [name, value] : fieldsJson.items()) {
            if (name.empty() || !value.is_number()) {
                errorMessage = "Scene script field must have a name and numeric value";
                return false;
            }
            script.fields.push_back({name, value.get<float>()});
        }
    } else {
        const auto moveSpeed = json.value("moveSpeed", 7.5F);
        const auto fastMultiplier = json.value("fastMultiplier", 3.0F);
        setScriptFieldValue(script, "speed", moveSpeed);
        setScriptFieldValue(script, "sprintSpeed", moveSpeed * fastMultiplier);
        setScriptFieldValue(script, "mouseSensitivity", json.value("lookSensitivity", 0.0035F) * 57.2957795F);
    }
    return true;
}

} // namespace

nlohmann::json scriptsToJson(const std::vector<ScriptComponent>& scripts)
{
    auto output = nlohmann::json::array();
    for (const auto& script : scripts) {
        auto fields = nlohmann::json::object();
        for (const auto& field : script.fields) {
            fields[field.name] = field.value;
        }
        output.push_back({
            {"instanceId", script.instanceId.value()},
            {"name", script.scriptName},
            {"asset", script.scriptAsset},
            {"enabled", script.enabled},
            {"fields", std::move(fields)},
        });
    }
    return output;
}

bool scriptsFromJson(
    const nlohmann::json& entityJson,
    std::vector<ScriptComponent>& scripts,
    std::string& errorMessage)
{
    scripts.clear();
    if (entityJson.contains("scripts")) {
        const auto& scriptsJson = entityJson.at("scripts");
        if (!scriptsJson.is_array()) {
            errorMessage = "Scene scripts must be an array";
            return false;
        }
        for (const auto& scriptJson : scriptsJson) {
            ScriptComponent script;
            if (!scriptFromJson(scriptJson, script, errorMessage)) {
                return false;
            }
            scripts.push_back(std::move(script));
        }
        return true;
    }
    if (entityJson.contains("script")) {
        ScriptComponent script;
        if (!scriptFromJson(entityJson.at("script"), script, errorMessage)) {
            return false;
        }
        scripts.push_back(std::move(script));
    }
    return true;
}

nlohmann::json componentOrderToJson(const std::vector<ComponentOrderEntry>& order)
{
    auto output = nlohmann::json::array();
    for (const auto& entry : order) {
        nlohmann::json item {{"type", componentTypeName(entry.type)}};
        if (entry.type == ComponentType::Script) {
            item["instanceId"] = entry.scriptInstanceId.value();
        }
        output.push_back(std::move(item));
    }
    return output;
}

bool componentOrderFromJson(
    const nlohmann::json& json,
    std::vector<ComponentOrderEntry>& order,
    std::string& errorMessage)
{
    if (!json.is_array()) {
        errorMessage = "Scene component order must be an array";
        return false;
    }
    order.clear();
    std::unordered_set<std::string> uniqueEntries;
    for (const auto& item : json) {
        if (!item.is_object() || !item.contains("type") || !item.at("type").is_string()) {
            errorMessage = "Scene component order entry is invalid";
            return false;
        }
        ComponentOrderEntry entry;
        const auto typeName = item.at("type").get<std::string>();
        if (!componentTypeFromName(typeName, entry.type)) {
            errorMessage = "Scene component order contains an unknown type";
            return false;
        }
        auto uniqueKey = typeName;
        if (entry.type == ComponentType::Script) {
            entry.scriptInstanceId = ScriptInstanceId(item.value("instanceId", std::uint64_t {0}));
            if (!entry.scriptInstanceId.isValid()) {
                errorMessage = "Scene script component order requires a valid instance id";
                return false;
            }
            uniqueKey += ':' + std::to_string(entry.scriptInstanceId.value());
        }
        if (!uniqueEntries.insert(uniqueKey).second) {
            errorMessage = "Scene component order contains duplicate entries";
            return false;
        }
        order.push_back(entry);
    }
    return true;
}

} // namespace projectunity::scene::serialization

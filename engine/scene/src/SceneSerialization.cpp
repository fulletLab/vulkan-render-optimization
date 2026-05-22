#include <projectunity/scene/Scene.hpp>

#include <projectunity/core/Log.hpp>

#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <unordered_map>

namespace projectunity::scene {
namespace {

constexpr int kSceneFormatVersion = 1;

[[nodiscard]] nlohmann::json vecToJson(const math::Vec3& value)
{
    return nlohmann::json::array({value.x, value.y, value.z});
}

[[nodiscard]] bool vecFromJson(const nlohmann::json& json, math::Vec3& output)
{
    if (!json.is_array() || json.size() != 3) {
        return false;
    }

    for (const auto& element : json) {
        if (!element.is_number()) {
            return false;
        }
    }

    output = {
        json.at(0).get<float>(),
        json.at(1).get<float>(),
        json.at(2).get<float>(),
    };
    return true;
}

void setError(std::string* errorMessage, std::string message)
{
    if (errorMessage != nullptr) {
        *errorMessage = std::move(message);
    }
}

} // namespace

std::string Scene::serialize(std::string* errorMessage) const
{
    try {
        nlohmann::json root;
        root["version"] = kSceneFormatVersion;
        root["name"] = name_;
        root["entities"] = nlohmann::json::array();

        for (const auto& entity : entities_) {
            nlohmann::json item;
            item["id"] = entity.id.value();
            item["parent"] = entity.parent.has_value() ? nlohmann::json((*entity.parent).value()) : nlohmann::json(nullptr);
            item["name"] = entity.name;
            item["transform"] = {
                {"position", vecToJson(entity.transform.position)},
                {"rotationEuler", vecToJson(entity.transform.rotationEuler)},
                {"scale", vecToJson(entity.transform.scale)},
            };
            if (entity.meshRenderer.has_value()) {
                item["meshRenderer"] = {
                    {"modelAssetId", entity.meshRenderer->modelAssetId.value()},
                };
            }
            root["entities"].push_back(std::move(item));
        }

        return root.dump(2);
    } catch (const std::exception& exception) {
        setError(errorMessage, exception.what());
        core::logError(core::LogCategory::Core, "Scene serialization failed");
        return {};
    }
}

bool Scene::deserialize(std::string_view jsonText, std::string* errorMessage)
{
    try {
        const auto root = nlohmann::json::parse(jsonText.begin(), jsonText.end());
        if (!root.is_object()) {
            setError(errorMessage, "Scene root must be a JSON object");
            return false;
        }

        const auto version = root.value("version", 0);
        if (version != kSceneFormatVersion) {
            setError(errorMessage, "Unsupported scene format version");
            return false;
        }

        const auto& entitiesJson = root.at("entities");
        if (!entitiesJson.is_array()) {
            setError(errorMessage, "Scene entities must be an array");
            return false;
        }

        std::vector<Entity> loadedEntities;
        loadedEntities.reserve(entitiesJson.size());
        std::unordered_map<std::uint64_t, std::size_t> indexById;

        for (const auto& item : entitiesJson) {
            if (!item.is_object()) {
                setError(errorMessage, "Scene entity must be an object");
                return false;
            }

            Entity entity;
            entity.id = EntityId(item.at("id").get<std::uint64_t>());
            if (!entity.id.isValid()) {
                setError(errorMessage, "Scene entity id must be non-zero");
                return false;
            }

            if (indexById.contains(entity.id.value())) {
                setError(errorMessage, "Scene contains duplicate entity ids");
                return false;
            }

            const auto& parentJson = item.at("parent");
            if (!parentJson.is_null()) {
                entity.parent = EntityId(parentJson.get<std::uint64_t>());
                if (!entity.parent->isValid()) {
                    setError(errorMessage, "Scene entity parent id must be non-zero or null");
                    return false;
                }
            }

            entity.name = item.value("name", "GameObject");

            const auto& transformJson = item.at("transform");
            if (!vecFromJson(transformJson.at("position"), entity.transform.position)
                || !vecFromJson(transformJson.at("rotationEuler"), entity.transform.rotationEuler)
                || !vecFromJson(transformJson.at("scale"), entity.transform.scale)) {
                setError(errorMessage, "Scene transform vectors must have three numeric elements");
                return false;
            }

            if (item.contains("meshRenderer")) {
                const auto& meshRendererJson = item.at("meshRenderer");
                if (!meshRendererJson.is_object()) {
                    setError(errorMessage, "Scene mesh renderer must be an object");
                    return false;
                }

                MeshRendererComponent meshRenderer;
                meshRenderer.modelAssetId = core::StableId(meshRendererJson.at("modelAssetId").get<std::uint64_t>());
                if (!meshRenderer.modelAssetId.isValid()) {
                    setError(errorMessage, "Scene mesh renderer model asset id must be non-zero");
                    return false;
                }
                entity.meshRenderer = meshRenderer;
            }

            indexById.emplace(entity.id.value(), loadedEntities.size());
            loadedEntities.push_back(std::move(entity));
        }

        for (auto& entity : loadedEntities) {
            if (entity.parent.has_value()) {
                auto parentIt = indexById.find(entity.parent->value());
                if (parentIt == indexById.end()) {
                    setError(errorMessage, "Scene entity references a missing parent");
                    return false;
                }
                loadedEntities[parentIt->second].children.push_back(entity.id);
            }
        }

        entities_ = std::move(loadedEntities);
        setName(root.value("name", "Untitled Scene"));
        rebuildNextId();
        core::logInfo(core::LogCategory::Core, "Scene deserialized");
        return true;
    } catch (const std::exception& exception) {
        setError(errorMessage, exception.what());
        core::logError(core::LogCategory::Core, "Scene deserialization failed");
        return false;
    }
}

bool Scene::saveToFile(const std::filesystem::path& path, std::string* errorMessage) const
{
    const auto json = serialize(errorMessage);
    if (json.empty()) {
        return false;
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        setError(errorMessage, "Unable to open scene file for writing");
        core::logError(core::LogCategory::Core, "Scene save failed: unable to open output file");
        return false;
    }

    file << json;
    if (!file.good()) {
        setError(errorMessage, "Unable to write scene file");
        core::logError(core::LogCategory::Core, "Scene save failed: write error");
        return false;
    }

    core::logInfo(core::LogCategory::Core, "Scene saved");
    return true;
}

bool Scene::loadFromFile(const std::filesystem::path& path, std::string* errorMessage)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        setError(errorMessage, "Unable to open scene file for reading");
        core::logError(core::LogCategory::Core, "Scene load failed: unable to open input file");
        return false;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    if (!file.good() && !file.eof()) {
        setError(errorMessage, "Unable to read scene file");
        core::logError(core::LogCategory::Core, "Scene load failed: read error");
        return false;
    }

    return deserialize(buffer.str(), errorMessage);
}

} // namespace projectunity::scene

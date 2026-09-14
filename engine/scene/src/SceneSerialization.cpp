#include <projectunity/scene/Scene.hpp>

#include <projectunity/core/Log.hpp>

#include "SceneScriptSerialization.hpp"

#include <fstream>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <sstream>
#include <unordered_map>

namespace projectunity::scene {
namespace {

constexpr int kSceneFormatVersion = 1;

void setError(std::string* errorMessage, std::string message);

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

[[nodiscard]] nlohmann::json colorToJson(const std::array<float, 3>& value)
{
    return nlohmann::json::array({value[0], value[1], value[2]});
}

[[nodiscard]] bool colorFromJson(const nlohmann::json& json, std::array<float, 3>& output)
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

[[nodiscard]] const char* lightTypeToString(LightComponentType type) noexcept
{
    switch (type) {
    case LightComponentType::Directional:
        return "directional";
    case LightComponentType::Point:
        return "point";
    case LightComponentType::Spot:
        return "spot";
    }
    return "directional";
}

[[nodiscard]] bool lightTypeFromJson(const nlohmann::json& json, LightComponentType& output)
{
    if (!json.is_string()) {
        return false;
    }
    const auto type = json.get<std::string>();
    if (type == "directional") {
        output = LightComponentType::Directional;
        return true;
    }
    if (type == "point") {
        output = LightComponentType::Point;
        return true;
    }
    if (type == "spot") {
        output = LightComponentType::Spot;
        return true;
    }
    return false;
}

[[nodiscard]] const char* cameraProjectionToString(CameraComponentProjection projection) noexcept
{
    switch (projection) {
    case CameraComponentProjection::Perspective:
        return "perspective";
    case CameraComponentProjection::Orthographic:
        return "orthographic";
    }
    return "perspective";
}

[[nodiscard]] bool cameraProjectionFromJson(const nlohmann::json& json, CameraComponentProjection& output)
{
    if (!json.is_string()) {
        return false;
    }
    const auto projection = json.get<std::string>();
    if (projection == "perspective") {
        output = CameraComponentProjection::Perspective;
        return true;
    }
    if (projection == "orthographic") {
        output = CameraComponentProjection::Orthographic;
        return true;
    }
    return false;
}

[[nodiscard]] const char* runtimePhysicsModeToString(RuntimePhysicsMode mode) noexcept
{
    switch (mode) {
    case RuntimePhysicsMode::None:
        return "none";
    case RuntimePhysicsMode::StaticCollider:
        return "staticCollider";
    case RuntimePhysicsMode::RigidBody:
        return "rigidBody";
    }
    return "none";
}

[[nodiscard]] bool runtimePhysicsModeFromJson(const nlohmann::json& json, RuntimePhysicsMode& output)
{
    if (!json.is_string()) {
        return false;
    }
    const auto mode = json.get<std::string>();
    if (mode == "none") {
        output = RuntimePhysicsMode::None;
        return true;
    }
    if (mode == "staticCollider") {
        output = RuntimePhysicsMode::StaticCollider;
        return true;
    }
    if (mode == "rigidBody") {
        output = RuntimePhysicsMode::RigidBody;
        return true;
    }
    return false;
}

[[nodiscard]] const char* terrainNoiseTypeToString(terrain::TerrainNoiseType type) noexcept
{
    return type == terrain::TerrainNoiseType::Ridged ? "ridged" : "value";
}

[[nodiscard]] bool terrainNoiseTypeFromJson(const nlohmann::json& json, terrain::TerrainNoiseType& output)
{
    if (!json.is_string()) {
        return false;
    }
    const auto type = json.get<std::string>();
    if (type == "value") {
        output = terrain::TerrainNoiseType::Value;
        return true;
    }
    if (type == "ridged") {
        output = terrain::TerrainNoiseType::Ridged;
        return true;
    }
    return false;
}

[[nodiscard]] const char* colliderShapeToString(ColliderShape shape) noexcept
{
    switch (shape) {
    case ColliderShape::Box:
        return "box";
    case ColliderShape::Sphere:
        return "sphere";
    case ColliderShape::Capsule:
        return "capsule";
    case ColliderShape::Mesh:
        return "mesh";
    case ColliderShape::Terrain:
        return "terrain";
    }
    return "box";
}

[[nodiscard]] bool colliderShapeFromJson(const nlohmann::json& json, ColliderShape& output)
{
    if (!json.is_string()) {
        return false;
    }
    const auto shape = json.get<std::string>();
    if (shape == "box") {
        output = ColliderShape::Box;
        return true;
    }
    if (shape == "sphere") {
        output = ColliderShape::Sphere;
        return true;
    }
    if (shape == "capsule") {
        output = ColliderShape::Capsule;
        return true;
    }
    if (shape == "mesh") {
        output = ColliderShape::Mesh;
        return true;
    }
    if (shape == "terrain") {
        output = ColliderShape::Terrain;
        return true;
    }
    return false;
}

[[nodiscard]] nlohmann::json vec2ToJson(const std::array<float, 2>& value)
{
    return nlohmann::json::array({value[0], value[1]});
}

[[nodiscard]] bool vec2FromJson(const nlohmann::json& json, std::array<float, 2>& output)
{
    if (!json.is_array() || json.size() != 2U || !json.at(0).is_number() || !json.at(1).is_number()) {
        return false;
    }
    output = {json.at(0).get<float>(), json.at(1).get<float>()};
    return true;
}

[[nodiscard]] nlohmann::json assetSlotReferenceToJson(const AssetSlotReference& reference)
{
    nlohmann::json output {
        {"assetId", reference.assetId.value()},
    };
    if (reference.subAssetIndex.has_value()) {
        output["subAssetIndex"] = *reference.subAssetIndex;
    }
    return output;
}

[[nodiscard]] bool assetSlotReferenceFromJson(const nlohmann::json& json, AssetSlotReference& output)
{
    if (!json.is_object() || !json.contains("assetId")) {
        return false;
    }
    output.assetId = core::StableId(json.value("assetId", std::uint64_t {0}));
    output.subAssetIndex = std::nullopt;
    if (json.contains("subAssetIndex")) {
        output.subAssetIndex = json.at("subAssetIndex").get<std::uint32_t>();
    }
    return output.assetId.isValid();
}

void writeOptionalReference(nlohmann::json& json, const char* key, const AssetSlotReference& reference)
{
    if (reference.isValid()) {
        json[key] = assetSlotReferenceToJson(reference);
    }
}

[[nodiscard]] bool readOptionalReference(
    const nlohmann::json& json,
    const char* key,
    AssetSlotReference& output,
    std::string* errorMessage)
{
    output = {};
    if (!json.contains(key)) {
        return true;
    }
    if (!assetSlotReferenceFromJson(json.at(key), output)) {
        setError(errorMessage, std::string("Scene material override reference is invalid: ") + key);
        return false;
    }
    return true;
}

[[nodiscard]] nlohmann::json materialSlotOverrideToJson(const MaterialSlotOverride& slot)
{
    nlohmann::json output {
        {"slotIndex", slot.slotIndex},
        {"overrideEnabled", slot.overrideEnabled},
        {"tiling", vec2ToJson(slot.tiling)},
        {"offset", vec2ToJson(slot.offset)},
    };
    if (slot.sourceMaterialIndex.has_value()) {
        output["sourceMaterialIndex"] = *slot.sourceMaterialIndex;
    }
    writeOptionalReference(output, "material", slot.material);
    writeOptionalReference(output, "baseColorTexture", slot.baseColorTexture.texture);
    writeOptionalReference(output, "normalTexture", slot.normalTexture.texture);
    writeOptionalReference(output, "metallicRoughnessTexture", slot.metallicRoughnessTexture.texture);
    writeOptionalReference(output, "emissiveTexture", slot.emissiveTexture.texture);
    return output;
}

[[nodiscard]] bool materialSlotOverrideFromJson(
    const nlohmann::json& json,
    MaterialSlotOverride& output,
    std::string* errorMessage)
{
    if (!json.is_object()) {
        setError(errorMessage, "Scene material override slot must be an object");
        return false;
    }
    output.slotIndex = json.value("slotIndex", std::uint32_t {0});
    output.sourceMaterialIndex = std::nullopt;
    if (json.contains("sourceMaterialIndex")) {
        output.sourceMaterialIndex = json.at("sourceMaterialIndex").get<std::uint32_t>();
    }
    output.overrideEnabled = json.value("overrideEnabled", true);
    if (json.contains("tiling") && !vec2FromJson(json.at("tiling"), output.tiling)) {
        setError(errorMessage, "Scene material override tiling is invalid");
        return false;
    }
    if (json.contains("offset") && !vec2FromJson(json.at("offset"), output.offset)) {
        setError(errorMessage, "Scene material override offset is invalid");
        return false;
    }
    if (!readOptionalReference(json, "material", output.material, errorMessage)
        || !readOptionalReference(json, "baseColorTexture", output.baseColorTexture.texture, errorMessage)
        || !readOptionalReference(json, "normalTexture", output.normalTexture.texture, errorMessage)
        || !readOptionalReference(json, "metallicRoughnessTexture", output.metallicRoughnessTexture.texture, errorMessage)
        || !readOptionalReference(json, "emissiveTexture", output.emissiveTexture.texture, errorMessage)) {
        return false;
    }
    return output.tiling[0] > 0.0F && output.tiling[1] > 0.0F;
}

[[nodiscard]] nlohmann::json rangeToJson(const std::array<float, 2>& value)
{
    return nlohmann::json::array({value[0], value[1]});
}

[[nodiscard]] bool rangeFromJson(const nlohmann::json& json, std::array<float, 2>& output)
{
    if (!json.is_array() || json.size() != 2U || !json.at(0).is_number() || !json.at(1).is_number()) {
        return false;
    }
    output = {json.at(0).get<float>(), json.at(1).get<float>()};
    return output[0] <= output[1];
}

[[nodiscard]] nlohmann::json terrainSettingsToJson(const terrain::TerrainSettings& settings)
{
    return {
        {"width", settings.width},
        {"length", settings.length},
        {"heightScale", settings.heightScale},
        {"resolution", settings.resolution},
        {"chunkSize", settings.chunkSize},
        {"seed", settings.seed},
        {"noiseType", terrainNoiseTypeToString(settings.noiseType)},
        {"frequency", settings.frequency},
        {"octaves", settings.octaves},
        {"persistence", settings.persistence},
        {"lacunarity", settings.lacunarity},
        {"generateNormals", settings.generateNormals},
        {"generateTangents", settings.generateTangents},
        {"lodLevels", settings.lodLevels},
        {"generateCollider", settings.generateCollider},
    };
}

[[nodiscard]] bool terrainSettingsFromJson(const nlohmann::json& json, terrain::TerrainSettings& output)
{
    if (!json.is_object() || !terrainNoiseTypeFromJson(json.at("noiseType"), output.noiseType)) {
        return false;
    }
    output.width = json.value("width", output.width);
    output.length = json.value("length", output.length);
    output.heightScale = json.value("heightScale", output.heightScale);
    output.resolution = json.value("resolution", output.resolution);
    output.chunkSize = json.value("chunkSize", output.chunkSize);
    output.seed = json.value("seed", output.seed);
    output.frequency = json.value("frequency", output.frequency);
    output.octaves = json.value("octaves", output.octaves);
    output.persistence = json.value("persistence", output.persistence);
    output.lacunarity = json.value("lacunarity", output.lacunarity);
    output.generateNormals = json.value("generateNormals", output.generateNormals);
    output.generateTangents = json.value("generateTangents", output.generateTangents);
    output.lodLevels = json.value("lodLevels", output.lodLevels);
    output.generateCollider = json.value("generateCollider", output.generateCollider);
    return output.width > 0.0F && output.length > 0.0F && output.heightScale >= 0.0F
        && output.resolution >= 2U && output.chunkSize > 0U && output.octaves > 0U;
}

[[nodiscard]] nlohmann::json terrainLayerToJson(const terrain::TerrainMaterialLayer& layer)
{
    return {
        {"name", layer.name},
        {"baseColorTextureId", layer.baseColorTextureId.value()},
        {"normalTextureId", layer.normalTextureId.value()},
        {"metallic", layer.metallic},
        {"roughness", layer.roughness},
        {"tiling", layer.tiling},
        {"strength", layer.strength},
        {"heightRange", rangeToJson(layer.heightRange)},
        {"slopeRange", rangeToJson(layer.slopeRange)},
    };
}

[[nodiscard]] bool terrainLayerFromJson(const nlohmann::json& json, terrain::TerrainMaterialLayer& output)
{
    if (!json.is_object()) {
        return false;
    }
    output.name = json.value("name", std::string {"Layer"});
    output.baseColorTextureId = core::StableId(json.value("baseColorTextureId", std::uint64_t {0}));
    output.normalTextureId = core::StableId(json.value("normalTextureId", std::uint64_t {0}));
    output.metallic = json.value("metallic", output.metallic);
    output.roughness = json.value("roughness", output.roughness);
    output.tiling = json.value("tiling", output.tiling);
    output.strength = json.value("strength", output.strength);
    return !output.name.empty()
        && output.tiling > 0.0F
        && output.strength >= 0.0F
        && rangeFromJson(json.at("heightRange"), output.heightRange)
        && rangeFromJson(json.at("slopeRange"), output.slopeRange);
}

[[nodiscard]] nlohmann::json tilemapToJson(const TilemapComponent& tilemap)
{
    return {
        {"width", tilemap.width},
        {"height", tilemap.height},
        {"tileSize", tilemap.tileSize},
        {"tileIds", tilemap.tileIds},
    };
}

[[nodiscard]] bool tilemapFromJson(
    const nlohmann::json& json,
    TilemapComponent& output,
    std::string* errorMessage)
{
    if (!json.is_object()) {
        setError(errorMessage, "Scene tilemap must be an object");
        return false;
    }

    output.width = json.value("width", output.width);
    output.height = json.value("height", output.height);
    output.tileSize = json.value("tileSize", output.tileSize);

    if (output.width == 0U || output.height == 0U || output.width > 1024U || output.height > 1024U
        || !(output.tileSize > 0.0F)) {
        setError(errorMessage, "Scene tilemap dimensions are invalid");
        return false;
    }
    if (!json.contains("tileIds") || !json.at("tileIds").is_array()) {
        setError(errorMessage, "Scene tilemap tileIds must be an array");
        return false;
    }

    output.tileIds = json.at("tileIds").get<std::vector<std::int32_t>>();
    if (output.tileIds.size() != output.cellCount()) {
        setError(errorMessage, "Scene tilemap tileIds size does not match width*height");
        return false;
    }
    if (std::any_of(output.tileIds.begin(), output.tileIds.end(), [](std::int32_t tileId) {
            return tileId < -1;
        })) {
        setError(errorMessage, "Scene tilemap tileIds contain an invalid id");
        return false;
    }
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
                    {"renderable", entity.meshRenderer->renderable},
                    {"runtimeCook", {
                        {"staticBatchable", entity.meshRenderer->runtimeCook.staticBatchable},
                        {"mutable", entity.meshRenderer->runtimeCook.mutableRuntime},
                        {"physics", runtimePhysicsModeToString(entity.meshRenderer->runtimeCook.physics)},
                        {"grabbable", entity.meshRenderer->runtimeCook.grabbable},
                    }},
                };
                if (entity.meshRenderer->primitiveInstanceIndex.has_value()) {
                    item["meshRenderer"]["primitiveInstanceIndex"] = *entity.meshRenderer->primitiveInstanceIndex;
                }
                if (entity.meshRenderer->editorInstanceIndex.has_value()) {
                    item["meshRenderer"]["editorInstanceIndex"] = *entity.meshRenderer->editorInstanceIndex;
                }
            }
            if (entity.materialOverrides.has_value() && !entity.materialOverrides->slots.empty()) {
                nlohmann::json slots = nlohmann::json::array();
                for (const auto& slot : entity.materialOverrides->slots) {
                    slots.push_back(materialSlotOverrideToJson(slot));
                }
                item["materialOverrides"] = {
                    {"slots", std::move(slots)},
                };
            }
            if (entity.light.has_value()) {
                item["light"] = {
                    {"enabled", entity.light->enabled},
                    {"castsShadow", entity.light->castsShadow},
                    {"type", lightTypeToString(entity.light->type)},
                    {"direction", vecToJson(entity.light->direction)},
                    {"color", colorToJson(entity.light->color)},
                    {"intensity", entity.light->intensity},
                    {"range", entity.light->range},
                    {"linearAttenuation", entity.light->linearAttenuation},
                    {"quadraticAttenuation", entity.light->quadraticAttenuation},
                    {"innerConeAngle", entity.light->innerConeAngle},
                    {"outerConeAngle", entity.light->outerConeAngle},
                };
            }
            if (entity.camera.has_value()) {
                item["camera"] = {
                    {"projection", cameraProjectionToString(entity.camera->projection)},
                    {"direction", vecToJson(entity.camera->direction)},
                    {"right", vecToJson(entity.camera->right)},
                    {"up", vecToJson(entity.camera->up)},
                    {"verticalFovRadians", entity.camera->verticalFovRadians},
                    {"aspectRatio", entity.camera->aspectRatio},
                    {"xMagnitude", entity.camera->xMagnitude},
                    {"yMagnitude", entity.camera->yMagnitude},
                    {"nearPlane", entity.camera->nearPlane},
                    {"farPlane", entity.camera->farPlane},
                };
            }
            if (!entity.scripts.empty()) {
                item["scripts"] = serialization::scriptsToJson(entity.scripts);
            }
            if (entity.terrain.has_value()) {
                nlohmann::json layers = nlohmann::json::array();
                for (const auto& layer : entity.terrain->materialLayers) {
                    layers.push_back(terrainLayerToJson(layer));
                }
                item["terrain"] = {
                    {"settings", terrainSettingsToJson(entity.terrain->settings)},
                    {"generatedModelAssetId", entity.terrain->generatedModelAssetId.value()},
                    {"materialLayers", std::move(layers)},
                    {"heightmap", entity.terrain->heightmap},
                };
            }
            if (entity.tilemap.has_value()) {
                item["tilemap"] = tilemapToJson(*entity.tilemap);
            }
            if (entity.rigidbody.has_value()) {
                item["rigidbody"] = {
                    {"enabled", entity.rigidbody->enabled},
                    {"mass", entity.rigidbody->mass},
                    {"linearDrag", entity.rigidbody->linearDrag},
                    {"angularDrag", entity.rigidbody->angularDrag},
                    {"useGravity", entity.rigidbody->useGravity},
                    {"kinematic", entity.rigidbody->kinematic},
                    {"partial", true},
                };
            }
            if (entity.collider.has_value()) {
                item["collider"] = {
                    {"enabled", entity.collider->enabled},
                    {"shape", colliderShapeToString(entity.collider->shape)},
                    {"size", vecToJson(entity.collider->size)},
                    {"offset", vecToJson(entity.collider->offset)},
                    {"radius", entity.collider->radius},
                    {"height", entity.collider->height},
                    {"trigger", entity.collider->trigger},
                    {"partial", true},
                };
            }
            item["componentOrder"] = serialization::componentOrderToJson(entity.componentOrder);
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
        std::unordered_map<std::uint64_t, bool> hasSerializedComponentOrder;

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
                if (meshRendererJson.contains("primitiveInstanceIndex")) {
                    meshRenderer.primitiveInstanceIndex = meshRendererJson.at("primitiveInstanceIndex").get<std::uint32_t>();
                }
                if (meshRendererJson.contains("editorInstanceIndex")) {
                    meshRenderer.editorInstanceIndex = meshRendererJson.at("editorInstanceIndex").get<std::uint32_t>();
                }
                meshRenderer.renderable = meshRendererJson.value("renderable", true);
                if (meshRendererJson.contains("runtimeCook")) {
                    const auto& runtimeJson = meshRendererJson.at("runtimeCook");
                    if (!runtimeJson.is_object()) {
                        setError(errorMessage, "Scene mesh renderer runtimeCook must be an object");
                        return false;
                    }
                    meshRenderer.runtimeCook.staticBatchable = runtimeJson.value("staticBatchable", true);
                    meshRenderer.runtimeCook.mutableRuntime = runtimeJson.value("mutable", false);
                    meshRenderer.runtimeCook.grabbable = runtimeJson.value("grabbable", false);
                    if (runtimeJson.contains("physics")
                        && !runtimePhysicsModeFromJson(runtimeJson.at("physics"), meshRenderer.runtimeCook.physics)) {
                        setError(errorMessage, "Scene mesh renderer runtimeCook physics mode is invalid");
                        return false;
                    }
                }
                entity.meshRenderer = meshRenderer;
            }

            if (item.contains("materialOverrides")) {
                const auto& overridesJson = item.at("materialOverrides");
                if (!overridesJson.is_object() || !overridesJson.contains("slots") || !overridesJson.at("slots").is_array()) {
                    setError(errorMessage, "Scene material overrides must contain a slots array");
                    return false;
                }
                MaterialOverrideComponent overrides;
                for (const auto& slotJson : overridesJson.at("slots")) {
                    MaterialSlotOverride slot;
                    if (!materialSlotOverrideFromJson(slotJson, slot, errorMessage)) {
                        return false;
                    }
                    overrides.slots.push_back(std::move(slot));
                }
                entity.materialOverrides = std::move(overrides);
            }

            if (item.contains("light")) {
                const auto& lightJson = item.at("light");
                if (!lightJson.is_object()) {
                    setError(errorMessage, "Scene light must be an object");
                    return false;
                }
                LightComponent light;
                if (!lightTypeFromJson(lightJson.at("type"), light.type)
                    || !vecFromJson(lightJson.at("direction"), light.direction)
                    || !colorFromJson(lightJson.at("color"), light.color)) {
                    setError(errorMessage, "Scene light data is invalid");
                    return false;
                }
                light.intensity = lightJson.value("intensity", light.intensity);
                light.enabled = lightJson.value("enabled", light.enabled);
                light.castsShadow = lightJson.value("castsShadow", light.castsShadow);
                light.range = lightJson.value("range", light.range);
                light.linearAttenuation = lightJson.value("linearAttenuation", light.linearAttenuation);
                light.quadraticAttenuation = lightJson.value("quadraticAttenuation", light.quadraticAttenuation);
                light.innerConeAngle = lightJson.value("innerConeAngle", light.innerConeAngle);
                light.outerConeAngle = lightJson.value("outerConeAngle", light.outerConeAngle);
                entity.light = light;
            }

            if (item.contains("camera")) {
                const auto& cameraJson = item.at("camera");
                if (!cameraJson.is_object()) {
                    setError(errorMessage, "Scene camera must be an object");
                    return false;
                }
                CameraComponent camera;
                if (!cameraProjectionFromJson(cameraJson.at("projection"), camera.projection)
                    || !vecFromJson(cameraJson.at("direction"), camera.direction)
                    || !vecFromJson(cameraJson.at("right"), camera.right)
                    || !vecFromJson(cameraJson.at("up"), camera.up)) {
                    setError(errorMessage, "Scene camera data is invalid");
                    return false;
                }
                camera.verticalFovRadians = cameraJson.value("verticalFovRadians", camera.verticalFovRadians);
                camera.aspectRatio = cameraJson.value("aspectRatio", camera.aspectRatio);
                camera.xMagnitude = cameraJson.value("xMagnitude", camera.xMagnitude);
                camera.yMagnitude = cameraJson.value("yMagnitude", camera.yMagnitude);
                camera.nearPlane = cameraJson.value("nearPlane", camera.nearPlane);
                camera.farPlane = cameraJson.value("farPlane", camera.farPlane);
                if ((camera.projection == CameraComponentProjection::Perspective && camera.nearPlane <= 0.0F)
                    || camera.nearPlane < 0.0F
                    || camera.farPlane <= camera.nearPlane) {
                    setError(errorMessage, "Scene camera clipping planes are invalid");
                    return false;
                }
                entity.camera = camera;
            }

            std::string scriptError;
            if (!serialization::scriptsFromJson(item, entity.scripts, scriptError)) {
                setError(errorMessage, std::move(scriptError));
                return false;
            }
            if (item.contains("componentOrder")
                && !serialization::componentOrderFromJson(item.at("componentOrder"), entity.componentOrder, scriptError)) {
                setError(errorMessage, std::move(scriptError));
                return false;
            }
            hasSerializedComponentOrder.emplace(entity.id.value(), item.contains("componentOrder"));

            if (item.contains("terrain")) {
                const auto& terrainJson = item.at("terrain");
                if (!terrainJson.is_object()) {
                    setError(errorMessage, "Scene terrain must be an object");
                    return false;
                }
                TerrainComponent terrainComponent;
                if (!terrainSettingsFromJson(terrainJson.at("settings"), terrainComponent.settings)
                    || !terrainJson.at("materialLayers").is_array()) {
                    setError(errorMessage, "Scene terrain settings are invalid");
                    return false;
                }
                terrainComponent.generatedModelAssetId = core::StableId(terrainJson.value("generatedModelAssetId", std::uint64_t {0}));
                if (terrainJson.contains("heightmap")) {
                    const auto& heightmapJson = terrainJson.at("heightmap");
                    if (!heightmapJson.is_array()) {
                        setError(errorMessage, "Scene terrain heightmap must be an array");
                        return false;
                    }
                    terrainComponent.heightmap = heightmapJson.get<std::vector<float>>();
                    const auto expectedHeightCount = static_cast<std::size_t>(terrainComponent.settings.resolution)
                        * terrainComponent.settings.resolution;
                    if (!terrainComponent.heightmap.empty() && terrainComponent.heightmap.size() != expectedHeightCount) {
                        setError(errorMessage, "Scene terrain heightmap size does not match resolution");
                        return false;
                    }
                }
                terrainComponent.materialLayers.clear();
                for (const auto& layerJson : terrainJson.at("materialLayers")) {
                    terrain::TerrainMaterialLayer layer;
                    if (!terrainLayerFromJson(layerJson, layer)) {
                        setError(errorMessage, "Scene terrain material layer is invalid");
                        return false;
                    }
                    terrainComponent.materialLayers.push_back(std::move(layer));
                }
                if (terrainComponent.materialLayers.empty() || terrainComponent.materialLayers.size() > 8U) {
                    setError(errorMessage, "Scene terrain must have one to eight material layers");
                    return false;
                }
                entity.terrain = std::move(terrainComponent);
            }

            if (item.contains("tilemap")) {
                TilemapComponent tilemapComponent;
                if (!tilemapFromJson(item.at("tilemap"), tilemapComponent, errorMessage)) {
                    return false;
                }
                entity.tilemap = std::move(tilemapComponent);
            }

            if (item.contains("rigidbody")) {
                const auto& rigidbodyJson = item.at("rigidbody");
                if (!rigidbodyJson.is_object()) {
                    setError(errorMessage, "Scene rigidbody must be an object");
                    return false;
                }
                RigidbodyComponent rigidbody;
                rigidbody.enabled = rigidbodyJson.value("enabled", rigidbody.enabled);
                rigidbody.mass = rigidbodyJson.value("mass", rigidbody.mass);
                rigidbody.linearDrag = rigidbodyJson.value("linearDrag", rigidbody.linearDrag);
                rigidbody.angularDrag = rigidbodyJson.value("angularDrag", rigidbody.angularDrag);
                rigidbody.useGravity = rigidbodyJson.value("useGravity", rigidbody.useGravity);
                rigidbody.kinematic = rigidbodyJson.value("kinematic", rigidbody.kinematic);
                if (!(rigidbody.mass > 0.0F) || rigidbody.linearDrag < 0.0F || rigidbody.angularDrag < 0.0F) {
                    setError(errorMessage, "Scene rigidbody values are invalid");
                    return false;
                }
                entity.rigidbody = rigidbody;
            }

            if (item.contains("collider")) {
                const auto& colliderJson = item.at("collider");
                if (!colliderJson.is_object()) {
                    setError(errorMessage, "Scene collider must be an object");
                    return false;
                }
                ColliderComponent collider;
                if (!colliderShapeFromJson(colliderJson.at("shape"), collider.shape)
                    || !vecFromJson(colliderJson.at("size"), collider.size)) {
                    setError(errorMessage, "Scene collider shape or size is invalid");
                    return false;
                }
                collider.enabled = colliderJson.value("enabled", collider.enabled);
                if (colliderJson.contains("offset") && !vecFromJson(colliderJson.at("offset"), collider.offset)) {
                    setError(errorMessage, "Scene collider offset is invalid");
                    return false;
                }
                collider.radius = colliderJson.value("radius", collider.radius);
                collider.height = colliderJson.value("height", collider.height);
                collider.trigger = colliderJson.value("trigger", collider.trigger);
                if (collider.size.x <= 0.0F || collider.size.y <= 0.0F || collider.size.z <= 0.0F
                    || collider.radius <= 0.0F || collider.height <= 0.0F) {
                    setError(errorMessage, "Scene collider values are invalid");
                    return false;
                }
                entity.collider = collider;
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
        std::uint64_t nextScriptId = 1;
        std::unordered_map<std::uint64_t, bool> usedScriptIds;
        for (const auto& entity : entities_) {
            for (const auto& script : entity.scripts) {
                if (script.instanceId.isValid()) {
                    nextScriptId = std::max(nextScriptId, script.instanceId.value() + 1);
                }
            }
        }
        for (auto& entity : entities_) {
            for (auto& script : entity.scripts) {
                if (!script.instanceId.isValid()) {
                    while (usedScriptIds.contains(nextScriptId)) {
                        ++nextScriptId;
                    }
                    script.instanceId = ScriptInstanceId(nextScriptId++);
                }
                if (!usedScriptIds.emplace(script.instanceId.value(), true).second) {
                    setError(errorMessage, "Scene contains duplicate script instance ids");
                    entities_.clear();
                    return false;
                }
            }
            if (!hasSerializedComponentOrder[entity.id.value()]) {
                rebuildComponentOrder(entity);
                continue;
            }
            if (entity.componentOrder.empty()
                || entity.componentOrder.front().type != ComponentType::Transform) {
                setError(errorMessage, "Scene component order must begin with Transform");
                entities_.clear();
                return false;
            }
            for (const auto& entry : entity.componentOrder) {
                const bool present = entry.type == ComponentType::Transform
                    || (entry.type == ComponentType::MeshRenderer && entity.meshRenderer.has_value())
                    || (entry.type == ComponentType::MaterialOverrides && entity.materialOverrides.has_value())
                    || (entry.type == ComponentType::Light && entity.light.has_value())
                    || (entry.type == ComponentType::Camera && entity.camera.has_value())
                    || (entry.type == ComponentType::Script && findScript(entity, entry.scriptInstanceId) != nullptr)
                    || (entry.type == ComponentType::Terrain && entity.terrain.has_value())
                    || (entry.type == ComponentType::Tilemap && entity.tilemap.has_value())
                    || (entry.type == ComponentType::Rigidbody && entity.rigidbody.has_value())
                    || (entry.type == ComponentType::Collider && entity.collider.has_value());
                if (!present) {
                    setError(errorMessage, "Scene component order references a missing component");
                    entities_.clear();
                    return false;
                }
            }
        }
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

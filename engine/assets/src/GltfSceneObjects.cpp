#include "GltfSceneObjects.hpp"

#include "AssetImportUtils.hpp"

#include <tiny_gltf.h>

#include <algorithm>
#include <cmath>
#include <string_view>

namespace projectunity::assets::detail {
namespace {

constexpr float kDirectionEpsilon = 0.000001F;

[[nodiscard]] bool normalize(math::Vec3& value)
{
    const auto length = value.length();
    if (length <= kDirectionEpsilon || !std::isfinite(length)) {
        return false;
    }
    value = value / length;
    return true;
}

[[nodiscard]] std::array<float, 3> lightColor(const tinygltf::Light& source)
{
    if (source.color.size() != 3U) {
        return {1.0F, 1.0F, 1.0F};
    }
    return {
        std::max(0.0F, static_cast<float>(source.color[0])),
        std::max(0.0F, static_cast<float>(source.color[1])),
        std::max(0.0F, static_cast<float>(source.color[2])),
    };
}

[[nodiscard]] bool importLight(
    const tinygltf::Model& gltf,
    const tinygltf::Node& node,
    GltfMatrix4 worldTransform,
    std::vector<ImportedLightAsset>& lights,
    std::string* errorMessage)
{
    if (node.light < 0) {
        return true;
    }
    if (static_cast<std::size_t>(node.light) >= gltf.lights.size()) {
        setError(errorMessage, "glTF light node references an invalid KHR_lights_punctual light");
        return false;
    }

    const auto& source = gltf.lights[static_cast<std::size_t>(node.light)];
    ImportedLightAsset light;
    light.name = source.name.empty() ? "Imported Light" : source.name;
    light.position = transformGltfPoint(worldTransform, {});
    light.direction = transformGltfVector(worldTransform, {0.0F, 0.0F, -1.0F});
    if (!normalize(light.direction)) {
        light.direction = {0.0F, 0.0F, -1.0F};
    }
    light.color = lightColor(source);
    light.intensity = std::max(0.0F, static_cast<float>(source.intensity));
    light.range = std::max(0.0F, static_cast<float>(source.range));
    light.innerConeAngle = std::clamp(static_cast<float>(source.spot.innerConeAngle), 0.0F, 1.57079632679F);
    light.outerConeAngle = std::clamp(static_cast<float>(source.spot.outerConeAngle), light.innerConeAngle, 1.57079632679F);
    if (source.type == "directional") {
        light.type = ImportedLightType::Directional;
    } else if (source.type == "point") {
        light.type = ImportedLightType::Point;
    } else if (source.type == "spot") {
        light.type = ImportedLightType::Spot;
    } else {
        setError(errorMessage, "glTF KHR_lights_punctual light has an unsupported type");
        return false;
    }
    lights.push_back(std::move(light));
    return true;
}

[[nodiscard]] bool importCamera(
    const tinygltf::Model& gltf,
    const tinygltf::Node& node,
    GltfMatrix4 worldTransform,
    std::vector<ImportedCameraAsset>& cameras,
    std::string* errorMessage)
{
    if (node.camera < 0) {
        return true;
    }
    if (static_cast<std::size_t>(node.camera) >= gltf.cameras.size()) {
        setError(errorMessage, "glTF camera node references an invalid camera");
        return false;
    }

    const auto& source = gltf.cameras[static_cast<std::size_t>(node.camera)];
    ImportedCameraAsset camera;
    camera.name = source.name.empty() ? "Imported Camera" : source.name;
    camera.position = transformGltfPoint(worldTransform, {});
    camera.direction = transformGltfVector(worldTransform, {0.0F, 0.0F, -1.0F});
    camera.up = transformGltfVector(worldTransform, {0.0F, 1.0F, 0.0F});
    if (!normalize(camera.direction)) {
        camera.direction = {0.0F, 0.0F, -1.0F};
    }
    if (!normalize(camera.up)) {
        camera.up = {0.0F, 1.0F, 0.0F};
    }

    if (source.type == "perspective") {
        camera.projection = ImportedCameraProjection::Perspective;
        camera.verticalFovRadians = static_cast<float>(source.perspective.yfov);
        camera.aspectRatio = std::max(0.0F, static_cast<float>(source.perspective.aspectRatio));
        camera.nearPlane = static_cast<float>(source.perspective.znear);
        camera.farPlane = source.perspective.zfar > source.perspective.znear
            ? static_cast<float>(source.perspective.zfar)
            : camera.farPlane;
        if (camera.verticalFovRadians <= 0.0F || camera.nearPlane <= 0.0F) {
            setError(errorMessage, "glTF perspective camera parameters are invalid");
            return false;
        }
    } else if (source.type == "orthographic") {
        camera.projection = ImportedCameraProjection::Orthographic;
        camera.xMagnitude = std::max(0.0001F, static_cast<float>(source.orthographic.xmag));
        camera.yMagnitude = std::max(0.0001F, static_cast<float>(source.orthographic.ymag));
        camera.nearPlane = static_cast<float>(source.orthographic.znear);
        camera.farPlane = static_cast<float>(source.orthographic.zfar);
        if (camera.nearPlane < 0.0F || camera.farPlane <= camera.nearPlane) {
            setError(errorMessage, "glTF orthographic camera parameters are invalid");
            return false;
        }
    } else {
        setError(errorMessage, "glTF camera has an unsupported projection type");
        return false;
    }
    cameras.push_back(std::move(camera));
    return true;
}

} // namespace

bool importGltfSceneObjects(
    const tinygltf::Model& gltf,
    int nodeIndex,
    GltfMatrix4 parentTransform,
    std::vector<ImportedLightAsset>& lights,
    std::vector<ImportedCameraAsset>& cameras,
    std::string* errorMessage,
    int depth)
{
    if (nodeIndex < 0 || static_cast<std::size_t>(nodeIndex) >= gltf.nodes.size() || depth > 256) {
        setError(errorMessage, "glTF scene-object hierarchy is invalid");
        return false;
    }

    const auto& node = gltf.nodes[static_cast<std::size_t>(nodeIndex)];
    const auto worldTransform = multiplyGltfMatrices(parentTransform, gltfNodeMatrix(node));
    if (!importLight(gltf, node, worldTransform, lights, errorMessage)
        || !importCamera(gltf, node, worldTransform, cameras, errorMessage)) {
        return false;
    }

    for (const auto child : node.children) {
        if (!importGltfSceneObjects(gltf, child, worldTransform, lights, cameras, errorMessage, depth + 1)) {
            return false;
        }
    }
    return true;
}

} // namespace projectunity::assets::detail

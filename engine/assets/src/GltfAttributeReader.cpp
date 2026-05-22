#include "GltfAttributeReader.hpp"

#include "AssetImportUtils.hpp"

#include <tiny_gltf.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>

namespace projectunity::assets::detail {
namespace {

[[nodiscard]] std::size_t componentCount(int type)
{
    switch (type) {
    case TINYGLTF_TYPE_SCALAR:
        return 1;
    case TINYGLTF_TYPE_VEC2:
        return 2;
    case TINYGLTF_TYPE_VEC3:
        return 3;
    case TINYGLTF_TYPE_VEC4:
        return 4;
    default:
        return 0;
    }
}

[[nodiscard]] std::size_t componentByteSize(int componentType)
{
    switch (componentType) {
    case TINYGLTF_COMPONENT_TYPE_BYTE:
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
        return 1;
    case TINYGLTF_COMPONENT_TYPE_SHORT:
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
        return 2;
    case TINYGLTF_COMPONENT_TYPE_INT:
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
    case TINYGLTF_COMPONENT_TYPE_FLOAT:
        return 4;
    default:
        return 0;
    }
}

[[nodiscard]] const std::uint8_t* accessorBytes(
    const tinygltf::Model& model,
    const tinygltf::Accessor& accessor,
    std::size_t elementIndex,
    std::string* errorMessage)
{
    if (accessor.bufferView < 0 || static_cast<std::size_t>(accessor.bufferView) >= model.bufferViews.size()) {
        setError(errorMessage, "Asset accessor references a missing buffer view");
        return nullptr;
    }

    const auto& view = model.bufferViews[static_cast<std::size_t>(accessor.bufferView)];
    if (view.buffer < 0 || static_cast<std::size_t>(view.buffer) >= model.buffers.size()) {
        setError(errorMessage, "Asset buffer view references a missing buffer");
        return nullptr;
    }

    const auto& buffer = model.buffers[static_cast<std::size_t>(view.buffer)];
    const auto packedStride = componentCount(accessor.type) * componentByteSize(accessor.componentType);
    const auto byteStride = accessor.ByteStride(view) > 0
        ? static_cast<std::size_t>(accessor.ByteStride(view))
        : packedStride;
    const auto offset = view.byteOffset + accessor.byteOffset + elementIndex * byteStride;
    if (packedStride == 0 || offset + packedStride > buffer.data.size()) {
        setError(errorMessage, "Asset accessor reads outside its buffer");
        return nullptr;
    }
    return buffer.data.data() + offset;
}

} // namespace

bool readFloatAttribute(
    const tinygltf::Model& model,
    int accessorIndex,
    std::size_t expectedComponents,
    std::vector<float>& output,
    std::string* errorMessage)
{
    if (accessorIndex < 0 || static_cast<std::size_t>(accessorIndex) >= model.accessors.size()) {
        setError(errorMessage, "Mesh attribute references a missing accessor");
        return false;
    }

    const auto& accessor = model.accessors[static_cast<std::size_t>(accessorIndex)];
    if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT
        || componentCount(accessor.type) != expectedComponents) {
        setError(errorMessage, "Mesh attribute must use float components with the expected vector width");
        return false;
    }

    output.clear();
    output.reserve(accessor.count * expectedComponents);
    for (std::size_t index = 0; index < accessor.count; ++index) {
        const auto* bytes = accessorBytes(model, accessor, index, errorMessage);
        if (bytes == nullptr) {
            return false;
        }
        for (std::size_t component = 0; component < expectedComponents; ++component) {
            float value = 0.0F;
            std::memcpy(&value, bytes + component * sizeof(float), sizeof(value));
            output.push_back(value);
        }
    }
    return true;
}

bool readColorAttribute(
    const tinygltf::Model& model,
    int accessorIndex,
    std::vector<float>& output,
    std::string* errorMessage)
{
    if (accessorIndex < 0 || static_cast<std::size_t>(accessorIndex) >= model.accessors.size()) {
        setError(errorMessage, "Mesh color attribute references a missing accessor");
        return false;
    }

    const auto& accessor = model.accessors[static_cast<std::size_t>(accessorIndex)];
    const auto colorComponents = componentCount(accessor.type);
    if (colorComponents != 3U && colorComponents != 4U) {
        setError(errorMessage, "Mesh color attribute must be VEC3 or VEC4");
        return false;
    }

    const auto componentSize = componentByteSize(accessor.componentType);
    if (componentSize == 0U) {
        setError(errorMessage, "Mesh color attribute component size is invalid");
        return false;
    }

    output.clear();
    output.reserve(accessor.count * 4U);
    for (std::size_t index = 0; index < accessor.count; ++index) {
        const auto* bytes = accessorBytes(model, accessor, index, errorMessage);
        if (bytes == nullptr) {
            return false;
        }

        std::array<float, 4> color {1.0F, 1.0F, 1.0F, 1.0F};
        for (std::size_t component = 0; component < colorComponents; ++component) {
            const auto* componentBytes = bytes + component * componentSize;
            float value = 1.0F;
            switch (accessor.componentType) {
            case TINYGLTF_COMPONENT_TYPE_FLOAT:
                std::memcpy(&value, componentBytes, sizeof(value));
                break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                value = static_cast<float>(*componentBytes) / 255.0F;
                break;
            case TINYGLTF_COMPONENT_TYPE_BYTE: {
                std::int8_t packed = 0;
                std::memcpy(&packed, componentBytes, sizeof(packed));
                value = static_cast<float>(packed) / 127.0F;
                break;
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
                std::uint16_t packed = 0;
                std::memcpy(&packed, componentBytes, sizeof(packed));
                value = static_cast<float>(packed) / 65535.0F;
                break;
            }
            case TINYGLTF_COMPONENT_TYPE_SHORT: {
                std::int16_t packed = 0;
                std::memcpy(&packed, componentBytes, sizeof(packed));
                value = static_cast<float>(packed) / 32767.0F;
                break;
            }
            default:
                setError(errorMessage, "Mesh color attribute uses an unsupported component type");
                return false;
            }
            color[component] = std::clamp(value, 0.0F, 1.0F);
        }
        output.insert(output.end(), color.begin(), color.end());
    }
    return true;
}

bool readIndices(
    const tinygltf::Model& model,
    int accessorIndex,
    std::size_t vertexCount,
    std::vector<std::uint32_t>& output,
    std::string* errorMessage)
{
    output.clear();
    if (accessorIndex < 0) {
        output.reserve(vertexCount);
        for (std::size_t index = 0; index < vertexCount; ++index) {
            output.push_back(static_cast<std::uint32_t>(index));
        }
        return true;
    }

    if (static_cast<std::size_t>(accessorIndex) >= model.accessors.size()) {
        setError(errorMessage, "Mesh indices reference a missing accessor");
        return false;
    }

    const auto& accessor = model.accessors[static_cast<std::size_t>(accessorIndex)];
    if (accessor.type != TINYGLTF_TYPE_SCALAR) {
        setError(errorMessage, "Mesh indices must use scalar accessors");
        return false;
    }

    output.reserve(accessor.count);
    for (std::size_t index = 0; index < accessor.count; ++index) {
        const auto* bytes = accessorBytes(model, accessor, index, errorMessage);
        if (bytes == nullptr) {
            return false;
        }

        std::uint32_t value = 0;
        switch (accessor.componentType) {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            value = *bytes;
            break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
            std::uint16_t indexValue = 0;
            std::memcpy(&indexValue, bytes, sizeof(indexValue));
            value = indexValue;
            break;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            std::memcpy(&value, bytes, sizeof(value));
            break;
        default:
            setError(errorMessage, "Mesh indices must use unsigned integer components");
            return false;
        }

        if (value >= vertexCount) {
            setError(errorMessage, "Mesh indices reference a missing vertex");
            return false;
        }
        output.push_back(value);
    }
    return true;
}

} // namespace projectunity::assets::detail

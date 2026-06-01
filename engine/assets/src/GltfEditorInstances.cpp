#include "GltfEditorInstances.hpp"

#include <utility>

namespace projectunity::assets::detail {

std::string gltfEditorInstanceName(
    std::string_view nodeName,
    std::string_view meshName,
    std::size_t primitiveIndex,
    std::size_t primitiveCount)
{
    auto name = !nodeName.empty() ? std::string {nodeName} : std::string {meshName};
    if (name.empty()) {
        name = "Part";
    }
    if (primitiveCount > 1U) {
        name += " Primitive " + std::to_string(primitiveIndex + 1U);
    }
    return name;
}

void appendMeshEditorInstance(ModelAsset& model, MeshPrimitiveInstance instance, std::string name)
{
    MeshEditorInstance editorInstance;
    editorInstance.name = std::move(name);
    editorInstance.sourcePrimitiveIndex = instance.primitiveIndex;
    editorInstance.transform = instance.transform;
    editorInstance.bounds = instance.bounds;
    editorInstance.flipsWinding = instance.flipsWinding;
    model.editorInstances.push_back(std::move(editorInstance));
}

} // namespace projectunity::assets::detail

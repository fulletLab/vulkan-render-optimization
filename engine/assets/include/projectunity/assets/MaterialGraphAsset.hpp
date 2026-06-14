#pragma once

#include <projectunity/core/StableId.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace projectunity::assets {

using MaterialGraphNodeId = core::StableId;
using MaterialGraphPinId = core::StableId;
using MaterialGraphConnectionId = core::StableId;
using MaterialGraphParameterId = core::StableId;

enum class MaterialDomain : std::uint8_t {
    Surface,
};

enum class MaterialGraphValueType : std::uint8_t {
    Bool,
    Int,
    Float,
    Float2,
    Float3,
    Float4,
    Color3,
    Color4,
    Texture2D,
};

enum class MaterialGraphPinDirection : std::uint8_t {
    Input,
    Output,
};

enum class MaterialGraphPreviewMesh : std::uint8_t {
    Sphere,
    Cube,
    Plane,
    CustomModel,
};

struct MaterialGraphTexture2DReference {
    core::StableId textureAssetId;
    std::uint32_t uvSet {0};

    [[nodiscard]] bool operator==(const MaterialGraphTexture2DReference&) const noexcept = default;
};

using MaterialGraphValue = std::variant<
    std::monostate,
    bool,
    std::int32_t,
    float,
    std::array<float, 2>,
    std::array<float, 3>,
    std::array<float, 4>,
    MaterialGraphTexture2DReference>;

struct MaterialGraphProperty {
    std::string key;
    MaterialGraphValue value;
};

struct MaterialGraphPin {
    MaterialGraphPinId id;
    std::string key;
    std::string displayName;
    MaterialGraphPinDirection direction {MaterialGraphPinDirection::Input};
    MaterialGraphValueType valueType {MaterialGraphValueType::Float};
    MaterialGraphValue defaultValue;
    bool allowsMultipleConnections {false};
};

struct MaterialGraphNode {
    MaterialGraphNodeId id;
    std::string typeId;
    std::uint32_t typeVersion {1};
    std::string displayName;
    std::vector<MaterialGraphPin> pins;
    std::vector<MaterialGraphProperty> properties;
};

struct MaterialGraphConnection {
    MaterialGraphConnectionId id;
    MaterialGraphPinId outputPinId;
    MaterialGraphPinId inputPinId;
};

struct MaterialGraphParameterUiMetadata {
    float minimum {0.0F};
    float maximum {1.0F};
    float step {0.01F};
    bool hasRange {false};
};

struct MaterialGraphParameter {
    MaterialGraphParameterId id;
    std::string name;
    MaterialGraphValueType valueType {MaterialGraphValueType::Float};
    MaterialGraphValue defaultValue;
    MaterialGraphPinId targetPinId;
    MaterialGraphParameterUiMetadata ui;
};

struct MaterialGraphNodeLayout {
    MaterialGraphNodeId nodeId;
    std::array<float, 2> position {};
    bool collapsed {false};
};

struct MaterialGraphEditorState {
    std::vector<MaterialGraphNodeLayout> nodes;
    std::array<float, 2> canvasPan {};
    float canvasZoom {1.0F};
};

struct MaterialGraphPreviewSettings {
    MaterialGraphPreviewMesh mesh {MaterialGraphPreviewMesh::Sphere};
    core::StableId customModelAssetId;
    core::StableId environmentTextureAssetId;
    float orbitYaw {0.0F};
    float orbitPitch {0.25F};
    float orbitDistance {3.0F};
    float exposure {1.0F};
};

struct MaterialGraphAsset {
    core::StableId id;
    std::string name;
    std::uint32_t schemaVersion {1};
    MaterialDomain domain {MaterialDomain::Surface};
    MaterialGraphNodeId outputNodeId;
    std::vector<MaterialGraphNode> nodes;
    std::vector<MaterialGraphConnection> connections;
    std::vector<MaterialGraphParameter> parameters;
    MaterialGraphEditorState editorState;
    MaterialGraphPreviewSettings preview;
};

enum class MaterialGraphDiagnosticCode : std::uint8_t {
    InvalidAssetId,
    InvalidOutputNode,
    InvalidNodeId,
    InvalidNodeType,
    DuplicateNodeId,
    InvalidPinId,
    InvalidPinKey,
    DuplicatePinId,
    InvalidPinValue,
    InvalidConnectionId,
    DuplicateConnectionId,
    MissingConnectionPin,
    InvalidConnectionDirection,
    ConnectionTypeMismatch,
    MultipleIncomingConnections,
    GraphCycle,
    InvalidParameterId,
    InvalidParameterName,
    DuplicateParameterId,
    MissingParameterPin,
    ParameterTypeMismatch,
    InvalidParameterValue,
    InvalidEditorState,
    InvalidPreviewSettings,
    LimitExceeded,
};

struct MaterialGraphDiagnostic {
    MaterialGraphDiagnosticCode code {MaterialGraphDiagnosticCode::InvalidAssetId};
    std::string message;
    core::StableId relatedId;
};

struct MaterialGraphValidationResult {
    std::vector<MaterialGraphDiagnostic> diagnostics;

    [[nodiscard]] bool valid() const noexcept
    {
        return diagnostics.empty();
    }
};

[[nodiscard]] MaterialGraphValidationResult validateMaterialGraph(const MaterialGraphAsset& graph);
[[nodiscard]] std::uint64_t materialGraphSemanticHash(const MaterialGraphAsset& graph) noexcept;
[[nodiscard]] std::uint64_t materialGraphDocumentHash(const MaterialGraphAsset& graph) noexcept;

} // namespace projectunity::assets

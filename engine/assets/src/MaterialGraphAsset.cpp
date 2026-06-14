#include <projectunity/assets/MaterialGraphAsset.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace projectunity::assets {
namespace {

constexpr std::size_t kMaxNodes = 16'384U;
constexpr std::size_t kMaxPins = 65'536U;
constexpr std::size_t kMaxConnections = 65'536U;
constexpr std::size_t kMaxParameters = 16'384U;
constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

struct PinOwner {
    const MaterialGraphPin* pin {nullptr};
    MaterialGraphNodeId nodeId;
};

class StableHasher final {
public:
    void byte(std::uint8_t value) noexcept
    {
        value_ ^= value;
        value_ *= kFnvPrime;
    }

    template <typename T>
    void integral(T value) noexcept
    {
        using Unsigned = std::make_unsigned_t<T>;
        const auto unsignedValue = static_cast<Unsigned>(value);
        for (std::size_t index = 0; index < sizeof(T); ++index) {
            byte(static_cast<std::uint8_t>(unsignedValue >> (index * 8U)));
        }
    }

    void floating(float value) noexcept
    {
        integral(std::bit_cast<std::uint32_t>(value));
    }

    void string(std::string_view value) noexcept
    {
        integral<std::uint64_t>(value.size());
        for (const auto character : value) {
            byte(static_cast<std::uint8_t>(character));
        }
    }

    [[nodiscard]] std::uint64_t value() const noexcept
    {
        return value_;
    }

private:
    std::uint64_t value_ {kFnvOffset};
};

void addDiagnostic(
    MaterialGraphValidationResult& result,
    MaterialGraphDiagnosticCode code,
    std::string message,
    core::StableId relatedId = {})
{
    result.diagnostics.push_back({code, std::move(message), relatedId});
}

[[nodiscard]] bool finiteValue(const MaterialGraphValue& value) noexcept
{
    return std::visit([](const auto& stored) {
        using Value = std::decay_t<decltype(stored)>;
        if constexpr (std::is_same_v<Value, float>) {
            return std::isfinite(stored);
        } else if constexpr (std::is_same_v<Value, std::array<float, 2>>
            || std::is_same_v<Value, std::array<float, 3>>
            || std::is_same_v<Value, std::array<float, 4>>) {
            return std::all_of(stored.begin(), stored.end(), [](float component) {
                return std::isfinite(component);
            });
        } else {
            return true;
        }
    }, value);
}

[[nodiscard]] bool valueMatchesType(
    const MaterialGraphValue& value,
    MaterialGraphValueType type) noexcept
{
    if (std::holds_alternative<std::monostate>(value)) {
        return true;
    }
    switch (type) {
    case MaterialGraphValueType::Bool:
        return std::holds_alternative<bool>(value);
    case MaterialGraphValueType::Int:
        return std::holds_alternative<std::int32_t>(value);
    case MaterialGraphValueType::Float:
        return std::holds_alternative<float>(value);
    case MaterialGraphValueType::Float2:
        return std::holds_alternative<std::array<float, 2>>(value);
    case MaterialGraphValueType::Float3:
    case MaterialGraphValueType::Color3:
        return std::holds_alternative<std::array<float, 3>>(value);
    case MaterialGraphValueType::Float4:
    case MaterialGraphValueType::Color4:
        return std::holds_alternative<std::array<float, 4>>(value);
    case MaterialGraphValueType::Texture2D:
        return std::holds_alternative<MaterialGraphTexture2DReference>(value);
    }
    return false;
}

void hashValue(StableHasher& hasher, const MaterialGraphValue& value) noexcept
{
    hasher.integral<std::uint8_t>(static_cast<std::uint8_t>(value.index()));
    std::visit([&hasher](const auto& stored) {
        using Value = std::decay_t<decltype(stored)>;
        if constexpr (std::is_same_v<Value, bool>) {
            hasher.integral<std::uint8_t>(stored ? 1U : 0U);
        } else if constexpr (std::is_same_v<Value, std::int32_t>) {
            hasher.integral(stored);
        } else if constexpr (std::is_same_v<Value, float>) {
            hasher.floating(stored);
        } else if constexpr (std::is_same_v<Value, std::array<float, 2>>
            || std::is_same_v<Value, std::array<float, 3>>
            || std::is_same_v<Value, std::array<float, 4>>) {
            for (const auto component : stored) {
                hasher.floating(component);
            }
        } else if constexpr (std::is_same_v<Value, MaterialGraphTexture2DReference>) {
            hasher.integral(stored.textureAssetId.value());
            hasher.integral(stored.uvSet);
        }
    }, value);
}

template <typename Value, typename IdFunction>
[[nodiscard]] std::vector<const Value*> sortedById(
    const std::vector<Value>& values,
    IdFunction idFunction)
{
    std::vector<const Value*> sorted;
    sorted.reserve(values.size());
    for (const auto& value : values) {
        sorted.push_back(&value);
    }
    std::sort(sorted.begin(), sorted.end(), [idFunction](const Value* lhs, const Value* rhs) {
        return idFunction(*lhs).value() < idFunction(*rhs).value();
    });
    return sorted;
}

void hashPin(StableHasher& hasher, const MaterialGraphPin& pin, bool includeDocumentData) noexcept
{
    hasher.integral(pin.id.value());
    hasher.string(pin.key);
    if (includeDocumentData) {
        hasher.string(pin.displayName);
    }
    hasher.integral(static_cast<std::uint8_t>(pin.direction));
    hasher.integral(static_cast<std::uint8_t>(pin.valueType));
    hashValue(hasher, pin.defaultValue);
    hasher.integral<std::uint8_t>(pin.allowsMultipleConnections ? 1U : 0U);
}

void hashGraphCore(
    StableHasher& hasher,
    const MaterialGraphAsset& graph,
    bool includeDocumentData) noexcept
{
    hasher.integral(graph.schemaVersion);
    hasher.integral(static_cast<std::uint8_t>(graph.domain));
    hasher.integral(graph.outputNodeId.value());
    if (includeDocumentData) {
        hasher.integral(graph.id.value());
        hasher.string(graph.name);
    }

    const auto nodes = sortedById(graph.nodes, [](const MaterialGraphNode& node) { return node.id; });
    hasher.integral<std::uint64_t>(nodes.size());
    for (const auto* node : nodes) {
        hasher.integral(node->id.value());
        hasher.string(node->typeId);
        hasher.integral(node->typeVersion);
        if (includeDocumentData) {
            hasher.string(node->displayName);
        }
        const auto pins = sortedById(node->pins, [](const MaterialGraphPin& pin) { return pin.id; });
        hasher.integral<std::uint64_t>(pins.size());
        for (const auto* pin : pins) {
            hashPin(hasher, *pin, includeDocumentData);
        }
        hasher.integral<std::uint64_t>(node->properties.size());
        std::vector<const MaterialGraphProperty*> properties;
        properties.reserve(node->properties.size());
        for (const auto& property : node->properties) {
            properties.push_back(&property);
        }
        std::sort(properties.begin(), properties.end(), [](const auto* lhs, const auto* rhs) {
            return lhs->key < rhs->key;
        });
        for (const auto* property : properties) {
            hasher.string(property->key);
            hashValue(hasher, property->value);
        }
    }

    const auto connections = sortedById(graph.connections, [](const MaterialGraphConnection& connection) {
        return connection.id;
    });
    hasher.integral<std::uint64_t>(connections.size());
    for (const auto* connection : connections) {
        hasher.integral(connection->id.value());
        hasher.integral(connection->outputPinId.value());
        hasher.integral(connection->inputPinId.value());
    }

    const auto parameters = sortedById(graph.parameters, [](const MaterialGraphParameter& parameter) {
        return parameter.id;
    });
    hasher.integral<std::uint64_t>(parameters.size());
    for (const auto* parameter : parameters) {
        hasher.integral(parameter->id.value());
        hasher.string(parameter->name);
        hasher.integral(static_cast<std::uint8_t>(parameter->valueType));
        hashValue(hasher, parameter->defaultValue);
        hasher.integral(parameter->targetPinId.value());
        hasher.floating(parameter->ui.minimum);
        hasher.floating(parameter->ui.maximum);
        hasher.floating(parameter->ui.step);
        hasher.integral<std::uint8_t>(parameter->ui.hasRange ? 1U : 0U);
    }
}

void hashDocumentState(StableHasher& hasher, const MaterialGraphAsset& graph) noexcept
{
    const auto layouts = sortedById(graph.editorState.nodes, [](const MaterialGraphNodeLayout& layout) {
        return layout.nodeId;
    });
    hasher.integral<std::uint64_t>(layouts.size());
    for (const auto* layout : layouts) {
        hasher.integral(layout->nodeId.value());
        hasher.floating(layout->position[0]);
        hasher.floating(layout->position[1]);
        hasher.integral<std::uint8_t>(layout->collapsed ? 1U : 0U);
    }
    for (const auto component : graph.editorState.canvasPan) {
        hasher.floating(component);
    }
    hasher.floating(graph.editorState.canvasZoom);
    hasher.integral(static_cast<std::uint8_t>(graph.preview.mesh));
    hasher.integral(graph.preview.customModelAssetId.value());
    hasher.integral(graph.preview.environmentTextureAssetId.value());
    hasher.floating(graph.preview.orbitYaw);
    hasher.floating(graph.preview.orbitPitch);
    hasher.floating(graph.preview.orbitDistance);
    hasher.floating(graph.preview.exposure);
}

} // namespace

MaterialGraphValidationResult validateMaterialGraph(const MaterialGraphAsset& graph)
{
    MaterialGraphValidationResult result;
    if (!graph.id.isValid()) {
        addDiagnostic(result, MaterialGraphDiagnosticCode::InvalidAssetId, "Material graph asset id is invalid");
    }
    if (graph.nodes.size() > kMaxNodes
        || graph.connections.size() > kMaxConnections
        || graph.parameters.size() > kMaxParameters) {
        addDiagnostic(result, MaterialGraphDiagnosticCode::LimitExceeded, "Material graph exceeds an asset element limit");
    }

    std::unordered_set<std::uint64_t> nodeIds;
    std::unordered_set<std::uint64_t> pinIds;
    std::unordered_set<std::uint64_t> connectionIds;
    std::unordered_set<std::uint64_t> parameterIds;
    std::unordered_map<std::uint64_t, PinOwner> pins;
    std::size_t pinCount = 0;

    for (const auto& node : graph.nodes) {
        if (!node.id.isValid()) {
            addDiagnostic(result, MaterialGraphDiagnosticCode::InvalidNodeId, "Material graph node id is invalid");
        } else if (!nodeIds.insert(node.id.value()).second) {
            addDiagnostic(result, MaterialGraphDiagnosticCode::DuplicateNodeId, "Material graph node id is duplicated", node.id);
        }
        if (node.typeId.empty()) {
            addDiagnostic(result, MaterialGraphDiagnosticCode::InvalidNodeType, "Material graph node type is empty", node.id);
        }
        pinCount += node.pins.size();
        for (const auto& pin : node.pins) {
            if (!pin.id.isValid()) {
                addDiagnostic(result, MaterialGraphDiagnosticCode::InvalidPinId, "Material graph pin id is invalid", node.id);
            } else if (!pinIds.insert(pin.id.value()).second) {
                addDiagnostic(result, MaterialGraphDiagnosticCode::DuplicatePinId, "Material graph pin id is duplicated", pin.id);
            } else {
                pins.emplace(pin.id.value(), PinOwner {&pin, node.id});
            }
            if (pin.key.empty()) {
                addDiagnostic(result, MaterialGraphDiagnosticCode::InvalidPinKey, "Material graph pin key is empty", pin.id);
            }
            if (!valueMatchesType(pin.defaultValue, pin.valueType) || !finiteValue(pin.defaultValue)) {
                addDiagnostic(result, MaterialGraphDiagnosticCode::InvalidPinValue, "Material graph pin default value is invalid", pin.id);
            }
        }
    }
    if (pinCount > kMaxPins) {
        addDiagnostic(result, MaterialGraphDiagnosticCode::LimitExceeded, "Material graph exceeds the pin limit");
    }
    if (!graph.outputNodeId.isValid() || !nodeIds.contains(graph.outputNodeId.value())) {
        addDiagnostic(result, MaterialGraphDiagnosticCode::InvalidOutputNode, "Material graph output node is missing", graph.outputNodeId);
    }

    std::unordered_map<std::uint64_t, std::size_t> incomingByPin;
    std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> outgoingByNode;
    std::unordered_map<std::uint64_t, std::size_t> indegree;
    for (const auto id : nodeIds) {
        indegree.emplace(id, 0U);
    }
    for (const auto& connection : graph.connections) {
        if (!connection.id.isValid()) {
            addDiagnostic(result, MaterialGraphDiagnosticCode::InvalidConnectionId, "Material graph connection id is invalid");
        } else if (!connectionIds.insert(connection.id.value()).second) {
            addDiagnostic(result, MaterialGraphDiagnosticCode::DuplicateConnectionId, "Material graph connection id is duplicated", connection.id);
        }
        const auto outputIt = pins.find(connection.outputPinId.value());
        const auto inputIt = pins.find(connection.inputPinId.value());
        if (outputIt == pins.end() || inputIt == pins.end()) {
            addDiagnostic(result, MaterialGraphDiagnosticCode::MissingConnectionPin, "Material graph connection references a missing pin", connection.id);
            continue;
        }
        if (outputIt->second.pin->direction != MaterialGraphPinDirection::Output
            || inputIt->second.pin->direction != MaterialGraphPinDirection::Input) {
            addDiagnostic(result, MaterialGraphDiagnosticCode::InvalidConnectionDirection, "Material graph connection direction is invalid", connection.id);
            continue;
        }
        if (outputIt->second.pin->valueType != inputIt->second.pin->valueType) {
            addDiagnostic(result, MaterialGraphDiagnosticCode::ConnectionTypeMismatch, "Material graph connection pin types do not match", connection.id);
        }
        const auto incoming = ++incomingByPin[connection.inputPinId.value()];
        if (incoming > 1U && !inputIt->second.pin->allowsMultipleConnections) {
            addDiagnostic(result, MaterialGraphDiagnosticCode::MultipleIncomingConnections, "Material graph input pin has multiple connections", connection.inputPinId);
        }
        outgoingByNode[outputIt->second.nodeId.value()].push_back(inputIt->second.nodeId.value());
        ++indegree[inputIt->second.nodeId.value()];
    }

    for (const auto& parameter : graph.parameters) {
        if (!parameter.id.isValid()) {
            addDiagnostic(result, MaterialGraphDiagnosticCode::InvalidParameterId, "Material graph parameter id is invalid");
        } else if (!parameterIds.insert(parameter.id.value()).second) {
            addDiagnostic(result, MaterialGraphDiagnosticCode::DuplicateParameterId, "Material graph parameter id is duplicated", parameter.id);
        }
        if (parameter.name.empty()) {
            addDiagnostic(result, MaterialGraphDiagnosticCode::InvalidParameterName, "Material graph parameter name is empty", parameter.id);
        }
        const auto pinIt = pins.find(parameter.targetPinId.value());
        if (pinIt == pins.end() || pinIt->second.pin->direction != MaterialGraphPinDirection::Input) {
            addDiagnostic(result, MaterialGraphDiagnosticCode::MissingParameterPin, "Material graph parameter target pin is missing", parameter.id);
        } else if (pinIt->second.pin->valueType != parameter.valueType) {
            addDiagnostic(result, MaterialGraphDiagnosticCode::ParameterTypeMismatch, "Material graph parameter type does not match its target pin", parameter.id);
        }
        if (!valueMatchesType(parameter.defaultValue, parameter.valueType)
            || !finiteValue(parameter.defaultValue)) {
            addDiagnostic(result, MaterialGraphDiagnosticCode::InvalidParameterValue, "Material graph parameter default value is invalid", parameter.id);
        }
        if (parameter.ui.hasRange
            && (!std::isfinite(parameter.ui.minimum)
                || !std::isfinite(parameter.ui.maximum)
                || !std::isfinite(parameter.ui.step)
                || parameter.ui.maximum < parameter.ui.minimum
                || parameter.ui.step <= 0.0F)) {
            addDiagnostic(result, MaterialGraphDiagnosticCode::InvalidParameterValue, "Material graph parameter UI range is invalid", parameter.id);
        }
    }

    for (const auto& layout : graph.editorState.nodes) {
        if (!nodeIds.contains(layout.nodeId.value())
            || !std::isfinite(layout.position[0])
            || !std::isfinite(layout.position[1])) {
            addDiagnostic(result, MaterialGraphDiagnosticCode::InvalidEditorState, "Material graph editor layout is invalid", layout.nodeId);
        }
    }
    if (!std::isfinite(graph.editorState.canvasPan[0])
        || !std::isfinite(graph.editorState.canvasPan[1])
        || !std::isfinite(graph.editorState.canvasZoom)
        || graph.editorState.canvasZoom <= 0.0F) {
        addDiagnostic(result, MaterialGraphDiagnosticCode::InvalidEditorState, "Material graph canvas state is invalid");
    }
    if (!std::isfinite(graph.preview.orbitYaw)
        || !std::isfinite(graph.preview.orbitPitch)
        || !std::isfinite(graph.preview.orbitDistance)
        || !std::isfinite(graph.preview.exposure)
        || graph.preview.orbitDistance <= 0.0F
        || graph.preview.exposure <= 0.0F
        || (graph.preview.mesh == MaterialGraphPreviewMesh::CustomModel
            && !graph.preview.customModelAssetId.isValid())) {
        addDiagnostic(result, MaterialGraphDiagnosticCode::InvalidPreviewSettings, "Material graph preview settings are invalid");
    }

    std::vector<std::uint64_t> ready;
    ready.reserve(indegree.size());
    for (const auto& [nodeId, degree] : indegree) {
        if (degree == 0U) {
            ready.push_back(nodeId);
        }
    }
    std::size_t visited = 0;
    while (!ready.empty()) {
        const auto nodeId = ready.back();
        ready.pop_back();
        ++visited;
        for (const auto target : outgoingByNode[nodeId]) {
            auto& degree = indegree[target];
            if (--degree == 0U) {
                ready.push_back(target);
            }
        }
    }
    if (visited != nodeIds.size()) {
        addDiagnostic(result, MaterialGraphDiagnosticCode::GraphCycle, "Material graph contains a cycle");
    }
    return result;
}

std::uint64_t materialGraphSemanticHash(const MaterialGraphAsset& graph) noexcept
{
    StableHasher hasher;
    hashGraphCore(hasher, graph, false);
    return hasher.value();
}

std::uint64_t materialGraphDocumentHash(const MaterialGraphAsset& graph) noexcept
{
    StableHasher hasher;
    hashGraphCore(hasher, graph, true);
    hashDocumentState(hasher, graph);
    return hasher.value();
}

} // namespace projectunity::assets

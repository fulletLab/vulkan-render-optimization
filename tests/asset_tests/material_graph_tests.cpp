#include <projectunity/assets/MaterialGraphAsset.hpp>

#include <algorithm>
#include <cstdlib>
#include <iostream>

namespace {

using namespace projectunity::assets;

int fail(const char* message)
{
    std::cerr << message << '\n';
    return EXIT_FAILURE;
}

[[nodiscard]] MaterialGraphAsset makeGraph()
{
    MaterialGraphPin colorOutput;
    colorOutput.id = MaterialGraphPinId(101);
    colorOutput.key = "value";
    colorOutput.displayName = "Color";
    colorOutput.direction = MaterialGraphPinDirection::Output;
    colorOutput.valueType = MaterialGraphValueType::Color4;
    colorOutput.defaultValue = std::array<float, 4> {0.8F, 0.2F, 0.1F, 1.0F};

    MaterialGraphNode colorNode;
    colorNode.id = MaterialGraphNodeId(10);
    colorNode.typeId = "projectunity.input.color4";
    colorNode.displayName = "Base Color";
    colorNode.pins.push_back(colorOutput);

    MaterialGraphPin surfaceInput;
    surfaceInput.id = MaterialGraphPinId(201);
    surfaceInput.key = "base_color";
    surfaceInput.displayName = "Base Color";
    surfaceInput.direction = MaterialGraphPinDirection::Input;
    surfaceInput.valueType = MaterialGraphValueType::Color4;
    surfaceInput.defaultValue = std::array<float, 4> {1.0F, 1.0F, 1.0F, 1.0F};

    MaterialGraphNode outputNode;
    outputNode.id = MaterialGraphNodeId(20);
    outputNode.typeId = "projectunity.output.pbr_surface";
    outputNode.displayName = "PBR Surface";
    outputNode.pins.push_back(surfaceInput);

    MaterialGraphAsset graph;
    graph.id = projectunity::core::StableId(1);
    graph.name = "Graph Test";
    graph.outputNodeId = outputNode.id;
    graph.nodes = {colorNode, outputNode};
    graph.connections.push_back({MaterialGraphConnectionId(301), colorOutput.id, surfaceInput.id});
    graph.parameters.push_back({
        MaterialGraphParameterId(401),
        "Tint",
        MaterialGraphValueType::Color4,
        std::array<float, 4> {1.0F, 1.0F, 1.0F, 1.0F},
        surfaceInput.id,
        {},
    });
    graph.editorState.nodes = {
        {colorNode.id, {-200.0F, 20.0F}, false},
        {outputNode.id, {120.0F, 20.0F}, false},
    };
    return graph;
}

[[nodiscard]] bool hasDiagnostic(
    const MaterialGraphValidationResult& result,
    MaterialGraphDiagnosticCode code)
{
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(), [code](const auto& diagnostic) {
        return diagnostic.code == code;
    });
}

} // namespace

int main()
{
    auto graph = makeGraph();
    if (const auto validation = validateMaterialGraph(graph); !validation.valid()) {
        return fail("valid material graph was rejected");
    }

    const auto semanticHash = materialGraphSemanticHash(graph);
    const auto documentHash = materialGraphDocumentHash(graph);
    graph.editorState.nodes.front().position[0] += 64.0F;
    if (materialGraphSemanticHash(graph) != semanticHash
        || materialGraphDocumentHash(graph) == documentHash) {
        return fail("material graph editor layout affected the semantic hash contract");
    }

    auto reordered = makeGraph();
    std::reverse(reordered.nodes.begin(), reordered.nodes.end());
    std::reverse(reordered.editorState.nodes.begin(), reordered.editorState.nodes.end());
    if (materialGraphSemanticHash(reordered) != semanticHash
        || materialGraphDocumentHash(reordered) != documentHash) {
        return fail("material graph hashes depend on vector ordering");
    }

    auto cycle = makeGraph();
    MaterialGraphPin feedbackInput;
    feedbackInput.id = MaterialGraphPinId(102);
    feedbackInput.key = "feedback";
    feedbackInput.direction = MaterialGraphPinDirection::Input;
    feedbackInput.valueType = MaterialGraphValueType::Color4;
    feedbackInput.defaultValue = std::array<float, 4> {};
    cycle.nodes.front().pins.push_back(feedbackInput);
    MaterialGraphPin feedbackOutput;
    feedbackOutput.id = MaterialGraphPinId(202);
    feedbackOutput.key = "surface";
    feedbackOutput.direction = MaterialGraphPinDirection::Output;
    feedbackOutput.valueType = MaterialGraphValueType::Color4;
    feedbackOutput.defaultValue = std::array<float, 4> {};
    cycle.nodes.back().pins.push_back(feedbackOutput);
    cycle.connections.push_back({MaterialGraphConnectionId(302), feedbackOutput.id, feedbackInput.id});
    const auto cycleValidation = validateMaterialGraph(cycle);
    if (!hasDiagnostic(cycleValidation, MaterialGraphDiagnosticCode::GraphCycle)) {
        return fail("material graph cycle was not rejected");
    }

    auto mismatch = makeGraph();
    mismatch.nodes.back().pins.front().valueType = MaterialGraphValueType::Float;
    mismatch.nodes.back().pins.front().defaultValue = 1.0F;
    const auto mismatchValidation = validateMaterialGraph(mismatch);
    if (!hasDiagnostic(mismatchValidation, MaterialGraphDiagnosticCode::ConnectionTypeMismatch)
        || !hasDiagnostic(mismatchValidation, MaterialGraphDiagnosticCode::ParameterTypeMismatch)) {
        return fail("material graph pin and parameter type mismatch was not rejected");
    }

    auto duplicate = makeGraph();
    duplicate.nodes.back().pins.front().id = duplicate.nodes.front().pins.front().id;
    const auto duplicateValidation = validateMaterialGraph(duplicate);
    if (!hasDiagnostic(duplicateValidation, MaterialGraphDiagnosticCode::DuplicatePinId)) {
        return fail("material graph duplicate pin id was not rejected");
    }

    return EXIT_SUCCESS;
}

#pragma once

#include <projectunity/assets/AssetManager.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace projectunity::asset_tests {

[[nodiscard]] inline bool isEnvironmentVariableSet(const char* name)
{
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) {
        return false;
    }
    std::free(value);
    return size > 0U;
#else
    return std::getenv(name) != nullptr;
#endif
}

inline void printNodePerformanceMetrics(
    const assets::ModelAsset& model,
    const assets::AssetImportResult& result,
    std::size_t firstTexturePixels,
    std::uint64_t firstTextureAverageRgb)
{
    if (!isEnvironmentVariableSet("PROJECTUNITY_PRINT_NODE_PERF_METRICS")) {
        return;
    }
    std::cout << "NodePerformanceTest imported:"
              << " primitives=" << model.primitives.size()
              << " instances=" << model.primitiveInstances.size()
              << " editorInstances=" << model.editorInstances.size()
              << " clusters=" << model.primitiveClusters.size()
              << " materials=" << model.materials.size()
              << " textures=" << model.textures.size()
              << " vertices=" << result.record.vertexCount
              << " firstTexturePixels=" << firstTexturePixels
              << " firstTextureAvgRgb=" << firstTextureAverageRgb
              << '\n';
}

[[nodiscard]] inline const char* validateNodePerformanceImport(
    const assets::ModelAsset* model,
    const assets::AssetImportResult& result)
{
    const auto materialBatchFloor = model != nullptr
        ? std::max<std::size_t>(model->materials.size(), 1U)
        : 1U;
    if (!result.success
        || model == nullptr
        || model->primitives.empty()
        || model->primitiveInstances.empty()
        || model->editorInstances.empty()
        || model->editorInstances.size() <= model->primitiveInstances.size()
        || model->primitiveInstances.size() <= materialBatchFloor
        || model->primitiveClusters.empty()
        || model->primitiveClusters.size() >= model->primitiveInstances.size()
        || model->primitiveInstances.size() > 2048U) {
        std::cerr << result.error << '\n';
        if (model != nullptr) {
            std::cerr << "primitives=" << model->primitives.size()
                      << " instances=" << model->primitiveInstances.size()
                      << " editorInstances=" << model->editorInstances.size()
                      << " clusters=" << model->primitiveClusters.size()
                      << " materials=" << model->materials.size()
                      << " textures=" << model->textures.size()
                      << " vertices=" << result.record.vertexCount << '\n';
        }
        return "NodePerformanceTest did not import into renderer-friendly spatial batches";
    }

    std::vector<bool> clustered(model->primitiveInstances.size(), false);
    std::size_t clusteredCount = 0;
    for (const auto& cluster : model->primitiveClusters) {
        if (cluster.primitiveInstanceIndices.empty() || cluster.bounds.radius <= 0.0F) {
            return "NodePerformanceTest primitive cluster hierarchy contains empty clusters";
        }
        for (const auto instanceIndex : cluster.primitiveInstanceIndices) {
            if (instanceIndex >= clustered.size() || clustered[instanceIndex]) {
                return "NodePerformanceTest primitive cluster hierarchy contains invalid instance references";
            }
            clustered[instanceIndex] = true;
            ++clusteredCount;
        }
    }
    if (clusteredCount != model->primitiveInstances.size()) {
        return "NodePerformanceTest primitive cluster hierarchy does not cover every instance";
    }
    if (result.record.vertexCount > 1'000'000U) {
        return "NodePerformanceTest import produced an unexpected vertex count";
    }
    if (model->materials.empty()
        || !model->materials.front().baseColorTexture.has_value()
        || *model->materials.front().baseColorTexture >= model->textures.size()) {
        return "NodePerformanceTest import lost base-color texture material bindings";
    }

    const auto& texture = model->textures[*model->materials.front().baseColorTexture];
    std::uint64_t colorTotal = 0;
    const auto pixelCount = texture.rgba8.size() / 4U;
    for (std::size_t pixel = 0; pixel < pixelCount; ++pixel) {
        colorTotal += texture.rgba8[pixel * 4U];
        colorTotal += texture.rgba8[pixel * 4U + 1U];
        colorTotal += texture.rgba8[pixel * 4U + 2U];
    }
    const auto averageColor = colorTotal / std::max<std::uint64_t>(pixelCount * 3U, 1U);
    printNodePerformanceMetrics(*model, result, pixelCount, averageColor);
    if (!texture.id.isValid() || pixelCount == 0U || averageColor > 220U) {
        return "NodePerformanceTest import produced invalid texture data";
    }

    const auto hasLods = std::any_of(model->primitives.begin(), model->primitives.end(), [](const assets::MeshPrimitive& primitive) {
        return !primitive.lods.empty()
            && primitive.lods.back().error > 0.0F
            && primitive.lods.back().indices.size() < primitive.indices.size();
    });
    if (!hasLods) {
        return "NodePerformanceTest renderer-friendly batches did not keep runtime LOD data";
    }

    return nullptr;
}

} // namespace projectunity::asset_tests

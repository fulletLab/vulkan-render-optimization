#pragma once

#include <projectunity/assets/AssetManager.hpp>

#include <cstdint>
#include <cstdlib>
#include <iostream>

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
              << " materials=" << model.materials.size()
              << " textures=" << model.textures.size()
              << " vertices=" << result.record.vertexCount
              << " firstTexturePixels=" << firstTexturePixels
              << " firstTextureAvgRgb=" << firstTextureAverageRgb
              << '\n';
}

} // namespace projectunity::asset_tests

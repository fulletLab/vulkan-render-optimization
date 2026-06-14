#pragma once

#include <projectunity/assets/AssetManager.hpp>

#include <string>

namespace projectunity::assets::detail {

struct MeshPrimitiveBatchStats {
    std::string mode;
    std::size_t sourcePrimitiveCount {0};
    std::size_t sourceInstanceCount {0};
    std::size_t materialCount {0};
    std::size_t candidateGridSide {1};
    std::size_t legacyGridSide {1};
    std::size_t finalGridSide {1};
    std::size_t candidateBatchCount {0};
    std::size_t legacyBatchCount {0};
    std::size_t finalBatchCount {0};
    std::size_t outputPrimitiveCount {0};
    std::size_t outputInstanceCount {0};
    std::size_t largestBatchInstances {0};
    std::size_t largestBatchTriangles {0};
    float largestBatchExtent {0.0F};
    float averageInstancesPerBatch {0.0F};
    bool bypassed {false};
};

[[nodiscard]] MeshPrimitiveBatchStats batchModelPrimitives(ModelAsset& model);
[[nodiscard]] bool meshPrimitiveBatchComparisonModeEnabled() noexcept;

} // namespace projectunity::assets::detail

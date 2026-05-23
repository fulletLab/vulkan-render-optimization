#pragma once

#include <cstdint>
#include <vector>

namespace projectunity::renderer {

struct RenderBrdfLut {
    std::uint32_t width {0};
    std::uint32_t height {0};
    std::vector<std::uint8_t> rgba8;
};

[[nodiscard]] RenderBrdfLut generateBrdfIntegrationLut(std::uint32_t size, std::uint32_t sampleCount);

} // namespace projectunity::renderer

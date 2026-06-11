#pragma once

#include <projectunity/renderer/RendererTypes.hpp>

#include <cstdint>

namespace projectunity::renderer {

[[nodiscard]] std::uint64_t renderShadowContentSignature(const RenderFrame& frame) noexcept;

} // namespace projectunity::renderer

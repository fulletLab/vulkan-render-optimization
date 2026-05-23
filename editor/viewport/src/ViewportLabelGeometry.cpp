#include "ViewportLabelGeometry.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>

namespace projectunity::editor {
namespace {

using GlyphRows = std::array<std::uint8_t, 7>;

[[nodiscard]] GlyphRows glyphRows(char value)
{
    switch (static_cast<char>(std::toupper(static_cast<unsigned char>(value)))) {
    case 'A': return {0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001};
    case 'B': return {0b11110, 0b10001, 0b10001, 0b11110, 0b10001, 0b10001, 0b11110};
    case 'C': return {0b01111, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b01111};
    case 'D': return {0b11110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b11110};
    case 'E': return {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111};
    case 'F': return {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b10000};
    case 'G': return {0b01111, 0b10000, 0b10000, 0b10111, 0b10001, 0b10001, 0b01111};
    case 'H': return {0b10001, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001};
    case 'I': return {0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b11111};
    case 'J': return {0b00111, 0b00010, 0b00010, 0b00010, 0b10010, 0b10010, 0b01100};
    case 'K': return {0b10001, 0b10010, 0b10100, 0b11000, 0b10100, 0b10010, 0b10001};
    case 'L': return {0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111};
    case 'M': return {0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001, 0b10001};
    case 'N': return {0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001, 0b10001};
    case 'O': return {0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110};
    case 'P': return {0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000};
    case 'Q': return {0b01110, 0b10001, 0b10001, 0b10001, 0b10101, 0b10010, 0b01101};
    case 'R': return {0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001};
    case 'S': return {0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110};
    case 'T': return {0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100};
    case 'U': return {0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110};
    case 'V': return {0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01010, 0b00100};
    case 'W': return {0b10001, 0b10001, 0b10001, 0b10101, 0b10101, 0b10101, 0b01010};
    case 'X': return {0b10001, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001, 0b10001};
    case 'Y': return {0b10001, 0b10001, 0b01010, 0b00100, 0b00100, 0b00100, 0b00100};
    case 'Z': return {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b11111};
    case '0': return {0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110};
    case '1': return {0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110};
    case '2': return {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111};
    case '3': return {0b11110, 0b00001, 0b00001, 0b01110, 0b00001, 0b00001, 0b11110};
    case '4': return {0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010};
    case '5': return {0b11111, 0b10000, 0b10000, 0b11110, 0b00001, 0b00001, 0b11110};
    case '6': return {0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110};
    case '7': return {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000};
    case '8': return {0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110};
    case '9': return {0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b01100};
    case '-': return {0b00000, 0b00000, 0b00000, 0b11111, 0b00000, 0b00000, 0b00000};
    case '_': return {0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b11111};
    case '.': return {0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b01100, 0b01100};
    case '/': return {0b00001, 0b00010, 0b00010, 0b00100, 0b01000, 0b01000, 0b10000};
    default: return {};
    }
}

void appendBillboardQuad(
    std::vector<renderer::RenderColorVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    math::Vec3 center,
    math::Vec3 halfRight,
    math::Vec3 halfUp,
    std::array<float, 4> color)
{
    const auto base = static_cast<std::uint32_t>(vertices.size());
    const auto topLeft = center - halfRight + halfUp;
    const auto topRight = center + halfRight + halfUp;
    const auto bottomRight = center + halfRight - halfUp;
    const auto bottomLeft = center - halfRight - halfUp;
    vertices.push_back({{topLeft.x, topLeft.y, topLeft.z}, color});
    vertices.push_back({{topRight.x, topRight.y, topRight.z}, color});
    vertices.push_back({{bottomRight.x, bottomRight.y, bottomRight.z}, color});
    vertices.push_back({{bottomLeft.x, bottomLeft.y, bottomLeft.z}, color});
    indices.insert(indices.end(), {base, base + 1U, base + 2U, base, base + 2U, base + 3U});
}

} // namespace

void appendViewportLabel(
    std::vector<renderer::RenderColorVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    const ViewportLabelCamera& camera,
    math::Vec3 anchor,
    std::string_view text,
    bool selected)
{
    constexpr std::size_t kMaxLabelCharacters = 48;
    const auto depth = math::dot(anchor - camera.eye, camera.forward);
    if (depth <= 0.05F || depth > 320.0F || text.empty() || !std::isfinite(depth)) {
        return;
    }

    const auto visibleCharacters = std::min(text.size(), kMaxLabelCharacters);
    const auto worldPerPixel = (2.0F * std::tan(camera.verticalFovRadians * 0.5F) * depth)
        / std::max(camera.viewportHeightPixels, 1.0F);
    const auto pixelSize = std::clamp(worldPerPixel * 2.0F, 0.012F, 0.085F);
    const auto characterAdvance = pixelSize * 6.0F;
    const auto labelWidth = static_cast<float>(visibleCharacters) * characterAdvance;
    const auto labelHeight = pixelSize * 7.0F;
    const auto origin = anchor + camera.up * (pixelSize * 10.0F) - camera.right * (labelWidth * 0.5F);
    const auto labelCenter = origin + camera.right * (labelWidth * 0.5F - pixelSize * 0.5F) + camera.up * (labelHeight * 0.5F);
    appendBillboardQuad(
        vertices,
        indices,
        labelCenter,
        camera.right * ((labelWidth + pixelSize * 2.0F) * 0.5F),
        camera.up * ((labelHeight + pixelSize * 2.0F) * 0.5F),
        {0.018F, 0.021F, 0.028F, selected ? 0.72F : 0.52F});

    const auto glyphColor = selected
        ? std::array<float, 4> {1.0F, 0.76F, 0.22F, 0.98F}
        : std::array<float, 4> {0.86F, 0.89F, 0.94F, 0.88F};
    for (std::size_t characterIndex = 0; characterIndex < visibleCharacters; ++characterIndex) {
        const auto rows = glyphRows(text[characterIndex]);
        for (std::size_t row = 0; row < rows.size(); ++row) {
            for (std::size_t column = 0; column < 5U; ++column) {
                if ((rows[row] & (1U << (4U - column))) == 0U) {
                    continue;
                }
                const auto center = origin
                    + camera.right * ((static_cast<float>(characterIndex) * 6.0F + static_cast<float>(column) + 0.5F) * pixelSize)
                    + camera.up * ((static_cast<float>(6U - row) + 0.5F) * pixelSize);
                appendBillboardQuad(
                    vertices,
                    indices,
                    center,
                    camera.right * (pixelSize * 0.42F),
                    camera.up * (pixelSize * 0.42F),
                    glyphColor);
            }
        }
    }
}

} // namespace projectunity::editor

#pragma once

#include <projectunity/math/Vec3.hpp>

#include <array>
#include <cstddef>
#include <vector>

namespace projectunity::debug {

struct DebugColor {
    float r {1.0F};
    float g {1.0F};
    float b {1.0F};
    float a {1.0F};
};

struct DebugLine {
    math::Vec3 start;
    math::Vec3 end;
    DebugColor color;
    float thickness {1.0F};
};

struct DebugGrid {
    math::Vec3 origin;
    math::Vec3 xAxis {1.0F, 0.0F, 0.0F};
    math::Vec3 zAxis {0.0F, 0.0F, 1.0F};
    float halfExtent {10.0F};
    std::size_t divisions {20};
    DebugColor color {0.48F, 0.52F, 0.58F, 0.56F};
    DebugColor centerColor {0.72F, 0.76F, 0.82F, 0.88F};
    float thickness {1.0F};
};

using FrustumCorners = std::array<math::Vec3, 8>;

class IDebugDraw {
public:
    virtual ~IDebugDraw() = default;

    [[nodiscard]] virtual bool line(math::Vec3 start, math::Vec3 end, DebugColor color, float thickness = 1.0F) = 0;
    [[nodiscard]] virtual bool ray(
        math::Vec3 origin,
        math::Vec3 direction,
        float length,
        DebugColor color,
        float thickness = 1.0F) = 0;
    [[nodiscard]] virtual bool aabb(math::Vec3 minimum, math::Vec3 maximum, DebugColor color, float thickness = 1.0F) = 0;
    [[nodiscard]] virtual bool frustum(const FrustumCorners& corners, DebugColor color, float thickness = 1.0F) = 0;
    [[nodiscard]] virtual bool grid(const DebugGrid& settings) = 0;
};

class DebugDrawList final : public IDebugDraw {
public:
    [[nodiscard]] bool line(math::Vec3 start, math::Vec3 end, DebugColor color, float thickness = 1.0F) override;
    [[nodiscard]] bool ray(
        math::Vec3 origin,
        math::Vec3 direction,
        float length,
        DebugColor color,
        float thickness = 1.0F) override;
    [[nodiscard]] bool aabb(math::Vec3 minimum, math::Vec3 maximum, DebugColor color, float thickness = 1.0F) override;
    [[nodiscard]] bool frustum(const FrustumCorners& corners, DebugColor color, float thickness = 1.0F) override;
    [[nodiscard]] bool grid(const DebugGrid& settings) override;

    [[nodiscard]] const std::vector<DebugLine>& lines() const noexcept;
    [[nodiscard]] std::size_t lineCount() const noexcept;
    void clear() noexcept;

private:
    std::vector<DebugLine> lines_;
};

} // namespace projectunity::debug

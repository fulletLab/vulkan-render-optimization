#pragma once

#include <array>

#include <projectunity/math/Vec3.hpp>

namespace projectunity::math {

struct Mat4 {
    std::array<float, 16> values {};

    [[nodiscard]] static constexpr Mat4 identity() noexcept
    {
        return Mat4 {{
            1.0F, 0.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.0F,
            0.0F, 0.0F, 0.0F, 1.0F,
        }};
    }

    [[nodiscard]] static constexpr Mat4 translation(Vec3 value) noexcept
    {
        auto matrix = identity();
        matrix.values[12] = value.x;
        matrix.values[13] = value.y;
        matrix.values[14] = value.z;
        return matrix;
    }

    [[nodiscard]] static constexpr Mat4 scale(Vec3 value) noexcept
    {
        auto matrix = identity();
        matrix.values[0] = value.x;
        matrix.values[5] = value.y;
        matrix.values[10] = value.z;
        return matrix;
    }
};

[[nodiscard]] constexpr Mat4 operator*(const Mat4& lhs, const Mat4& rhs) noexcept
{
    Mat4 result {};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0F;
            for (int inner = 0; inner < 4; ++inner) {
                sum += lhs.values[inner * 4 + row] * rhs.values[column * 4 + inner];
            }
            result.values[column * 4 + row] = sum;
        }
    }
    return result;
}

[[nodiscard]] constexpr Vec3 transformPoint(const Mat4& matrix, Vec3 point) noexcept
{
    return {
        point.x * matrix.values[0] + point.y * matrix.values[4] + point.z * matrix.values[8] + matrix.values[12],
        point.x * matrix.values[1] + point.y * matrix.values[5] + point.z * matrix.values[9] + matrix.values[13],
        point.x * matrix.values[2] + point.y * matrix.values[6] + point.z * matrix.values[10] + matrix.values[14],
    };
}

} // namespace projectunity::math

#pragma once

#include <cmath>

namespace projectunity::math {

struct Vec3 {
    float x {0.0F};
    float y {0.0F};
    float z {0.0F};

    constexpr Vec3() = default;
    constexpr Vec3(float xValue, float yValue, float zValue) noexcept
        : x(xValue)
        , y(yValue)
        , z(zValue)
    {
    }

    [[nodiscard]] constexpr float lengthSquared() const noexcept
    {
        return x * x + y * y + z * z;
    }

    [[nodiscard]] float length() const noexcept
    {
        return std::sqrt(lengthSquared());
    }

    [[nodiscard]] Vec3 normalized(float epsilon = 0.00001F) const noexcept
    {
        const auto len = length();
        if (len <= epsilon) {
            return {};
        }
        return {x / len, y / len, z / len};
    }
};

[[nodiscard]] constexpr Vec3 operator+(Vec3 lhs, Vec3 rhs) noexcept
{
    return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

[[nodiscard]] constexpr Vec3 operator-(Vec3 lhs, Vec3 rhs) noexcept
{
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

[[nodiscard]] constexpr Vec3 operator*(Vec3 value, float scalar) noexcept
{
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

[[nodiscard]] constexpr Vec3 operator*(float scalar, Vec3 value) noexcept
{
    return value * scalar;
}

[[nodiscard]] constexpr Vec3 operator/(Vec3 value, float scalar) noexcept
{
    return {value.x / scalar, value.y / scalar, value.z / scalar};
}

constexpr Vec3& operator+=(Vec3& lhs, Vec3 rhs) noexcept
{
    lhs = lhs + rhs;
    return lhs;
}

constexpr Vec3& operator-=(Vec3& lhs, Vec3 rhs) noexcept
{
    lhs = lhs - rhs;
    return lhs;
}

[[nodiscard]] constexpr float dot(Vec3 lhs, Vec3 rhs) noexcept
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

[[nodiscard]] constexpr Vec3 cross(Vec3 lhs, Vec3 rhs) noexcept
{
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

[[nodiscard]] constexpr float distanceSquared(Vec3 lhs, Vec3 rhs) noexcept
{
    return (lhs - rhs).lengthSquared();
}

[[nodiscard]] inline bool nearlyEqual(Vec3 lhs, Vec3 rhs, float epsilon = 0.00001F) noexcept
{
    return std::fabs(lhs.x - rhs.x) <= epsilon
        && std::fabs(lhs.y - rhs.y) <= epsilon
        && std::fabs(lhs.z - rhs.z) <= epsilon;
}

} // namespace projectunity::math

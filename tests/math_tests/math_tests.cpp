#include <projectunity/math/Mat4.hpp>
#include <projectunity/math/Vec3.hpp>

#include <cstdlib>
#include <iostream>

namespace {

int fail(const char* message)
{
    std::cerr << message << '\n';
    return EXIT_FAILURE;
}

} // namespace

int main()
{
    using namespace projectunity::math;

    const Vec3 a {1.0F, 2.0F, 3.0F};
    const Vec3 b {4.0F, 5.0F, 6.0F};

    if (!nearlyEqual(a + b, {5.0F, 7.0F, 9.0F})) {
        return fail("vec3 addition failed");
    }

    if (!nearlyEqual(cross({1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}), {0.0F, 0.0F, 1.0F})) {
        return fail("vec3 cross product failed");
    }

    auto accumulated = a;
    accumulated += {1.0F, 1.0F, 1.0F};
    accumulated -= {2.0F, 0.0F, 1.0F};
    if (!nearlyEqual(accumulated / 2.0F, {0.0F, 1.5F, 1.5F})) {
        return fail("vec3 compound arithmetic failed");
    }

    if (distanceSquared({1.0F, 1.0F, 1.0F}, {3.0F, 1.0F, 1.0F}) != 4.0F) {
        return fail("vec3 distance squared failed");
    }

    const auto transform = Mat4::translation({10.0F, 20.0F, 30.0F}) * Mat4::scale({2.0F, 3.0F, 4.0F});
    const auto point = transformPoint(transform, {1.0F, 1.0F, 1.0F});
    if (!nearlyEqual(point, {12.0F, 23.0F, 34.0F})) {
        return fail("mat4 transform failed");
    }

    return EXIT_SUCCESS;
}

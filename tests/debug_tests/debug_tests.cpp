#include <projectunity/debug/DebugDraw.hpp>

#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

int fail(const char* message)
{
    std::cerr << message << '\n';
    return EXIT_FAILURE;
}

} // namespace

int main()
{
    using namespace projectunity::debug;
    using projectunity::math::Vec3;

    DebugDrawList draw;
    const DebugColor white {1.0F, 1.0F, 1.0F, 1.0F};
    if (!draw.line({0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, white, 1.0F)) {
        return fail("debug line was rejected");
    }
    if (!draw.ray({0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, 2.0F, white, 1.0F)) {
        return fail("debug ray was rejected");
    }
    if (!draw.aabb({-1.0F, -1.0F, -1.0F}, {1.0F, 1.0F, 1.0F}, white, 1.0F)) {
        return fail("debug aabb was rejected");
    }

    const FrustumCorners frustum {
        Vec3 {-1.0F, -1.0F, 1.0F},
        Vec3 {1.0F, -1.0F, 1.0F},
        Vec3 {1.0F, 1.0F, 1.0F},
        Vec3 {-1.0F, 1.0F, 1.0F},
        Vec3 {-2.0F, -2.0F, 4.0F},
        Vec3 {2.0F, -2.0F, 4.0F},
        Vec3 {2.0F, 2.0F, 4.0F},
        Vec3 {-2.0F, 2.0F, 4.0F},
    };
    if (!draw.frustum(frustum, white, 1.0F)) {
        return fail("debug frustum was rejected");
    }

    DebugGrid grid;
    grid.halfExtent = 2.0F;
    grid.divisions = 4;
    if (!draw.grid(grid)) {
        return fail("debug grid was rejected");
    }

    constexpr std::size_t expectedLineCount = 1 + 5 + 12 + 12 + 10;
    if (draw.lineCount() != expectedLineCount) {
        return fail("debug primitive line expansion mismatch");
    }

    if (draw.line({0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, white, 0.0F)) {
        return fail("invalid line thickness was accepted");
    }
    if (draw.ray({0.0F, 0.0F, 0.0F}, {}, 1.0F, white, 1.0F)) {
        return fail("zero debug ray direction was accepted");
    }
    if (draw.aabb({1.0F, 0.0F, 0.0F}, {-1.0F, 0.0F, 0.0F}, white, 1.0F)) {
        return fail("invalid aabb bounds were accepted");
    }
    grid.halfExtent = std::numeric_limits<float>::quiet_NaN();
    if (draw.grid(grid)) {
        return fail("nan grid settings were accepted");
    }

    draw.clear();
    if (draw.lineCount() != 0) {
        return fail("debug draw clear did not remove lines");
    }

    return EXIT_SUCCESS;
}

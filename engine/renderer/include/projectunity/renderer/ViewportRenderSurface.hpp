#pragma once

#include <cstdint>

namespace projectunity::renderer {

struct ViewportRenderSurfaceDesc {
    void* nativeWindowHandle {nullptr};
    std::uint32_t width {0};
    std::uint32_t height {0};
    bool vsync {true};
};

class IViewportRenderSurface {
public:
    virtual ~IViewportRenderSurface() = default;

    [[nodiscard]] virtual const ViewportRenderSurfaceDesc& description() const noexcept = 0;
    virtual void resize(std::uint32_t width, std::uint32_t height) = 0;
};

} // namespace projectunity::renderer

#pragma once

#include <projectunity/renderer/RendererTypes.hpp>
#include <projectunity/renderer/ViewportRenderSurface.hpp>

#include <string>

namespace projectunity::renderer {

class IRenderer {
public:
    virtual ~IRenderer() = default;

    [[nodiscard]] virtual const RendererStats& stats() const noexcept = 0;
    [[nodiscard]] virtual bool isReady() const noexcept = 0;
    [[nodiscard]] virtual bool supportsSurface(const ViewportRenderSurfaceDesc& surface) const noexcept = 0;
    [[nodiscard]] virtual bool prepareSurface(const ViewportRenderSurfaceDesc& surface, std::string* errorMessage = nullptr) = 0;
    [[nodiscard]] virtual bool renderSurfaceFrame(
        const ViewportRenderSurfaceDesc& surface,
        const RenderFrame& frame,
        std::string* errorMessage = nullptr) = 0;
    virtual void releaseSurface(void* nativeWindowHandle) noexcept = 0;
};

} // namespace projectunity::renderer

#pragma once

#include <projectunity/renderer/IRenderer.hpp>

#include <memory>
#include <string>

namespace projectunity::renderer {

class VulkanRenderer final : public IRenderer {
public:
    explicit VulkanRenderer(RendererConfig config);
    ~VulkanRenderer() override;

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;
    VulkanRenderer(VulkanRenderer&&) = delete;
    VulkanRenderer& operator=(VulkanRenderer&&) = delete;

    [[nodiscard]] const RendererStats& stats() const noexcept override;
    [[nodiscard]] bool isReady() const noexcept override;
    [[nodiscard]] bool supportsSurface(const ViewportRenderSurfaceDesc& surface) const noexcept override;
    [[nodiscard]] bool prepareSurface(const ViewportRenderSurfaceDesc& surface, std::string* errorMessage = nullptr) override;
    [[nodiscard]] bool renderSurfaceFrame(
        const ViewportRenderSurfaceDesc& surface,
        const RenderFrame& frame,
        std::string* errorMessage = nullptr) override;
    void releaseSurface(void* nativeWindowHandle) noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::unique_ptr<IRenderer> createVulkanRenderer(RendererConfig config, std::string* errorMessage = nullptr);

} // namespace projectunity::renderer

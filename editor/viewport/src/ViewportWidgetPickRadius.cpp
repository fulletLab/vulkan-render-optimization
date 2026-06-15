#include <projectunity/editor/ViewportWidget.hpp>

#include <algorithm>
#include <cmath>

namespace projectunity::editor {

float ViewportWidget::entityPickRadius(const scene::Entity& entity) const
{
    const auto maxScale = std::max({
        std::fabs(entity.transform.scale.x),
        std::fabs(entity.transform.scale.y),
        std::fabs(entity.transform.scale.z),
    });
    if (assetManager_ != nullptr
        && entity.meshRenderer.has_value()
        && (entity.meshRenderer->primitiveInstanceIndex.has_value()
            || entity.meshRenderer->editorInstanceIndex.has_value())) {
        const auto model = assetManager_->model(entity.meshRenderer->modelAssetId);
        if (model != nullptr && entity.meshRenderer->editorInstanceIndex.has_value()) {
            const auto index = *entity.meshRenderer->editorInstanceIndex;
            if (index < model->editorInstances.size()) {
                return std::clamp(model->editorInstances[index].bounds.radius * maxScale, 0.35F, 80.0F);
            }
        }
        if (model != nullptr && entity.meshRenderer->primitiveInstanceIndex.has_value()) {
            const auto index = *entity.meshRenderer->primitiveInstanceIndex;
            if (index < model->primitiveInstances.size()) {
                return std::clamp(model->primitiveInstances[index].bounds.radius * maxScale, 0.35F, 80.0F);
            }
        }
    }
    if (assetManager_ != nullptr
        && entity.meshRenderer.has_value()
        && !entity.meshRenderer->primitiveInstanceIndex.has_value()) {
        const auto model = assetManager_->model(entity.meshRenderer->modelAssetId);
        if (model != nullptr) {
            float radius = 0.0F;
            if (!model->primitiveInstances.empty()) {
                for (const auto& instance : model->primitiveInstances) {
                    radius = std::max(radius, instance.bounds.center.length() + instance.bounds.radius);
                }
            } else {
                for (const auto& primitive : model->primitives) {
                    radius = std::max(radius, primitive.bounds.center.length() + primitive.bounds.radius);
                }
            }
            if (radius > 0.0F && std::isfinite(radius)) {
                return std::clamp(radius * maxScale, 0.35F, 160.0F);
            }
        }
    }
    if (entity.camera.has_value()) {
        return std::clamp(maxScale * 0.85F, 0.45F, 6.0F);
    }
    return std::clamp(maxScale * 0.55F, 0.35F, 5.0F);
}

} // namespace projectunity::editor

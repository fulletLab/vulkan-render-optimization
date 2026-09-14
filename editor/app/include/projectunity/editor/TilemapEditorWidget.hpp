#pragma once

#include <projectunity/scene/Scene.hpp>

#include <functional>

#include <QWidget>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;

namespace projectunity::editor {

class TilemapCanvas;

class TilemapEditorWidget final : public QWidget {
public:
    explicit TilemapEditorWidget(QWidget* parent = nullptr);

    void setScene(scene::Scene* scene);
    void setSelectedEntity(scene::EntityId id);
    void setSceneEditedCallback(std::function<void()> callback);
    void setSelectionCallback(std::function<void(scene::EntityId)> callback);
    void refresh();

    [[nodiscard]] scene::EntityId createTilemapEntity();
    [[nodiscard]] bool ensureTilemapOnSelection();

private:
    friend class TilemapCanvas;

    [[nodiscard]] scene::Entity* selectedEntity() const;
    [[nodiscard]] scene::TilemapComponent* selectedTilemap() const;
    [[nodiscard]] bool paintCell(std::uint32_t cellX, std::uint32_t cellY);
    void resizeSelectedTilemap();
    void setBrush(std::int32_t tileId);
    void notifyEdited();

    scene::Scene* scene_ {nullptr};
    scene::EntityId selectedEntityId_;
    std::function<void()> sceneEditedCallback_;
    std::function<void(scene::EntityId)> selectionCallback_;
    TilemapCanvas* canvas_ {nullptr};
    QLabel* statusLabel_ {nullptr};
    QSpinBox* widthSpin_ {nullptr};
    QSpinBox* heightSpin_ {nullptr};
    QDoubleSpinBox* tileSizeSpin_ {nullptr};
    QSpinBox* brushSpin_ {nullptr};
    QCheckBox* eraseCheck_ {nullptr};
    bool refreshing_ {false};
};

} // namespace projectunity::editor

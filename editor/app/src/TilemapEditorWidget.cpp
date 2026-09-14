#include <projectunity/editor/TilemapEditorWidget.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>

#include <QCheckBox>
#include <QColor>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPoint>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSize>
#include <QSpinBox>
#include <QVBoxLayout>

namespace projectunity::editor {
namespace {

constexpr int kCanvasMargin = 18;
constexpr int kCellPixels = 28;
constexpr int kMaxEditorTilemapDimension = 256;

[[nodiscard]] QColor tileColor(std::int32_t tileId)
{
    static const std::array<QColor, 12> kPalette {
        QColor(85, 166, 255),
        QColor(99, 204, 141),
        QColor(244, 183, 80),
        QColor(235, 101, 104),
        QColor(172, 126, 230),
        QColor(73, 194, 189),
        QColor(230, 132, 76),
        QColor(162, 188, 83),
        QColor(96, 124, 180),
        QColor(210, 105, 153),
        QColor(112, 178, 117),
        QColor(204, 170, 88),
    };
    if (tileId < 0) {
        return QColor(31, 34, 42);
    }
    return kPalette[static_cast<std::size_t>(tileId) % kPalette.size()];
}

[[nodiscard]] QString tilemapSummary(const scene::TilemapComponent& tilemap)
{
    const auto painted = std::count_if(tilemap.tileIds.begin(), tilemap.tileIds.end(), [](std::int32_t tileId) {
        return tileId >= 0;
    });
    return QStringLiteral("Tilemap %1 x %2 | tileSize %3 | pintadas %4/%5")
        .arg(tilemap.width)
        .arg(tilemap.height)
        .arg(tilemap.tileSize, 0, 'f', 2)
        .arg(static_cast<qulonglong>(painted))
        .arg(static_cast<qulonglong>(tilemap.tileIds.size()));
}

} // namespace

class TilemapCanvas final : public QWidget {
public:
    explicit TilemapCanvas(TilemapEditorWidget* editor)
        : QWidget(editor)
        , editor_(editor)
    {
        setMouseTracking(true);
        setMinimumSize(320, 240);
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        const auto* tilemap = editor_->selectedTilemap();
        if (tilemap == nullptr) {
            return QSize(480, 320);
        }
        const auto width = kCanvasMargin * 2 + static_cast<int>(tilemap->width) * kCellPixels;
        const auto height = kCanvasMargin * 2 + static_cast<int>(tilemap->height) * kCellPixels;
        return QSize(width, height);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.fillRect(rect(), QColor(24, 26, 31));

        const auto* entity = editor_->selectedEntity();
        const auto* tilemap = editor_->selectedTilemap();
        if (entity == nullptr || tilemap == nullptr) {
            painter.setPen(QColor(180, 188, 200));
            painter.drawText(rect().adjusted(18, 18, -18, -18), Qt::AlignTop | Qt::TextWordWrap,
                QStringLiteral("Crea un Tilemap 2D o selecciona una entidad con Tilemap."));
            return;
        }

        const QRect gridRect(
            kCanvasMargin,
            kCanvasMargin,
            static_cast<int>(tilemap->width) * kCellPixels,
            static_cast<int>(tilemap->height) * kCellPixels);
        painter.fillRect(gridRect, QColor(31, 34, 42));

        for (std::uint32_t y = 0; y < tilemap->height; ++y) {
            for (std::uint32_t x = 0; x < tilemap->width; ++x) {
                const auto index = static_cast<std::size_t>(y) * tilemap->width + x;
                const auto tileId = tilemap->tileIds[index];
                const QRect cellRect(
                    kCanvasMargin + static_cast<int>(x) * kCellPixels,
                    kCanvasMargin + static_cast<int>(y) * kCellPixels,
                    kCellPixels,
                    kCellPixels);
                if (tileId >= 0) {
                    painter.fillRect(cellRect.adjusted(1, 1, -1, -1), tileColor(tileId));
                    painter.setPen(QColor(16, 18, 22));
                    painter.drawText(cellRect, Qt::AlignCenter, QString::number(tileId));
                }
            }
        }

        painter.setPen(QColor(67, 73, 86));
        for (std::uint32_t x = 0; x <= tilemap->width; ++x) {
            const auto px = kCanvasMargin + static_cast<int>(x) * kCellPixels;
            painter.drawLine(px, gridRect.top(), px, gridRect.bottom());
        }
        for (std::uint32_t y = 0; y <= tilemap->height; ++y) {
            const auto py = kCanvasMargin + static_cast<int>(y) * kCellPixels;
            painter.drawLine(gridRect.left(), py, gridRect.right(), py);
        }

        painter.setPen(QColor(210, 216, 225));
        painter.drawText(
            QRect(kCanvasMargin, 2, gridRect.width(), kCanvasMargin - 2),
            Qt::AlignLeft | Qt::AlignVCenter,
            QString::fromStdString(entity->name));
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        handleMouse(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if ((event->buttons() & Qt::LeftButton) != Qt::NoButton) {
            handleMouse(event);
        }
    }

private:
    void handleMouse(QMouseEvent* event)
    {
        if (event->button() != Qt::LeftButton && (event->buttons() & Qt::LeftButton) == Qt::NoButton) {
            return;
        }

        std::uint32_t cellX = 0;
        std::uint32_t cellY = 0;
        if (!pointToCell(event->position().toPoint(), cellX, cellY)) {
            return;
        }
        if (editor_->paintCell(cellX, cellY)) {
            update();
        }
    }

    [[nodiscard]] bool pointToCell(const QPoint& point, std::uint32_t& cellX, std::uint32_t& cellY) const
    {
        const auto* tilemap = editor_->selectedTilemap();
        if (tilemap == nullptr) {
            return false;
        }

        const auto localX = point.x() - kCanvasMargin;
        const auto localY = point.y() - kCanvasMargin;
        if (localX < 0 || localY < 0) {
            return false;
        }

        cellX = static_cast<std::uint32_t>(localX / kCellPixels);
        cellY = static_cast<std::uint32_t>(localY / kCellPixels);
        return cellX < tilemap->width && cellY < tilemap->height;
    }

    TilemapEditorWidget* editor_ {nullptr};
};

TilemapEditorWidget::TilemapEditorWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    auto* commandRow = new QHBoxLayout;
    auto* createButton = new QPushButton(QStringLiteral("Crear Tilemap 2D"));
    auto* attachButton = new QPushButton(QStringLiteral("Agregar a seleccion"));
    commandRow->addWidget(createButton);
    commandRow->addWidget(attachButton);
    layout->addLayout(commandRow);

    auto* settingsFrame = new QFrame;
    settingsFrame->setFrameShape(QFrame::StyledPanel);
    auto* settingsLayout = new QFormLayout(settingsFrame);
    widthSpin_ = new QSpinBox;
    widthSpin_->setRange(1, kMaxEditorTilemapDimension);
    widthSpin_->setValue(16);
    heightSpin_ = new QSpinBox;
    heightSpin_->setRange(1, kMaxEditorTilemapDimension);
    heightSpin_->setValue(16);
    tileSizeSpin_ = new QDoubleSpinBox;
    tileSizeSpin_->setRange(0.05, 64.0);
    tileSizeSpin_->setDecimals(2);
    tileSizeSpin_->setValue(1.0);
    brushSpin_ = new QSpinBox;
    brushSpin_->setRange(0, 999);
    brushSpin_->setValue(0);
    eraseCheck_ = new QCheckBox(QStringLiteral("Borrar"));
    auto* applySizeButton = new QPushButton(QStringLiteral("Aplicar tamano"));

    settingsLayout->addRow(QStringLiteral("Ancho"), widthSpin_);
    settingsLayout->addRow(QStringLiteral("Alto"), heightSpin_);
    settingsLayout->addRow(QStringLiteral("Tile size"), tileSizeSpin_);
    settingsLayout->addRow(QStringLiteral("Brush ID"), brushSpin_);
    settingsLayout->addRow(QString(), eraseCheck_);
    settingsLayout->addRow(QString(), applySizeButton);
    layout->addWidget(settingsFrame);

    auto* paletteGrid = new QGridLayout;
    paletteGrid->setSpacing(4);
    for (int tileId = 0; tileId < 12; ++tileId) {
        auto* button = new QPushButton(QString::number(tileId));
        button->setFixedSize(32, 28);
        button->setToolTip(QStringLiteral("Tile ID %1").arg(tileId));
        button->setStyleSheet(QStringLiteral("QPushButton { background: %1; color: #11151a; font-weight: 600; }")
            .arg(tileColor(tileId).name()));
        paletteGrid->addWidget(button, tileId / 6, tileId % 6);
        connect(button, &QPushButton::clicked, this, [this, tileId] {
            setBrush(tileId);
        });
    }
    layout->addLayout(paletteGrid);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(false);
    canvas_ = new TilemapCanvas(this);
    scroll->setWidget(canvas_);
    layout->addWidget(scroll, 1);

    statusLabel_ = new QLabel(QStringLiteral("Crea o selecciona un Tilemap 2D."));
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    connect(createButton, &QPushButton::clicked, this, [this] {
        (void)createTilemapEntity();
    });
    connect(attachButton, &QPushButton::clicked, this, [this] {
        (void)ensureTilemapOnSelection();
    });
    connect(applySizeButton, &QPushButton::clicked, this, [this] {
        resizeSelectedTilemap();
    });
    connect(brushSpin_, &QSpinBox::valueChanged, this, [this](int) {
        if (eraseCheck_ != nullptr && eraseCheck_->isChecked()) {
            eraseCheck_->setChecked(false);
        }
    });
}

void TilemapEditorWidget::setScene(scene::Scene* scene)
{
    scene_ = scene;
    if (scene_ == nullptr || (selectedEntityId_.isValid() && scene_->findEntity(selectedEntityId_) == nullptr)) {
        selectedEntityId_ = {};
    }
    refresh();
}

void TilemapEditorWidget::setSelectedEntity(scene::EntityId id)
{
    selectedEntityId_ = id;
    refresh();
}

void TilemapEditorWidget::setSceneEditedCallback(std::function<void()> callback)
{
    sceneEditedCallback_ = std::move(callback);
}

void TilemapEditorWidget::setSelectionCallback(std::function<void(scene::EntityId)> callback)
{
    selectionCallback_ = std::move(callback);
}

void TilemapEditorWidget::refresh()
{
    refreshing_ = true;
    auto* tilemap = selectedTilemap();
    const auto enabled = tilemap != nullptr;
    for (auto* widget : {static_cast<QWidget*>(widthSpin_), static_cast<QWidget*>(heightSpin_),
             static_cast<QWidget*>(tileSizeSpin_), static_cast<QWidget*>(brushSpin_), static_cast<QWidget*>(eraseCheck_)}) {
        if (widget != nullptr) {
            widget->setEnabled(enabled);
        }
    }

    if (tilemap != nullptr) {
        const QSignalBlocker blockWidth(widthSpin_);
        const QSignalBlocker blockHeight(heightSpin_);
        const QSignalBlocker blockTileSize(tileSizeSpin_);
        widthSpin_->setValue(static_cast<int>(tilemap->width));
        heightSpin_->setValue(static_cast<int>(tilemap->height));
        tileSizeSpin_->setValue(tilemap->tileSize);
        if (statusLabel_ != nullptr) {
            statusLabel_->setText(tilemapSummary(*tilemap));
        }
    } else if (statusLabel_ != nullptr) {
        statusLabel_->setText(QStringLiteral("Crea un Tilemap 2D o selecciona una entidad con Tilemap."));
    }

    if (canvas_ != nullptr) {
        canvas_->updateGeometry();
        canvas_->update();
    }
    refreshing_ = false;
}

scene::EntityId TilemapEditorWidget::createTilemapEntity()
{
    if (scene_ == nullptr) {
        return {};
    }

    scene::TilemapComponent tilemap;
    tilemap.width = static_cast<std::uint32_t>(widthSpin_ == nullptr ? 16 : widthSpin_->value());
    tilemap.height = static_cast<std::uint32_t>(heightSpin_ == nullptr ? 16 : heightSpin_->value());
    tilemap.tileSize = static_cast<float>(tileSizeSpin_ == nullptr ? 1.0 : tileSizeSpin_->value());
    tilemap.tileIds.assign(tilemap.cellCount(), -1);

    auto& entity = scene_->createEntity("Tilemap 2D");
    if (!scene_->setTilemap(entity.id, tilemap)) {
        return {};
    }

    selectedEntityId_ = entity.id;
    if (selectionCallback_) {
        selectionCallback_(entity.id);
    }
    notifyEdited();
    refresh();
    return entity.id;
}

bool TilemapEditorWidget::ensureTilemapOnSelection()
{
    auto* entity = selectedEntity();
    if (scene_ == nullptr || entity == nullptr) {
        return false;
    }
    if (entity->tilemap.has_value()) {
        refresh();
        return true;
    }

    scene::TilemapComponent tilemap;
    tilemap.width = static_cast<std::uint32_t>(widthSpin_ == nullptr ? 16 : widthSpin_->value());
    tilemap.height = static_cast<std::uint32_t>(heightSpin_ == nullptr ? 16 : heightSpin_->value());
    tilemap.tileSize = static_cast<float>(tileSizeSpin_ == nullptr ? 1.0 : tileSizeSpin_->value());
    tilemap.tileIds.assign(tilemap.cellCount(), -1);
    if (!scene_->setTilemap(entity->id, tilemap)) {
        return false;
    }
    notifyEdited();
    refresh();
    return true;
}

scene::Entity* TilemapEditorWidget::selectedEntity() const
{
    if (scene_ == nullptr || !selectedEntityId_.isValid()) {
        return nullptr;
    }
    return scene_->findEntity(selectedEntityId_);
}

scene::TilemapComponent* TilemapEditorWidget::selectedTilemap() const
{
    auto* entity = selectedEntity();
    if (entity == nullptr || !entity->tilemap.has_value()) {
        return nullptr;
    }
    return &*entity->tilemap;
}

bool TilemapEditorWidget::paintCell(std::uint32_t cellX, std::uint32_t cellY)
{
    auto* tilemap = selectedTilemap();
    if (scene_ == nullptr || tilemap == nullptr || cellX >= tilemap->width || cellY >= tilemap->height) {
        return false;
    }

    auto next = *tilemap;
    // Repaso: la celda editada sale del grid real; no es un color temporal del canvas.
    const auto index = static_cast<std::size_t>(cellY) * next.width + cellX;
    const auto nextTile = (eraseCheck_ != nullptr && eraseCheck_->isChecked())
        ? -1
        : static_cast<std::int32_t>(brushSpin_ == nullptr ? 0 : brushSpin_->value());
    if (next.tileIds[index] == nextTile) {
        return false;
    }

    next.tileIds[index] = nextTile;
    if (!scene_->setTilemap(selectedEntityId_, std::move(next))) {
        return false;
    }
    if (statusLabel_ != nullptr) {
        if (const auto* updated = selectedTilemap()) {
            statusLabel_->setText(tilemapSummary(*updated));
        }
    }
    notifyEdited();
    return true;
}

void TilemapEditorWidget::resizeSelectedTilemap()
{
    auto* tilemap = selectedTilemap();
    if (scene_ == nullptr || tilemap == nullptr || refreshing_) {
        return;
    }

    scene::TilemapComponent next;
    next.width = static_cast<std::uint32_t>(widthSpin_->value());
    next.height = static_cast<std::uint32_t>(heightSpin_->value());
    next.tileSize = static_cast<float>(tileSizeSpin_->value());
    next.tileIds.assign(next.cellCount(), -1);

    const auto copyWidth = std::min(tilemap->width, next.width);
    const auto copyHeight = std::min(tilemap->height, next.height);
    for (std::uint32_t y = 0; y < copyHeight; ++y) {
        for (std::uint32_t x = 0; x < copyWidth; ++x) {
            const auto sourceIndex = static_cast<std::size_t>(y) * tilemap->width + x;
            const auto targetIndex = static_cast<std::size_t>(y) * next.width + x;
            next.tileIds[targetIndex] = tilemap->tileIds[sourceIndex];
        }
    }

    if (!scene_->setTilemap(selectedEntityId_, std::move(next))) {
        refresh();
        return;
    }
    notifyEdited();
    refresh();
}

void TilemapEditorWidget::setBrush(std::int32_t tileId)
{
    if (brushSpin_ != nullptr) {
        brushSpin_->setValue(tileId);
    }
    if (eraseCheck_ != nullptr) {
        eraseCheck_->setChecked(false);
    }
}

void TilemapEditorWidget::notifyEdited()
{
    if (sceneEditedCallback_) {
        sceneEditedCallback_();
    }
}

} // namespace projectunity::editor

#pragma once

#include <QTreeWidget>

namespace projectunity::editor {

inline constexpr auto kProjectUnityAssetIdMime = "application/x-projectunity-asset-id";
inline constexpr auto kProjectUnityAssetPathMime = "application/x-projectunity-asset-path";

class ProjectAssetTreeWidget final : public QTreeWidget {
public:
    explicit ProjectAssetTreeWidget(QWidget* parent = nullptr);

protected:
    [[nodiscard]] QStringList mimeTypes() const override;
    [[nodiscard]] QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override;
    [[nodiscard]] Qt::DropActions supportedDropActions() const override;
};

} // namespace projectunity::editor

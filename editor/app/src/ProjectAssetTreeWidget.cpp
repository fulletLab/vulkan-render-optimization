#include <projectunity/editor/ProjectAssetTreeWidget.hpp>

#include <QAbstractItemView>
#include <QByteArray>
#include <QMimeData>
#include <QTreeWidgetItem>

namespace projectunity::editor {
namespace {

constexpr int kAssetIdRole = Qt::UserRole + 2;
constexpr int kPathRole = Qt::UserRole + 9;

} // namespace

ProjectAssetTreeWidget::ProjectAssetTreeWidget(QWidget* parent)
    : QTreeWidget(parent)
{
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragOnly);
    setDefaultDropAction(Qt::CopyAction);
}

QStringList ProjectAssetTreeWidget::mimeTypes() const
{
    return {QString::fromLatin1(kProjectUnityAssetIdMime), QString::fromLatin1(kProjectUnityAssetPathMime)};
}

QMimeData* ProjectAssetTreeWidget::mimeData(const QList<QTreeWidgetItem*>& items) const
{
    auto* mime = new QMimeData;
    if (items.isEmpty() || items.front() == nullptr) {
        return mime;
    }
    const auto* item = items.front();
    const auto assetId = item->data(0, kAssetIdRole).toULongLong();
    const auto path = item->data(0, kPathRole).toString();
    if (assetId != 0U) {
        mime->setData(kProjectUnityAssetIdMime, QByteArray::number(assetId));
    }
    if (!path.isEmpty()) {
        mime->setData(kProjectUnityAssetPathMime, path.toUtf8());
    }
    return mime;
}

Qt::DropActions ProjectAssetTreeWidget::supportedDropActions() const
{
    return Qt::CopyAction;
}

} // namespace projectunity::editor

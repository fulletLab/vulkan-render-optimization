#include <projectunity/editor/ProjectAssetTreeWidget.hpp>

#include <QAbstractItemView>
#include <QByteArray>
#include <QMimeData>
#include <QTreeWidgetItem>

namespace projectunity::editor {
namespace {

constexpr int kAssetIdRole = Qt::UserRole + 2;
constexpr int kKindRole = Qt::UserRole + 1;
constexpr int kSubAssetIndexRole = Qt::UserRole + 4;
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
    return {
        QString::fromLatin1(kProjectUnityAssetIdMime),
        QString::fromLatin1(kProjectUnityAssetPathMime),
        QString::fromLatin1(kProjectUnityAssetKindMime),
        QString::fromLatin1(kProjectUnitySubAssetIndexMime),
    };
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
    const auto kind = item->data(0, kKindRole);
    const auto subAssetIndex = item->data(0, kSubAssetIndexRole);
    if (assetId != 0U) {
        mime->setData(kProjectUnityAssetIdMime, QByteArray::number(assetId));
    }
    if (!path.isEmpty()) {
        mime->setData(kProjectUnityAssetPathMime, path.toUtf8());
    }
    if (kind.isValid()) {
        mime->setData(kProjectUnityAssetKindMime, QByteArray::number(kind.toInt()));
    }
    if (subAssetIndex.isValid()) {
        mime->setData(kProjectUnitySubAssetIndexMime, QByteArray::number(subAssetIndex.toUInt()));
    }
    return mime;
}

Qt::DropActions ProjectAssetTreeWidget::supportedDropActions() const
{
    return Qt::CopyAction;
}

} // namespace projectunity::editor

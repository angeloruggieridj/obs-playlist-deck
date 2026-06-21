#include "PlaylistListWidget.hpp"

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>

PlaylistListWidget::PlaylistListWidget(QWidget* parent) : QListWidget(parent) {
    setSelectionMode(QAbstractItemView::SingleSelection);
    setDragDropMode(QAbstractItemView::InternalMove); // reorder by drag
    setAcceptDrops(true);
    setDefaultDropAction(Qt::MoveAction);
}

void PlaylistListWidget::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
    else
        QListWidget::dragEnterEvent(e); // internal move
}

void PlaylistListWidget::dragMoveEvent(QDragMoveEvent* e) {
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
    else
        QListWidget::dragMoveEvent(e);
}

void PlaylistListWidget::dropEvent(QDropEvent* e) {
    if (e->mimeData()->hasUrls()) {
        QStringList paths;
        for (const QUrl& u : e->mimeData()->urls()) {
            if (u.isLocalFile()) paths << u.toLocalFile();
        }
        if (!paths.isEmpty()) {
            e->acceptProposedAction();
            emit filesDropped(paths);
        }
        return;
    }
    QListWidget::dropEvent(e); // perform the internal move
    emit reordered();
}

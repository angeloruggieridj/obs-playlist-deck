// SPDX-License-Identifier: MIT
#include "PlaylistView.hpp"
#include "DeckStyle.hpp"

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFont>
#include <QKeyEvent>
#include <QMimeData>
#include <QPainter>
#include <QPen>
#include <QUrl>

PlaylistView::PlaylistView(QWidget* parent) : QListView(parent) {
    setObjectName("pldList");
    // Multi-selection: removing or reordering a block of clips is the common
    // edit before a show, and doing it one row at a time was busywork.
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setDragDropMode(QAbstractItemView::InternalMove); // reorder by drag
    setDragDropOverwriteMode(false);
    setDefaultDropAction(Qt::MoveAction);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setEditTriggers(QAbstractItemView::NoEditTriggers); // renaming is explicit
    setContextMenuPolicy(Qt::CustomContextMenu);
    setUniformItemSizes(false);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
}

void PlaylistView::setPlaceholder(const QString& title, const QString& hint) {
    placeholderTitle_ = title;
    placeholderHint_ = hint;
    viewport()->update();
}

void PlaylistView::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) {
        dropActive_ = true;
        viewport()->update();
        e->acceptProposedAction();
    } else {
        QListView::dragEnterEvent(e); // internal move
    }
}

void PlaylistView::dragMoveEvent(QDragMoveEvent* e) {
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
    else
        QListView::dragMoveEvent(e);
}

void PlaylistView::dragLeaveEvent(QDragLeaveEvent* e) {
    dropActive_ = false;
    viewport()->update();
    QListView::dragLeaveEvent(e);
}

void PlaylistView::dropEvent(QDropEvent* e) {
    dropActive_ = false;
    viewport()->update();
    // Files from the file manager are the view's business; a reorder is the
    // model's, and the base class routes it there.
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
    QListView::dropEvent(e);
}

void PlaylistView::keyPressEvent(QKeyEvent* e) {
    switch (e->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (currentIndex().isValid()) {
            emit playRequested();
            return;
        }
        break;
    case Qt::Key_Delete:
        if (currentIndex().isValid()) {
            emit removeRequested();
            return;
        }
        break;
    case Qt::Key_F2:
        if (currentIndex().isValid()) {
            emit renameRequested();
            return;
        }
        break;
    default:
        break;
    }
    QListView::keyPressEvent(e);
}

void PlaylistView::paintEvent(QPaintEvent* e) {
    QListView::paintEvent(e);
    const bool empty = (model() == nullptr || model()->rowCount() == 0);
    if (!empty && !dropActive_) return;

    QPainter p(viewport());
    const QRect r = viewport()->rect();

    if (dropActive_) {
        // Dashed accent frame: the affordance that says "yes, drop it here".
        QPen pen(pld::style::accent(palette()));
        pen.setStyle(Qt::DashLine);
        pen.setWidth(2);
        p.setPen(pen);
        p.drawRoundedRect(r.adjusted(2, 2, -2, -2), pld::style::kRadius, pld::style::kRadius);
    }

    if (!empty || placeholderTitle_.isEmpty()) return;

    QFont f = font();
    f.setBold(true);
    p.setFont(f);
    p.setPen(pld::style::textSecondary(palette()));
    const QRect titleRect(r.left() + 12, r.top() + r.height() / 2 - 22, r.width() - 24, 20);
    p.drawText(titleRect, Qt::AlignHCenter | Qt::AlignVCenter, placeholderTitle_);

    f.setBold(false);
    p.setFont(f);
    const QRect hintRect(r.left() + 12, titleRect.bottom() + 2, r.width() - 24, 40);
    p.drawText(hintRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, placeholderHint_);
}

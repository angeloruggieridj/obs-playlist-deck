// SPDX-License-Identifier: MIT
#include "PlaylistListWidget.hpp"
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

PlaylistListWidget::PlaylistListWidget(QWidget* parent) : QListWidget(parent) {
    setObjectName("pldList");
    // Multi-selection: removing or reordering a block of clips is the common
    // edit before a show, and doing it one row at a time was busywork.
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setDragDropMode(QAbstractItemView::InternalMove); // reorder by drag
    setAcceptDrops(true);
    setDefaultDropAction(Qt::MoveAction);
    setUniformItemSizes(false);
    setEditTriggers(QAbstractItemView::NoEditTriggers); // renaming is explicit
    setContextMenuPolicy(Qt::CustomContextMenu);
}

void PlaylistListWidget::setPlaceholder(const QString& title, const QString& hint) {
    placeholderTitle_ = title;
    placeholderHint_ = hint;
    viewport()->update();
}

void PlaylistListWidget::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) {
        dropActive_ = true;
        viewport()->update();
        e->acceptProposedAction();
    } else {
        QListWidget::dragEnterEvent(e); // internal move
    }
}

void PlaylistListWidget::dragMoveEvent(QDragMoveEvent* e) {
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
    else
        QListWidget::dragMoveEvent(e);
}

void PlaylistListWidget::dragLeaveEvent(QDragLeaveEvent* e) {
    dropActive_ = false;
    viewport()->update();
    QListWidget::dragLeaveEvent(e);
}

void PlaylistListWidget::dropEvent(QDropEvent* e) {
    dropActive_ = false;
    viewport()->update();
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

void PlaylistListWidget::keyPressEvent(QKeyEvent* e) {
    switch (e->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (currentItem()) {
            emit playRequested();
            return;
        }
        break;
    case Qt::Key_Delete:
        if (currentItem()) {
            emit removeRequested();
            return;
        }
        break;
    case Qt::Key_F2:
        if (currentItem()) {
            emit renameRequested();
            return;
        }
        break;
    default:
        break;
    }
    QListWidget::keyPressEvent(e);
}

void PlaylistListWidget::paintEvent(QPaintEvent* e) {
    QListWidget::paintEvent(e);
    const bool empty = (count() == 0);
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

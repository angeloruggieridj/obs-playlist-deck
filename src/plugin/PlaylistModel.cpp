// SPDX-License-Identifier: MIT
#include "PlaylistModel.hpp"

#include <QDataStream>
#include <QIODevice>
#include <QMimeData>
#include <algorithm>

namespace {
// Private to this list: an internal move must not be confused with anything
// else the desktop might be dragging.
const char* kRowsMime = "application/x-obs-playlist-deck-rows";
const PlaylistRow kEmptyRow{};
} // namespace

PlaylistModel::PlaylistModel(QObject* parent) : QAbstractListModel(parent) {}

void PlaylistModel::setRows(QVector<PlaylistRow> rows) {
    beginResetModel();
    rows_ = std::move(rows);
    current_ = -1;
    for (int i = 0; i < rows_.size(); ++i)
        if (rows_[i].current) current_ = i;
    endResetModel();
}

void PlaylistModel::setCurrentRow(int row) {
    if (row == current_) return;
    const int previous = current_;
    current_ = row;
    auto repaint = [this](int at) {
        if (at < 0 || at >= rows_.size()) return;
        rows_[at].current = (at == current_);
        const QModelIndex idx = index(at);
        emit dataChanged(idx, idx, {CurrentRole, Qt::FontRole, Qt::ForegroundRole});
    };
    repaint(previous);
    repaint(current_);
}

void PlaylistModel::updateRow(int row, const PlaylistRow& value) {
    if (row < 0 || row >= rows_.size()) return;
    rows_[row] = value;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx);
}

const PlaylistRow& PlaylistModel::rowAt(int row) const {
    if (row < 0 || row >= rows_.size()) return kEmptyRow;
    return rows_[row];
}

int PlaylistModel::rowCount(const QModelIndex& parent) const {
    // A list model has no children: without this, a view asking about a child
    // index would be told there are rows under it.
    if (parent.isValid()) return 0;
    return static_cast<int>(rows_.size());
}

QVariant PlaylistModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rows_.size()) return {};
    const PlaylistRow& row = rows_[index.row()];
    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
        return row.title;
    case Qt::ToolTipRole:
        return row.path;
    case ModelIndexRole:
        return index.row();
    case DurationRole:
        return row.duration;
    case MissingRole:
        return row.missing;
    case CurrentRole:
        return row.current;
    case PathRole:
        return row.path;
    default:
        return {};
    }
}

bool PlaylistModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!index.isValid() || role != Qt::EditRole) return false;
    const QString title = value.toString().trimmed();
    if (title.isEmpty()) return false;
    // The dock applies it, through the undo history, and sends the rows back.
    emit renameRequested(index.row(), title);
    return true;
}

Qt::ItemFlags PlaylistModel::flags(const QModelIndex& index) const {
    Qt::ItemFlags base = QAbstractListModel::flags(index);
    if (index.isValid())
        return base | Qt::ItemIsEditable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
    // Dropping past the last row is how you move something to the end.
    return base | Qt::ItemIsDropEnabled;
}

Qt::DropActions PlaylistModel::supportedDropActions() const { return Qt::MoveAction; }

QStringList PlaylistModel::mimeTypes() const { return {QString::fromUtf8(kRowsMime)}; }

QMimeData* PlaylistModel::mimeData(const QModelIndexList& indexes) const {
    QVector<int> rows;
    for (const QModelIndex& index : indexes)
        if (index.isValid()) rows.append(index.row());
    if (rows.isEmpty()) return nullptr;
    std::sort(rows.begin(), rows.end());

    auto* mime = new QMimeData();
    QByteArray encoded;
    QDataStream stream(&encoded, QIODevice::WriteOnly);
    stream << rows;
    mime->setData(QString::fromUtf8(kRowsMime), encoded);
    return mime;
}

bool PlaylistModel::canDropMimeData(const QMimeData* data, Qt::DropAction, int, int,
                                    const QModelIndex&) const {
    return data && data->hasFormat(QString::fromUtf8(kRowsMime));
}

bool PlaylistModel::dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int,
                                 const QModelIndex& parent) {
    if (action == Qt::IgnoreAction) return true;
    if (!data || !data->hasFormat(QString::fromUtf8(kRowsMime))) return false;

    QByteArray encoded = data->data(QString::fromUtf8(kRowsMime));
    QDataStream stream(&encoded, QIODevice::ReadOnly);
    QVector<int> rows;
    stream >> rows;
    if (rows.isEmpty()) return false;

    // `row` is -1 when the drop landed on an item rather than between two.
    int destination = row;
    if (destination < 0) destination = parent.isValid() ? parent.row() : rowCount();
    emit moveRequested(rows, destination);

    // Deliberately false. Returning true would have the view delete the source
    // rows itself to complete the "move", on top of the reorder the dock is
    // about to perform — the rows would be moved and then removed.
    return false;
}

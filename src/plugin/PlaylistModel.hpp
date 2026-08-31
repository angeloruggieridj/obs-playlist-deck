// SPDX-License-Identifier: MIT
#pragma once
#include <QAbstractListModel>
#include <QString>
#include <QStringList>
#include <QVector>

// One row as the list draws it. The dock builds these from the playlist and the
// scanner's answers; the model is a view onto them, not a second copy of the
// truth.
struct PlaylistRow {
    QString title;
    QString path;
    QString duration; // formatted, empty when unknown
    bool missing = false;
    bool current = false;
};

// The list's model.
//
// It replaced a QListWidget whose every change meant clearing the widget and
// building every row again — with the selection, the scroll position and any
// open editor thrown away with them. A model can say "this row changed" and the
// view keeps the rest.
//
// Reordering and renaming are *requests*: the dock owns the playlist, applies
// them through the undo history, and hands back a new set of rows. A model that
// mutated the data directly would have to reimplement that, or lose it.
class PlaylistModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        ModelIndexRole = Qt::UserRole, // position in the playlist
        DurationRole,
        MissingRole,
        CurrentRole,
        PathRole,
    };

    explicit PlaylistModel(QObject* parent = nullptr);

    // Replaces every row. Used when the playlist itself changed shape.
    void setRows(QVector<PlaylistRow> rows);
    // Same rows, different playing item: two rows repaint instead of all of them.
    void setCurrentRow(int row);
    // Same rows, new duration/missing state for one path.
    void updateRow(int row, const PlaylistRow& value);

    const PlaylistRow& rowAt(int row) const;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    Qt::DropActions supportedDropActions() const override;
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    bool canDropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                         const QModelIndex& parent) const override;
    bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                      const QModelIndex& parent) override;

signals:
    // The user finished editing a title in place.
    void renameRequested(int index, const QString& title);
    // Rows were dragged onto `destination` (the index they should end up at).
    void moveRequested(const QVector<int>& rows, int destination);

private:
    QVector<PlaylistRow> rows_;
    int current_ = -1;
};

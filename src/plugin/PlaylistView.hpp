// SPDX-License-Identifier: MIT
#pragma once
#include <QListView>
#include <QString>
#include <QStringList>

// The playlist list: a QListView over PlaylistModel.
//
// It keeps what the old QListWidget did — media files dropped from the file
// manager, reorder by drag, keyboard operation, and an empty state that says
// how to fill it — and adds nothing the model can do better.
class PlaylistView : public QListView {
    Q_OBJECT
public:
    explicit PlaylistView(QWidget* parent = nullptr);

    // Two lines drawn centred when the list is empty: what this is, and how to
    // fill it. Localized by the dock.
    void setPlaceholder(const QString& title, const QString& hint);

signals:
    void filesDropped(const QStringList& paths);
    // Keyboard equivalents of the toolbar, so the dock is operable without a
    // mouse: Return plays, Delete removes, F2 renames.
    void playRequested();
    void removeRequested();
    void renameRequested();

protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dragLeaveEvent(QDragLeaveEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void paintEvent(QPaintEvent* e) override;

private:
    QString placeholderTitle_;
    QString placeholderHint_;
    bool dropActive_ = false;
};

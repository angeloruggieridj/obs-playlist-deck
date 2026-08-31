// SPDX-License-Identifier: MIT
#pragma once
#include <QListWidget>
#include <QString>
#include <QStringList>

// QListWidget that accepts media files dropped from the OS file manager and
// supports reordering items by internal drag. Emits high-level signals the dock
// turns into playlist-model mutations.
//
// It also draws its own empty state and drop affordance: dropping files onto an
// empty list has always worked, but nothing on screen said so.
class PlaylistListWidget : public QListWidget {
    Q_OBJECT
public:
    explicit PlaylistListWidget(QWidget* parent = nullptr);

    // Two lines drawn centred when the list is empty: what this is, and how to
    // fill it. Localized by the dock.
    void setPlaceholder(const QString& title, const QString& hint);

signals:
    void filesDropped(const QStringList& paths);
    void reordered();
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

#pragma once
#include <QListWidget>
#include <QStringList>

// QListWidget that accepts media files dropped from the OS file manager and
// supports reordering items by internal drag. Emits high-level signals the dock
// turns into playlist-model mutations.
class PlaylistListWidget : public QListWidget {
    Q_OBJECT
public:
    explicit PlaylistListWidget(QWidget* parent = nullptr);

signals:
    void filesDropped(const QStringList& paths);
    void reordered();

protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dropEvent(QDropEvent* e) override;
};

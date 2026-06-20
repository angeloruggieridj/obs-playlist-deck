#pragma once
#include <QDockWidget>
#include <obs.h>
#include "Playlist.hpp"
#include "MediaSourceController.hpp"

class QListWidget;
class QComboBox;
class QLabel;
class QCheckBox;

class PlaylistDock : public QDockWidget {
    Q_OBJECT
public:
    explicit PlaylistDock(QWidget* parent = nullptr);
    ~PlaylistDock() override;

    void refreshSources();

public slots:
    void advance(); // next or stop (used by media_ended + Next button)

private slots:
    void onAddFiles();
    void onRemove();
    void onUp();
    void onDown();
    void onClear();
    void onPlaySelected();
    void onPlay();
    void onPause();
    void onTogglePlayPause();
    void onStop();
    void onPrev();
    void onSourceChanged(int index);
    void onSavePlaylist();
    void onLoadPlaylist();
    void onDeletePlaylist();
    void onImport();
    void onExport();

private:
    void buildUi();
    void rebuildList();
    void selectAndPlay(int row);
    void setStatus(const QString& msg, bool error = false);
    std::string configDir() const;
    void refreshPlaylistCombo();

    void registerHotkeys();
    void unregisterHotkeys();

    pld::Playlist playlist_;
    MediaSourceController controller_;
    bool wrap_ = true;

    QComboBox* sourceCombo_ = nullptr;
    QListWidget* list_ = nullptr;
    QComboBox* playlistCombo_ = nullptr;
    QCheckBox* wrapCheck_ = nullptr;
    QLabel* status_ = nullptr;

    obs_hotkey_id hkNext_ = OBS_INVALID_HOTKEY_ID;
    obs_hotkey_id hkPrev_ = OBS_INVALID_HOTKEY_ID;
    obs_hotkey_id hkPlayPause_ = OBS_INVALID_HOTKEY_ID;
    obs_hotkey_id hkStop_ = OBS_INVALID_HOTKEY_ID;
};

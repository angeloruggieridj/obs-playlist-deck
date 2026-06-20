#pragma once
#include <QDockWidget>
#include <QIcon>
#include <QString>
#include <obs.h>
#include "Playlist.hpp"
#include "MediaSourceController.hpp"

class QListWidget;
class QComboBox;
class QLabel;
class QNetworkAccessManager;

class PlaylistDock : public QDockWidget {
    Q_OBJECT
public:
    explicit PlaylistDock(QWidget* parent = nullptr);
    ~PlaylistDock() override;

    void refreshSources();

    // Release all libobs resources while libobs is still alive. Called on
    // OBS_FRONTEND_EVENT_EXIT so we never touch obs during teardown.
    void shutdown();

    // End-of-clip behavior (matches the "On end" combo order).
    enum EndMode { PlayNext = 0, Loop = 1, LoadNext = 2, StopAtEnd = 3 };

private slots:
    void onAddFiles();
    void onRemove();
    void onUp();
    void onDown();
    void onClear();
    void onPlaySelected();
    void onTogglePlayPause();
    void onStop();
    void onNext();
    void onPrev();
    void onSourceChanged(int index);
    void onSavePlaylist();
    void onOpenPlaylist();
    void onRenamePlaylist();
    void onRemoveSaved();
    void onSavedSelected(int index);
    void onMediaEnded();
    void captureCurrentDuration();

private:
    void buildUi();
    void rebuildList();
    QString itemText(int row) const;
    void playIndex(int row);
    void loadIndex(int row);
    bool loadPlaylistFile(const QString& path);
    void addOrSelectSaved(const QString& path);
    void setStatus(const QString& msg, bool error = false);
    QIcon tintedIcon(const QString& resource) const;
    void checkForUpdate();

    QString settingsPath() const;
    void saveSettings() const;
    void loadSettings();

    void registerHotkeys();
    void unregisterHotkeys();

    pld::Playlist playlist_;
    MediaSourceController controller_;
    EndMode mode_ = PlayNext;
    QString pendingSource_;   // source name to reselect after refreshSources()
    bool loadingCombo_ = false;
    bool obsShutdown_ = false;

    QComboBox* sourceCombo_ = nullptr;
    QListWidget* list_ = nullptr;
    QComboBox* playlistCombo_ = nullptr;
    QComboBox* endCombo_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* versionLabel_ = nullptr;
    QNetworkAccessManager* net_ = nullptr;

    obs_hotkey_id hkNext_ = OBS_INVALID_HOTKEY_ID;
    obs_hotkey_id hkPrev_ = OBS_INVALID_HOTKEY_ID;
    obs_hotkey_id hkPlayPause_ = OBS_INVALID_HOTKEY_ID;
    obs_hotkey_id hkStop_ = OBS_INVALID_HOTKEY_ID;
};

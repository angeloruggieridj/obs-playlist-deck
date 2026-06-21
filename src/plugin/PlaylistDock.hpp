#pragma once
#include <QDockWidget>
#include <QIcon>
#include <QString>
#include <random>
#include <obs.h>
#include "Playlist.hpp"
#include "MediaSourceController.hpp"

class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QTimer;
class QNetworkAccessManager;
class PlaylistListWidget;

class PlaylistDock : public QDockWidget {
    Q_OBJECT
public:
    explicit PlaylistDock(QWidget* parent = nullptr);
    ~PlaylistDock() override;

    void refreshSources();
    void shutdown();

    // End-of-clip behavior (matches the "On end" combo order).
    enum EndMode { PlayNext = 0, Loop = 1, LoadNext = 2, StopAtEnd = 3, Shuffle = 4, RepeatOne = 5 };

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
    void onMediaEnded();
    void onSourceDeactivated();
    void onFilesDropped(const QStringList& paths);
    void onListReordered();
    void onFilterChanged(const QString& text);
    void onTick();
    void onOpenSettings();
    void applyProbedDuration(const QString& path, long long durationMs);
    void captureCurrentDuration();

private:
    void buildUi();
    void rebuildList();
    void applyFilter();
    QString itemText(int row) const;
    void addPaths(const QStringList& paths);
    void playIndex(int row);
    void loadIndex(int row);
    bool loadPlaylistFile(const QString& path);
    void setLoadedPlaylist(const QString& path);
    void setStatus(const QString& msg, bool error = false);
    QIcon tintedIcon(const QString& resource) const;
    void checkForUpdate();
    void startBackgroundProbe(const QStringList& paths);

    QString settingsPath() const;
    void saveSettings() const;
    void loadSettings();
    QString sessionPath() const;
    void saveSession() const;
    void loadSession();

    void registerHotkeys();
    void unregisterHotkeys();

    pld::Playlist playlist_;
    MediaSourceController controller_;
    EndMode mode_ = PlayNext;
    QString pendingSource_;
    bool obsShutdown_ = false;
    bool pendingStageNext_ = false;
    bool enableProbe_ = true;
    bool autoRestore_ = false;
    std::mt19937 rng_{std::random_device{}()};

    QComboBox* sourceCombo_ = nullptr;
    PlaylistListWidget* list_ = nullptr;
    QLineEdit* filterEdit_ = nullptr;
    QComboBox* endCombo_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QLabel* timeLabel_ = nullptr;
    QLabel* loadedLabel_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* versionLabel_ = nullptr;
    QTimer* uiTimer_ = nullptr;
    QNetworkAccessManager* net_ = nullptr;

    obs_hotkey_id hkNext_ = OBS_INVALID_HOTKEY_ID;
    obs_hotkey_id hkPrev_ = OBS_INVALID_HOTKEY_ID;
    obs_hotkey_id hkPlayPause_ = OBS_INVALID_HOTKEY_ID;
    obs_hotkey_id hkStop_ = OBS_INVALID_HOTKEY_ID;
};

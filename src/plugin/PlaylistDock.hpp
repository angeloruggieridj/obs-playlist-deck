#pragma once
#include <QWidget>
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
class PlaylistListWidget;

// Plain widget, not a QDockWidget: OBS wraps it in its own dock through
// obs_frontend_add_dock_by_id(), which is what puts the entry in the Docks
// menu and saves the dock's visibility and geometry across restarts.
class PlaylistDock : public QWidget {
    Q_OBJECT
public:
    explicit PlaylistDock(QWidget* parent = nullptr);
    ~PlaylistDock() override;

    // Localized dock title, handed to obs_frontend_add_dock_by_id().
    static QString dockTitle();

    // Second-stage init, run on OBS_FRONTEND_EVENT_FINISHED_LOADING. The dock
    // is registered from obs_module_load() so OBS restores its saved state, but
    // everything touching sources, the session or hotkeys needs the frontend to
    // have finished loading first.
    void frontendLoaded();

    void refreshSources();
    // Releases the strong OBS source reference before its scene collection is
    // unloaded. The selector is refreshed after the new collection loads.
    void releaseSource();
    void shutdown();

    // Remote control entry points (invoked from the obs-websocket vendor API on
    // the websocket thread via queued connection — keep them invokable).
    Q_INVOKABLE void wsNext();
    Q_INVOKABLE void wsPrev();
    Q_INVOKABLE void wsStop();
    Q_INVOKABLE void wsPlayPause();
    Q_INVOKABLE void wsPlayIndex(int index);
    Q_INVOKABLE void wsLoad(const QString& path);
    int itemCount() const { return playlist_.size(); }
    int currentIndex() const { return playlist_.currentIndex(); }

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
    // manual == true means the user asked from Settings, so the outcome is
    // reported in the status line instead of only the OBS log.
    void checkForUpdate(bool manual);
    void applyUpdateCheckResult(const QString& body, const QString& error, bool manual);
    void startBackgroundProbe(const QStringList& paths);
    // Clears the bound source's file only when this plugin is the one that put
    // it there. A path the user configured in OBS is never emptied.
    void clearStalePluginFile();

    QString settingsPath() const;
    void saveSettings() const;
    void loadSettings();
    void applyLocale();              // sets the module locale from language_
    void applyLocaleAndRebuild();    // applyLocale() + rebuild the UI in the new language
    QString sessionPath() const;
    void saveSession() const;
    void loadSession();

    // Retitles the QDockWidget OBS wrapped this widget in (language change).
    void applyDockTitle();

    void registerHotkeys();
    void unregisterHotkeys();

    pld::Playlist playlist_;
    MediaSourceController controller_;
    EndMode mode_ = PlayNext;
    // The media source the user chose. Survives scene collection switches where
    // the incoming collection has no source by that name, so the binding comes
    // back when they switch to a collection that does.
    QString pendingSource_;
    bool obsShutdown_ = false;
    bool refreshing_ = false; // true while refreshSources() repopulates the combo
    bool pendingStageNext_ = false;
    bool enableProbe_ = true;
    bool autoRestore_ = false;
    QString language_ = "auto"; // "auto" (follow OBS) | "en-US" | "it-IT"
    QString loadedPath_;        // currently loaded playlist file, for label restore
    std::mt19937 rng_{std::random_device{}()};

    QWidget* content_ = nullptr; // what buildUi() fills; replaced on rebuild
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

    obs_hotkey_id hkNext_ = OBS_INVALID_HOTKEY_ID;
    obs_hotkey_id hkPrev_ = OBS_INVALID_HOTKEY_ID;
    obs_hotkey_id hkPlayPause_ = OBS_INVALID_HOTKEY_ID;
    obs_hotkey_id hkStop_ = OBS_INVALID_HOTKEY_ID;
};

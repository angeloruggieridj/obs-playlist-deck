// SPDX-License-Identifier: MIT
#pragma once
#include <QHash>
#include <QIcon>
#include <QList>
#include <QPair>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>
#include <memory>
#include <mutex>
#include <random>
#include <vector>
#include <obs.h>
#include "EndMode.hpp"
#include "History.hpp"
#include "Library.hpp"
#include "MediaScanner.hpp"
#include "MediaSourceController.hpp"
#include "PlaybackEngine.hpp"
#include "Playlist.hpp"
#include "SettingsStore.hpp"

class QComboBox;
class QFileSystemWatcher;
class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QTimer;
class PlaylistModel;
class PlaylistView;
class UpdateChecker;

// What the remote-control API reports. Read from the websocket thread, written
// only from the UI thread, both under `snapshotMutex_`.
//
// The vendor callbacks used to read the playlist model directly from the
// websocket thread while the UI thread was mutating the same std::vector — a
// real data race, and undefined behaviour whenever the read landed inside an
// erase. Nothing outside this struct is shared any more.
struct DeckStatus {
    int count = 0;
    int currentIndex = -1;
    QString currentTitle;
    QString currentPath;
    long long positionMs = -1;
    long long durationMs = -1;
    bool playing = false;
    bool paused = false;
    bool muted = false;
    bool sourceBound = false;
    QString sourceName;
    int mode = 0;
    QString modeName;
    QString playlistName;
    int playlistIndex = 0;
    QStringList playlists;
    int upNextIndex = -1;
    QString upNextTitle;
    long long scheduledStartMs = -1;
    // title/path pairs, for the paginated GetItems request.
    QList<QPair<QString, QString>> items;
};

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
    // everything touching sources, the library or hotkeys needs the frontend to
    // have finished loading first.
    void frontendLoaded();

    void refreshSources();
    // Releases the strong OBS source reference before its scene collection is
    // unloaded. The selector is refreshed after the new collection loads.
    void releaseSource();
    void shutdown();

    // Program/preview changed: the trigger for a staged clip (see Staging.hpp).
    void programLayoutChanged();

    // Remote control entry points (invoked from the obs-websocket vendor API on
    // the websocket thread via queued connection — keep them invokable).
    Q_INVOKABLE void wsNext();
    Q_INVOKABLE void wsPrev();
    Q_INVOKABLE void wsStop();
    Q_INVOKABLE void wsPlayPause();
    Q_INVOKABLE void wsPlayIndex(int index);
    Q_INVOKABLE void wsLoad(const QString& path);
    Q_INVOKABLE void wsSetMode(int mode);
    Q_INVOKABLE void wsSeek(int ms);
    Q_INVOKABLE void wsClear();
    Q_INVOKABLE void wsAddPaths(const QStringList& paths);
    Q_INVOKABLE void wsSetMute(bool muted);
    Q_INVOKABLE void wsToggleMute();
    Q_INVOKABLE void wsSave(const QString& path);
    Q_INVOKABLE void wsMove(int from, int to);
    Q_INVOKABLE void wsRemove(int index);
    Q_INVOKABLE void wsSwitchPlaylist(const QString& name);
    Q_INVOKABLE void wsPanic();

    // Thread-safe copy for the websocket thread.
    DeckStatus status() const;

    // What an OBS hotkey callback is handed. Owned by the dock (they used to be
    // bare `new`s that nothing ever freed) and released only after the hotkeys
    // themselves are unregistered, so no callback can still be in flight.
    struct HotkeyTarget {
        PlaylistDock* dock = nullptr;
        void (PlaylistDock::*method)() = nullptr;
        int index = -1; // for "play item N"; -1 for the plain actions
    };

private slots:
    void onAddFiles();
    void onAddFolder();
    void onRemove();
    void onUp();
    void onDown();
    void onClear();
    void onRename();
    void onPlaySelected();
    void onTogglePlayPause();
    void onToggleMute();
    // The source's mute changed — from this button, from a hotkey, or from the
    // OBS audio mixer, which is why the dock listens instead of remembering.
    void onMuteChanged(bool muted);
    void onStop();
    void onPanic();
    void onNext();
    void onPrev();
    void onUndo();
    void onRedo();
    void onSourceChanged(int index);
    void onSavePlaylist();
    void onOpenPlaylist();
    void onExportCsv();
    void onImportFromSource();
    void onMediaEnded();
    void onMediaStarted();
    void onSourceDeactivated();
    void onFilesDropped(const QStringList& paths);
    void onRowsMoved(const QVector<int>& rows, int destination);
    void onFilterChanged(const QString& text);
    void onTick();
    void onOpenSettings();
    void onScanResults(const QList<ScanResult>& results);
    void onRecheckFiles();
    void onFindMoved();
    void onItemRenamed(int index, const QString& title);
    void onContextMenu(const QPoint& pos);
    void onSeekReleased();
    void onUpdateResult(const QString& body, const QString& error, bool manual);
    // Playlist library.
    void onPlaylistSelected(int index);
    void onPlaylistMenu();
    void onPlaylistProperties();
    void onWatchedFolderChanged();

protected:
    // Theme switches (OBS 31 can change theme while running) and the dock being
    // shown or hidden both arrive here.
    void changeEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    enum class StatusKind { Info, Success, Warning, Error };

    void buildUi();
    void rebuildList();
    void applyFilter();
    void updateNowPlaying();
    void updateTotals();
    void updateTransportIcons();
    void updateMuteButton();
    void updatePlaylistCombo();
    QString itemText(int row) const;
    void addPaths(const QStringList& paths, bool recordUndo = true);
    void playIndex(int row);
    // Turns an engine decision into status text, a duration capture, a vendor
    // event and a redraw.
    void applyPlayback(const pld::PlaybackResult& result);
    bool loadPlaylistFile(const QString& path);
    // Shared by the Save button and the remote Save request: the extension
    // decides the format, and the write is atomic either way.
    bool writePlaylistTo(const QString& path);
    // Shared by the Remove button, the Delete key and the remote request.
    void removeRows(std::vector<int> rows);
    // Rows the user has selected, as playlist indices.
    std::vector<int> selectedRows() const;
    void setLoadedPlaylist(const QString& path);
    void setStatus(const QString& msg, StatusKind kind = StatusKind::Info);
    QIcon tintedIcon(const QString& resource) const;
    void retintIcons();
    void applyTheme();
    void snapshotStatus();
    void snapshotPlayback(); // just the volatile fields, for the twice-a-second tick
    void emitVendorItemStarted();

    // Records the state before a destructive edit so Ctrl+Z can bring it back.
    void recordUndo(const QString& label);
    void applyHistoryState(std::vector<pld::PlaylistItem> items, int current);

    // Reads the duration from the source once it reports the clip started, and
    // only for the item that was actually scheduled — a fixed timer used to
    // write the duration onto whatever item happened to be current 700 ms later.
    void captureDuration();

    void startScan(const QStringList& paths, bool replacesPlaylist = false);
    void rescanAll();

    // Clears the bound source's file only when this plugin is the one that put
    // it there. A path the user configured in OBS is never emptied.
    void clearStalePluginFile();

    void saveSettings() const;
    void loadSettings();
    void applyLocale();           // sets the module locale from the chosen language
    void applyLocaleAndRebuild(); // applyLocale() + rebuild the UI in the new language

    // ---- Playlist library ----
    void loadLibrary();
    // Copies the live playlist back into the active library entry. Everything
    // that persists or switches goes through this first.
    void commitToLibrary();
    void activateLibraryEntry(int index);
    void saveLibrarySoon(); // debounced
    void saveLibraryNow();
    void applyWatchFolder();
    void scanWatchFolder();
    // Fires a scheduled start when its moment arrives.
    void checkSchedule();

    void registerHotkeys();
    void unregisterHotkeys();

    // Declaration order matters: the engine takes references to the three
    // members above it.
    pld::Playlist playlist_;
    MediaSourceController controller_;
    std::mt19937 rng_{std::random_device{}()};
    // Owns what plays next and when — the end-of-clip modes, the shuffle bag and
    // the staged-clip rule. All of it is unit-tested against a fake transport;
    // the dock only reacts to what the engine decided.
    pld::PlaybackEngine engine_{playlist_, controller_, rng_};
    pld::History history_;
    pld::Library library_;
    SettingsStore store_;
    DeckSettings settings_;
    MediaScanner* scanner_ = nullptr;
    UpdateChecker* updateChecker_ = nullptr;
    QFileSystemWatcher* watcher_ = nullptr;

    bool obsShutdown_ = false;
    bool refreshing_ = false;        // true while refreshSources() repopulates the combo
    bool switchingPlaylist_ = false; // true while the library combo is repopulated
    bool stagePauseWanted_ = false;  // a staged clip must pause as soon as it opens
    int captureRow_ = -1;            // item whose duration the next media_started belongs to
    QString capturePath_;
    int captureRetries_ = 0;
    // Start time the scheduler has already honoured, so it fires once.
    long long lastScheduleFiredMs_ = -1;

    // Existence of each path, so rebuilding the list does not stat() every item
    // again. Filled by the scanner thread; a missing entry means "not checked
    // yet", which is drawn as no warning at all rather than a false alarm.
    QHash<QString, bool> existsCache_;

    mutable std::mutex snapshotMutex_;
    DeckStatus snapshot_;
    qint64 lastStateEventMs_ = 0;

    QWidget* content_ = nullptr; // what buildUi() fills; replaced on rebuild
    QComboBox* playlistCombo_ = nullptr;
    QComboBox* sourceCombo_ = nullptr;
    PlaylistModel* model_ = nullptr;
    PlaylistView* list_ = nullptr;
    QLineEdit* filterEdit_ = nullptr;
    QLabel* filterCount_ = nullptr;
    QComboBox* endCombo_ = nullptr;
    QFrame* card_ = nullptr;
    QLabel* nowTitle_ = nullptr;
    QLabel* nowMeta_ = nullptr;
    QLabel* upNext_ = nullptr;
    QSlider* seek_ = nullptr;
    QLabel* timeLabel_ = nullptr;
    QLabel* totalsLabel_ = nullptr;
    QLabel* loadedLabel_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* versionLabel_ = nullptr;
    QPushButton* playPauseBtn_ = nullptr;
    QPushButton* muteBtn_ = nullptr;
    int lastTransportState_ = -1; // 1 playing, 0 not, -1 unknown: skip identical repaints
    QPushButton* undoBtn_ = nullptr;
    QTimer* uiTimer_ = nullptr;
    QTimer* statusTimer_ = nullptr;
    QTimer* libraryTimer_ = nullptr;
    QTimer* watchTimer_ = nullptr;

    // Every icon button, so a theme switch can re-tint all of them, paired with
    // the resource each one draws.
    QList<QPair<QPointer<QPushButton>, QString>> iconButtons_;
    mutable QHash<QString, QIcon> iconCache_;

    std::vector<obs_hotkey_id> hotkeys_;
    std::vector<std::unique_ptr<HotkeyTarget>> hotkeyTargets_;
};

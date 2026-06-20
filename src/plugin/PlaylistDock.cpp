#include "PlaylistDock.hpp"
#include "PlaylistIO.hpp"
#include "MediaPath.hpp"
#include "MediaProbe.hpp"
#include "Format.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QAbstractItemView>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <utility>

using namespace pld;

PlaylistDock::PlaylistDock(QWidget* parent) : QDockWidget(parent) {
    setWindowTitle(QString::fromUtf8(obs_module_text("PlaylistDeck")));
    setObjectName("obs-playlist-deck-dock");
    buildUi();
    refreshSources();
    refreshPlaylistCombo();
    controller_.setOnMediaEnded([this]() {
        QMetaObject::invokeMethod(this, "onMediaEnded", Qt::QueuedConnection);
    });
    registerHotkeys();
}

PlaylistDock::~PlaylistDock() {
    unregisterHotkeys();
    controller_.setOnMediaEnded(nullptr);
    controller_.unbind();
}

void PlaylistDock::buildUi() {
    auto* root = new QWidget(this);
    auto* col = new QVBoxLayout(root);
    col->setContentsMargins(4, 4, 4, 4);
    col->setSpacing(4);

    // Helper: compact icon tool button with a tooltip.
    auto mk = [this](QStyle::StandardPixmap sp, const QString& tip) {
        auto* b = new QToolButton();
        b->setIcon(style()->standardIcon(sp));
        b->setToolTip(tip);
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        return b;
    };

    // Source dropdown gets a full-width row of its own.
    sourceCombo_ = new QComboBox();
    sourceCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* refreshBtn = mk(QStyle::SP_BrowserReload, "Refresh source list");
    auto* srcRow = new QHBoxLayout();
    srcRow->addWidget(sourceCombo_, 1);
    srcRow->addWidget(refreshBtn);
    col->addLayout(srcRow);

    list_ = new QListWidget();
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    col->addWidget(list_, 1);

    // Item operations (icons only).
    auto* addBtn = mk(QStyle::SP_DialogOpenButton, "Add media files");
    auto* rmBtn = mk(QStyle::SP_TrashIcon, "Remove selected");
    auto* upBtn = mk(QStyle::SP_ArrowUp, "Move up");
    auto* downBtn = mk(QStyle::SP_ArrowDown, "Move down");
    auto* clrBtn = mk(QStyle::SP_DialogResetButton, "Clear playlist");
    auto* opsRow = new QHBoxLayout();
    opsRow->setSpacing(2);
    for (auto* b : {addBtn, rmBtn, upBtn, downBtn, clrBtn}) opsRow->addWidget(b);
    opsRow->addStretch(1);
    col->addLayout(opsRow);

    // Transport (icons only).
    auto* playSelBtn = mk(QStyle::SP_MediaPlay, "Play selected");
    auto* prevBtn = mk(QStyle::SP_MediaSkipBackward, "Previous");
    auto* playPauseBtn = mk(QStyle::SP_MediaPause, "Play / Pause bound source");
    auto* stopBtn = mk(QStyle::SP_MediaStop, "Stop");
    auto* nextBtn = mk(QStyle::SP_MediaSkipForward, "Next");
    auto* trRow = new QHBoxLayout();
    trRow->setSpacing(2);
    for (auto* b : {playSelBtn, prevBtn, playPauseBtn, stopBtn, nextBtn}) trRow->addWidget(b);
    trRow->addStretch(1);
    col->addLayout(trRow);

    // End-of-clip behavior.
    endCombo_ = new QComboBox();
    endCombo_->addItem("On end: Play next");
    endCombo_->addItem("On end: Loop");
    endCombo_->addItem("On end: Load next (paused)");
    endCombo_->addItem("On end: Stop");
    endCombo_->setCurrentIndex(static_cast<int>(mode_));
    col->addWidget(endCombo_);

    // Saved playlists: combo full-width, then icon actions.
    playlistCombo_ = new QComboBox();
    playlistCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    col->addWidget(playlistCombo_);

    auto* saveBtn = mk(QStyle::SP_DialogSaveButton, "Save current as new playlist");
    auto* loadBtn = mk(QStyle::SP_DialogYesButton, "Load selected playlist");
    auto* renameBtn = mk(QStyle::SP_FileDialogDetailedView, "Rename selected playlist");
    auto* delBtn = mk(QStyle::SP_DialogDiscardButton, "Delete selected playlist");
    auto* impBtn = mk(QStyle::SP_ArrowDown, "Import playlist from file (.json/.m3u)");
    auto* expBtn = mk(QStyle::SP_ArrowUp, "Export playlist to file (.json/.m3u)");
    auto* plRow = new QHBoxLayout();
    plRow->setSpacing(2);
    for (auto* b : {saveBtn, loadBtn, renameBtn, delBtn, impBtn, expBtn}) plRow->addWidget(b);
    plRow->addStretch(1);
    col->addLayout(plRow);

    status_ = new QLabel("");
    status_->setWordWrap(true);
    col->addWidget(status_);

    setWidget(root);

    connect(refreshBtn, &QToolButton::clicked, this, &PlaylistDock::refreshSources);
    connect(addBtn, &QToolButton::clicked, this, &PlaylistDock::onAddFiles);
    connect(rmBtn, &QToolButton::clicked, this, &PlaylistDock::onRemove);
    connect(upBtn, &QToolButton::clicked, this, &PlaylistDock::onUp);
    connect(downBtn, &QToolButton::clicked, this, &PlaylistDock::onDown);
    connect(clrBtn, &QToolButton::clicked, this, &PlaylistDock::onClear);
    connect(playSelBtn, &QToolButton::clicked, this, &PlaylistDock::onPlaySelected);
    connect(prevBtn, &QToolButton::clicked, this, &PlaylistDock::onPrev);
    connect(playPauseBtn, &QToolButton::clicked, this, &PlaylistDock::onTogglePlayPause);
    connect(stopBtn, &QToolButton::clicked, this, &PlaylistDock::onStop);
    connect(nextBtn, &QToolButton::clicked, this, &PlaylistDock::onNext);
    connect(saveBtn, &QToolButton::clicked, this, &PlaylistDock::onSavePlaylist);
    connect(loadBtn, &QToolButton::clicked, this, &PlaylistDock::onLoadPlaylist);
    connect(renameBtn, &QToolButton::clicked, this, &PlaylistDock::onRenamePlaylist);
    connect(delBtn, &QToolButton::clicked, this, &PlaylistDock::onDeletePlaylist);
    connect(impBtn, &QToolButton::clicked, this, &PlaylistDock::onImport);
    connect(expBtn, &QToolButton::clicked, this, &PlaylistDock::onExport);
    connect(list_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem*) { onPlaySelected(); });
    connect(sourceCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &PlaylistDock::onSourceChanged);
    connect(endCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int i) { mode_ = static_cast<EndMode>(i); });
}

QString PlaylistDock::itemLabel(int row) const {
    const auto& it = playlist_.items()[row];
    QString label = QString::fromStdString(it.title);
    std::string dur = formatDuration(it.durationMs);
    if (!dur.empty()) label += QStringLiteral(" (%1)").arg(QString::fromStdString(dur));
    if (row == playlist_.currentIndex()) label = QStringLiteral("\xE2\x96\xB6 ") + label; // ▶
    return label;
}

void PlaylistDock::rebuildList() {
    int sel = list_->currentRow();
    list_->clear();
    for (int i = 0; i < playlist_.size(); ++i) {
        auto* item = new QListWidgetItem(itemLabel(i));
        item->setToolTip(QString::fromStdString(playlist_.items()[i].path));
        list_->addItem(item);
    }
    if (playlist_.currentIndex() >= 0)
        list_->setCurrentRow(playlist_.currentIndex());
    else if (sel >= 0 && sel < playlist_.size())
        list_->setCurrentRow(sel);
}

void PlaylistDock::playIndex(int row) {
    if (!playlist_.setCurrent(row)) return;
    const auto* it = playlist_.current();
    if (!it) return;
    if (!controller_.isBound()) {
        setStatus("No media source bound.", true);
        rebuildList();
        return;
    }
    if (controller_.setFileAndRestart(it->path)) {
        setStatus(QStringLiteral("Playing: %1").arg(QString::fromStdString(it->title)));
        QTimer::singleShot(700, this, &PlaylistDock::captureCurrentDuration);
    } else {
        setStatus("Failed to set media source.", true);
    }
    rebuildList();
}

void PlaylistDock::loadIndex(int row) {
    if (!playlist_.setCurrent(row)) return;
    const auto* it = playlist_.current();
    if (!it) return;
    if (!controller_.isBound()) {
        setStatus("No media source bound.", true);
        rebuildList();
        return;
    }
    if (controller_.setFileLoadOnly(it->path)) {
        setStatus(QStringLiteral("Loaded (paused): %1").arg(QString::fromStdString(it->title)));
        QTimer::singleShot(700, this, &PlaylistDock::captureCurrentDuration);
    } else {
        setStatus("Failed to set media source.", true);
    }
    rebuildList();
}

void PlaylistDock::captureCurrentDuration() {
    int row = playlist_.currentIndex();
    if (row < 0 || row >= playlist_.size()) return;
    if (playlist_.items()[row].durationMs >= 0) return; // already known
    long long d = controller_.currentDurationMs();
    if (d < 0) return;
    // setItems is the only mutator that touches durations; rebuild a copy.
    auto items = playlist_.items();
    items[row].durationMs = d;
    int cur = playlist_.currentIndex();
    playlist_.setItems(std::move(items));
    playlist_.setCurrent(cur);
    rebuildList();
}

void PlaylistDock::onMediaEnded() {
    switch (mode_) {
    case PlayNext: {
        int i = playlist_.next(false);
        if (i >= 0) playIndex(i);
        else controller_.stop();
        break;
    }
    case Loop: {
        int i = playlist_.next(true);
        if (i >= 0) playIndex(i);
        break;
    }
    case LoadNext: {
        int i = playlist_.next(false);
        if (i >= 0) loadIndex(i);
        else controller_.stop();
        break;
    }
    case StopAtEnd:
        controller_.stop();
        break;
    }
}

void PlaylistDock::onAddFiles() {
    QStringList files = QFileDialog::getOpenFileNames(this, "Add media files");
    int added = 0;
    for (const auto& f : files) {
        std::string p = f.toStdString();
        if (!mediapath::isMediaFile(p)) continue;
        PlaylistItem it{p, mediapath::fileStem(p), pld::probeDurationMs(p)};
        playlist_.add(it);
        ++added;
    }
    rebuildList();
    if (added) setStatus(QStringLiteral("Added %1 file(s).").arg(added));
}

void PlaylistDock::onRemove() {
    int row = list_->currentRow();
    if (row >= 0) {
        playlist_.removeAt(row);
        rebuildList();
    }
}

void PlaylistDock::onUp() {
    int r = list_->currentRow();
    if (playlist_.moveUp(r)) {
        rebuildList();
        list_->setCurrentRow(r - 1);
    }
}

void PlaylistDock::onDown() {
    int r = list_->currentRow();
    if (playlist_.moveDown(r)) {
        rebuildList();
        list_->setCurrentRow(r + 1);
    }
}

void PlaylistDock::onClear() {
    playlist_.clear();
    rebuildList();
}

void PlaylistDock::onPlaySelected() {
    int r = list_->currentRow();
    if (r >= 0) playIndex(r);
}

void PlaylistDock::onPlay() { controller_.play(); }
void PlaylistDock::onTogglePlayPause() { controller_.togglePlayPause(); }
void PlaylistDock::onStop() { controller_.stop(); }

void PlaylistDock::onNext() {
    int i = playlist_.next(wrapEnabled());
    if (i >= 0) playIndex(i);
}

void PlaylistDock::onPrev() {
    int i = playlist_.prev(wrapEnabled());
    if (i >= 0) playIndex(i);
}

void PlaylistDock::onSourceChanged(int) {
    QString name = sourceCombo_->currentText();
    if (name.isEmpty()) {
        controller_.unbind();
        return;
    }
    controller_.bind(name.toStdString());
    setStatus(QStringLiteral("Bound to: %1").arg(name));
}

void PlaylistDock::refreshSources() {
    QString prev = sourceCombo_ ? sourceCombo_->currentText() : QString();
    sourceCombo_->blockSignals(true);
    sourceCombo_->clear();
    for (const auto& n : MediaSourceController::listMediaSources())
        sourceCombo_->addItem(QString::fromStdString(n));
    int idx = sourceCombo_->findText(prev);
    if (idx >= 0) sourceCombo_->setCurrentIndex(idx);
    sourceCombo_->blockSignals(false);
    onSourceChanged(sourceCombo_->currentIndex());
}

std::string PlaylistDock::configDir() const {
    char* p = obs_module_config_path("playlists");
    std::string s = p ? p : "";
    bfree(p);
    if (!s.empty()) QDir().mkpath(QString::fromStdString(s));
    return s;
}

void PlaylistDock::refreshPlaylistCombo() {
    QString prev = playlistCombo_->currentText();
    playlistCombo_->clear();
    QDir dir(QString::fromStdString(configDir()));
    const auto entries = dir.entryInfoList(QStringList() << "*.json", QDir::Files, QDir::Name);
    for (const auto& fi : entries) playlistCombo_->addItem(fi.completeBaseName());
    int idx = playlistCombo_->findText(prev);
    if (idx >= 0) playlistCombo_->setCurrentIndex(idx);
}

void PlaylistDock::onSavePlaylist() {
    bool ok = false;
    QString name =
        QInputDialog::getText(this, "Save playlist", "Name:", QLineEdit::Normal, "", &ok);
    if (!ok || name.isEmpty()) return;
    std::string text = io::toJson(name.toStdString(), playlist_.items());
    QString path = QString::fromStdString(configDir()) + "/" + name + ".json";
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(text.c_str());
        f.close();
    }
    refreshPlaylistCombo();
    setStatus(QStringLiteral("Saved playlist: %1").arg(name));
}

void PlaylistDock::onLoadPlaylist() {
    QString name = playlistCombo_->currentText();
    if (name.isEmpty()) return;
    QString path = QString::fromStdString(configDir()) + "/" + name + ".json";
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        setStatus("Cannot open playlist.", true);
        return;
    }
    std::string text = f.readAll().toStdString();
    f.close();
    std::string n;
    std::vector<PlaylistItem> items;
    if (!io::fromJson(text, n, items)) {
        setStatus("Invalid playlist file.", true);
        return;
    }
    playlist_.setItems(std::move(items));
    rebuildList();
    setStatus(QStringLiteral("Loaded playlist: %1").arg(name));
}

void PlaylistDock::onRenamePlaylist() {
    QString oldName = playlistCombo_->currentText();
    if (oldName.isEmpty()) return;
    bool ok = false;
    QString newName =
        QInputDialog::getText(this, "Rename playlist", "New name:", QLineEdit::Normal, oldName, &ok);
    if (!ok || newName.isEmpty() || newName == oldName) return;
    QString dir = QString::fromStdString(configDir());
    QString oldPath = dir + "/" + oldName + ".json";
    QString newPath = dir + "/" + newName + ".json";
    if (QFile::exists(newPath)) {
        setStatus("A playlist with that name already exists.", true);
        return;
    }
    if (!QFile::rename(oldPath, newPath)) {
        setStatus("Rename failed.", true);
        return;
    }
    // Keep the stored "name" field in sync with the file name.
    QFile f(newPath);
    if (f.open(QIODevice::ReadOnly)) {
        std::string text = f.readAll().toStdString();
        f.close();
        std::string n;
        std::vector<PlaylistItem> items;
        if (io::fromJson(text, n, items)) {
            std::string out = io::toJson(newName.toStdString(), items);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                f.write(out.c_str());
                f.close();
            }
        }
    }
    refreshPlaylistCombo();
    int idx = playlistCombo_->findText(newName);
    if (idx >= 0) playlistCombo_->setCurrentIndex(idx);
    setStatus(QStringLiteral("Renamed to: %1").arg(newName));
}

void PlaylistDock::onDeletePlaylist() {
    QString name = playlistCombo_->currentText();
    if (name.isEmpty()) return;
    QFile::remove(QString::fromStdString(configDir()) + "/" + name + ".json");
    refreshPlaylistCombo();
    setStatus(QStringLiteral("Deleted playlist: %1").arg(name));
}

void PlaylistDock::onImport() {
    QString path = QFileDialog::getOpenFileName(this, "Import playlist", "",
                                                "Playlists (*.json *.m3u *.m3u8)");
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        setStatus("Cannot open file.", true);
        return;
    }
    std::string text = f.readAll().toStdString();
    f.close();
    std::vector<PlaylistItem> items;
    if (path.endsWith(".json", Qt::CaseInsensitive)) {
        std::string n;
        if (!io::fromJson(text, n, items)) {
            setStatus("Invalid JSON playlist.", true);
            return;
        }
    } else {
        items = io::parseM3u(text);
    }
    for (auto& it : items)
        if (it.durationMs < 0) it.durationMs = pld::probeDurationMs(it.path);
    playlist_.setItems(std::move(items));
    rebuildList();
    setStatus("Imported playlist.");
}

void PlaylistDock::onExport() {
    QString path = QFileDialog::getSaveFileName(this, "Export playlist", "",
                                                "JSON (*.json);;M3U (*.m3u)");
    if (path.isEmpty()) return;
    std::string text = path.endsWith(".m3u", Qt::CaseInsensitive)
                           ? io::toM3u(playlist_.items())
                           : io::toJson("playlist", playlist_.items());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(text.c_str());
        f.close();
    }
    setStatus("Exported playlist.");
}

void PlaylistDock::setStatus(const QString& msg, bool error) {
    status_->setStyleSheet(error ? "color:#e06c75;" : "");
    status_->setText(msg);
}

// ---- Hotkeys -------------------------------------------------------------

namespace {
struct HotkeyTarget {
    PlaylistDock* dock;
    void (PlaylistDock::*method)();
};

void hotkeyCallback(void* data, obs_hotkey_id, obs_hotkey_t*, bool pressed) {
    if (!pressed) return;
    auto* t = static_cast<HotkeyTarget*>(data);
    PlaylistDock* dock = t->dock;
    auto method = t->method;
    QMetaObject::invokeMethod(
        dock, [dock, method]() { (dock->*method)(); }, Qt::QueuedConnection);
}
} // namespace

void PlaylistDock::registerHotkeys() {
    auto reg = [this](const char* id, const char* desc, void (PlaylistDock::*fn)()) {
        return obs_hotkey_register_frontend(id, desc, hotkeyCallback,
                                            new HotkeyTarget{this, fn});
    };
    hkNext_ = reg("obs-playlist-deck.next", "Playlist Deck: Next", &PlaylistDock::onNext);
    hkPrev_ = reg("obs-playlist-deck.prev", "Playlist Deck: Previous", &PlaylistDock::onPrev);
    hkPlayPause_ = reg("obs-playlist-deck.playpause", "Playlist Deck: Play/Pause",
                       &PlaylistDock::onTogglePlayPause);
    hkStop_ = reg("obs-playlist-deck.stop", "Playlist Deck: Stop", &PlaylistDock::onStop);
}

void PlaylistDock::unregisterHotkeys() {
    for (auto id : {hkNext_, hkPrev_, hkPlayPause_, hkStop_})
        if (id != OBS_INVALID_HOTKEY_ID) obs_hotkey_unregister(id);
}
